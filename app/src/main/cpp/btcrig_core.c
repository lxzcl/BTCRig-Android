#include "btcrig_core.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static const uint32_t k_sha256_initial_state[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
};

static const uint32_t k_sha256_round_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

typedef struct {
    double stop_at;
    uint32_t seed;
    uint64_t hashes;
    uint8_t sink;
} bench_worker_t;

typedef struct {
    pthread_t id;
    uint32_t seed;
} miner_worker_t;

typedef struct {
    char pool[256];
    char user[128];
    char pass[128];
    int cpu_threads;
} core_config_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_running = 0;
static int g_stop_requested = 0;
static miner_worker_t *g_workers = NULL;
static int g_worker_count = 0;
static uint64_t g_total_hashes = 0;
static uint8_t g_sink = 0;
static double g_started_at = 0.0;
static pthread_t g_stratum_thread;
static int g_stratum_started = 0;
static int g_stratum_connected = 0;
static uint64_t g_stratum_jobs = 0;
static char g_pool[256] = "";
static char g_user[128] = "";
static char g_pass[128] = "";
static char g_stratum_status[64] = "idle";
static char g_last_error[128] = "";

static uint32_t rotr32(uint32_t x, unsigned int n) {
    return (x >> n) | (x << (32U - n));
}

static uint32_t load_be32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
}

static void put_be32(uint8_t *out, uint32_t v) {
    uint32_t be = __builtin_bswap32(v);
    memcpy(out, &be, sizeof(be));
}

static void put_be64(uint8_t *out, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void put_le32(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v >> 16);
    out[3] = (uint8_t)(v >> 24);
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void sha256_compress_words(uint32_t state[8], uint32_t w[64]) {
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k_sha256_round_constants[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_compress_block(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load_be32(block + i * 4);
    }
    sha256_compress_words(state, w);
}

static void sha256_finish(uint32_t state[8], const uint8_t *tail, size_t tail_len, uint64_t total_len, uint8_t out[32]) {
    uint8_t block[128];
    size_t block_len = tail_len;

    memset(block, 0, sizeof(block));
    memcpy(block, tail, tail_len);
    block[block_len++] = 0x80;
    if (block_len > 56) {
        put_be64(block + 120, total_len * 8U);
        sha256_compress_block(state, block);
        sha256_compress_block(state, block + 64);
    } else {
        put_be64(block + 56, total_len * 8U);
        sha256_compress_block(state, block);
    }

    for (int i = 0; i < 8; ++i) {
        put_be32(out + i * 4, state[i]);
    }
}

static void sha256_80(const uint8_t in[80], uint8_t out[32]) {
    uint32_t state[8];
    memcpy(state, k_sha256_initial_state, sizeof(state));
    sha256_compress_block(state, in);
    sha256_finish(state, in + 64, 16, 80, out);
}

static void sha256_32(const uint8_t in[32], uint8_t out[32]) {
    uint32_t state[8];
    memcpy(state, k_sha256_initial_state, sizeof(state));
    sha256_finish(state, in, 32, 32, out);
}

static void sha256d_80(const uint8_t header[80], uint8_t out[32]) {
    uint8_t first[32];
    sha256_80(header, first);
    sha256_32(first, out);
}

static void *bench_worker(void *opaque) {
    bench_worker_t *worker = (bench_worker_t *)opaque;
    uint8_t header[80];
    uint8_t out[32];
    uint32_t nonce = worker->seed;
    uint64_t hashes = 0;
    uint8_t sink = 0;

    for (int i = 0; i < 80; ++i) {
        header[i] = (uint8_t)(i + worker->seed);
    }

    while (monotonic_seconds() < worker->stop_at) {
        for (int i = 0; i < 1024; ++i) {
            put_le32(header + 76, nonce++);
            sha256d_80(header, out);
            sink ^= out[0];
        }
        hashes += 1024;
    }

    worker->hashes = hashes;
    worker->sink = sink;
    return NULL;
}

static int available_processors(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count < 1) {
        return 1;
    }
    if (count > 256) {
        return 256;
    }
    return (int)count;
}

static int clamp_threads(int threads) {
    if (threads < 1) {
        return available_processors();
    }
    if (threads > 256) {
        return 256;
    }
    return threads;
}

static void copy_text(char *out, size_t out_size, const char *text) {
    if (out_size == 0) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    snprintf(out, out_size, "%s", text);
}

static int read_config_text(const char *config_path, char *text, size_t text_size) {
    if (config_path == NULL || config_path[0] == '\0') {
        return 0;
    }

    FILE *file = fopen(config_path, "rb");
    if (file == NULL) {
        return 0;
    }

    size_t n = fread(text, 1, text_size - 1, file);
    fclose(file);
    text[n] = '\0';
    return 1;
}

static const char *find_json_value(const char *text, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *found = strstr(text, pattern);
    if (found == NULL) {
        return NULL;
    }
    const char *colon = strchr(found, ':');
    if (colon == NULL) {
        return NULL;
    }
    return colon + 1;
}

static int parse_json_int(const char *text, const char *name) {
    const char *value = find_json_value(text, name);
    if (value == NULL) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || parsed < 0) {
        return 0;
    }
    if (parsed > 256) {
        return 256;
    }
    return (int)parsed;
}

static void parse_json_string(const char *text, const char *name, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';

    const char *value = find_json_value(text, name);
    if (value == NULL) {
        return;
    }
    const char *p = strchr(value, '"');
    if (p == NULL) {
        return;
    }
    ++p;

    size_t i = 0;
    while (*p != '\0' && *p != '"' && i + 1 < out_size) {
        if (*p == '\\' && p[1] != '\0') {
            ++p;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static core_config_t read_config(const char *config_path) {
    core_config_t config;
    memset(&config, 0, sizeof(config));

    char text[4097];
    if (!read_config_text(config_path, text, sizeof(text))) {
        return config;
    }

    // ponytail: flat parser for app-owned config; replace when full BTCRig JSON config is imported.
    config.cpu_threads = parse_json_int(text, "cpu_threads");
    parse_json_string(text, "pool", config.pool, sizeof(config.pool));
    parse_json_string(text, "user", config.user, sizeof(config.user));
    parse_json_string(text, "pass", config.pass, sizeof(config.pass));
    return config;
}

static int miner_should_stop(void) {
    pthread_mutex_lock(&g_lock);
    int stop = g_stop_requested;
    pthread_mutex_unlock(&g_lock);
    return stop;
}

static void miner_add_hashes(uint64_t hashes, uint8_t sink) {
    pthread_mutex_lock(&g_lock);
    g_total_hashes += hashes;
    g_sink ^= sink;
    pthread_mutex_unlock(&g_lock);
}

static void *miner_worker(void *opaque) {
    miner_worker_t *worker = (miner_worker_t *)opaque;
    uint8_t header[80];
    uint8_t out[32];
    uint32_t nonce = worker->seed;
    uint64_t hashes = 0;
    uint8_t sink = 0;

    for (int i = 0; i < 80; ++i) {
        header[i] = (uint8_t)(i + worker->seed);
    }

    while (!miner_should_stop()) {
        for (int i = 0; i < 4096; ++i) {
            put_le32(header + 76, nonce++);
            sha256d_80(header, out);
            sink ^= out[0];
        }
        hashes += 4096;

        if (hashes >= 65536) {
            miner_add_hashes(hashes, sink);
            hashes = 0;
            sink = 0;
        }
    }

    if (hashes != 0) {
        miner_add_hashes(hashes, sink);
    }
    return NULL;
}

static void set_stratum_state(const char *status, const char *error, int connected) {
    pthread_mutex_lock(&g_lock);
    copy_text(g_stratum_status, sizeof(g_stratum_status), status);
    if (error != NULL) {
        copy_text(g_last_error, sizeof(g_last_error), error);
    }
    g_stratum_connected = connected;
    pthread_mutex_unlock(&g_lock);
}

static void add_stratum_job(void) {
    pthread_mutex_lock(&g_lock);
    ++g_stratum_jobs;
    copy_text(g_stratum_status, sizeof(g_stratum_status), "job received");
    pthread_mutex_unlock(&g_lock);
}

static int parse_pool_url(const char *pool, char *host, size_t host_size, char *port, size_t port_size) {
    const char *p = pool;
    const char *prefix = "stratum+tcp://";
    if (strncmp(p, prefix, strlen(prefix)) == 0) {
        p += strlen(prefix);
    } else {
        prefix = "tcp://";
        if (strncmp(p, prefix, strlen(prefix)) == 0) {
            p += strlen(prefix);
        }
    }

    const char *slash = strchr(p, '/');
    const char *end = slash == NULL ? p + strlen(p) : slash;
    const char *colon = NULL;
    for (const char *it = p; it < end; ++it) {
        if (*it == ':') {
            colon = it;
        }
    }
    if (colon == NULL || colon == p || colon + 1 >= end) {
        return 0;
    }

    size_t host_len = (size_t)(colon - p);
    size_t port_len = (size_t)(end - colon - 1);
    if (host_len >= host_size || port_len >= port_size) {
        return 0;
    }
    memcpy(host, p, host_len);
    host[host_len] = '\0';
    memcpy(port, colon + 1, port_len);
    port[port_len] = '\0';
    return 1;
}

static int wait_socket(int fd, short events, int timeout_ms) {
    struct pollfd poll_fd;
    memset(&poll_fd, 0, sizeof(poll_fd));
    poll_fd.fd = fd;
    poll_fd.events = events;

    while (!miner_should_stop()) {
        int rc = poll(&poll_fd, 1, timeout_ms);
        if (rc > 0) {
            if ((poll_fd.revents & events) != 0) {
                return 1;
            }
            if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                return -1;
            }
            return 0;
        }
        if (rc < 0 && errno != EINTR) {
            return 0;
        }
    }
    return 0;
}

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    if (getaddrinfo(host, port, &hints, &result) != 0) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *ai = result; ai != NULL && !miner_should_stop(); ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0 || errno == EINPROGRESS) {
            if (rc == 0 || wait_socket(fd, POLLOUT, 5000) > 0) {
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                    break;
                }
            }
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

static int socket_send_all(int fd, const char *text) {
    size_t sent = 0;
    size_t len = strlen(text);
    while (sent < len && !miner_should_stop()) {
        if (wait_socket(fd, POLLOUT, 1000) <= 0) {
            return 0;
        }
        ssize_t n = send(fd, text + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return 0;
        }
    }
    return sent == len;
}

static int socket_read_line(int fd, char *line, size_t line_size) {
    size_t pos = 0;
    while (pos + 1 < line_size && !miner_should_stop()) {
        int ready = wait_socket(fd, POLLIN, 1000);
        if (ready < 0) {
            return 0;
        }
        if (ready == 0) {
            continue;
        }

        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return 0;
        }
        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            line[pos++] = c;
        }
    }
    line[pos] = '\0';
    return pos > 0;
}

static int sleep_with_stop(int seconds) {
    for (int i = 0; i < seconds; ++i) {
        if (miner_should_stop()) {
            return 0;
        }
        sleep(1);
    }
    return 1;
}

static void handle_stratum_line(const char *line) {
    if (strstr(line, "\"id\":1") != NULL || strstr(line, "\"id\": 1") != NULL) {
        set_stratum_state("subscribed", NULL, 1);
    } else if (strstr(line, "\"id\":2") != NULL || strstr(line, "\"id\": 2") != NULL) {
        if (strstr(line, "\"result\":true") != NULL || strstr(line, "\"result\": true") != NULL) {
            set_stratum_state("authorized", "", 1);
        } else {
            set_stratum_state("authorize failed", "authorize failed", 1);
        }
    } else if (strstr(line, "\"method\":\"mining.notify\"") != NULL
            || strstr(line, "\"method\": \"mining.notify\"") != NULL) {
        add_stratum_job();
    }
}

static void *stratum_worker(void *opaque) {
    (void)opaque;
    char pool[256];
    char user[128];
    char pass[128];

    pthread_mutex_lock(&g_lock);
    copy_text(pool, sizeof(pool), g_pool);
    copy_text(user, sizeof(user), g_user);
    copy_text(pass, sizeof(pass), g_pass);
    pthread_mutex_unlock(&g_lock);

    if (pool[0] == '\0') {
        set_stratum_state("no pool configured", "", 0);
        return NULL;
    }

    char host[192];
    char port[16];
    if (!parse_pool_url(pool, host, sizeof(host), port, sizeof(port))) {
        set_stratum_state("bad pool url", "bad pool url", 0);
        return NULL;
    }

    while (!miner_should_stop()) {
        set_stratum_state("connecting", "", 0);
        int fd = connect_tcp(host, port);
        if (fd < 0) {
            set_stratum_state("connect failed", "connect failed", 0);
            if (!sleep_with_stop(5)) {
                break;
            }
            continue;
        }

        set_stratum_state("connected", "", 1);
        char request[512];
        snprintf(request, sizeof(request),
                "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"BTCRig-Android/0.1.0\"]}\n");
        if (!socket_send_all(fd, request)) {
            close(fd);
            set_stratum_state("send failed", "send failed", 0);
            continue;
        }

        snprintf(request, sizeof(request),
                "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
                user,
                pass);
        if (!socket_send_all(fd, request)) {
            close(fd);
            set_stratum_state("send failed", "send failed", 0);
            continue;
        }

        char line[4096];
        while (!miner_should_stop() && socket_read_line(fd, line, sizeof(line))) {
            handle_stratum_line(line);
        }
        close(fd);
        if (!miner_should_stop()) {
            set_stratum_state("disconnected", "disconnected", 0);
            sleep_with_stop(5);
        }
    }

    set_stratum_state("stopped", "", 0);
    return NULL;
}

const char *btcrig_core_backend_name(void) {
    return "fast-c";
}

int btcrig_core_self_test(void) {
    static const uint8_t expected[32] = {
        0x4b, 0xe7, 0x57, 0x0e, 0x8f, 0x70, 0xeb, 0x09,
        0x36, 0x40, 0xc8, 0x46, 0x82, 0x74, 0xba, 0x75,
        0x97, 0x45, 0xa7, 0xaa, 0x2b, 0x7d, 0x25, 0xab,
        0x1e, 0x04, 0x21, 0xb2, 0x59, 0x84, 0x50, 0x14,
    };
    uint8_t header[80] = {0};
    uint8_t out[32];
    sha256d_80(header, out);
    return memcmp(out, expected, sizeof(expected)) == 0;
}

int btcrig_core_start(const char *config_path) {
    core_config_t config = read_config(config_path);
    int threads = clamp_threads(config.cpu_threads);
    miner_worker_t *workers = calloc((size_t)threads, sizeof(*workers));
    if (workers == NULL) {
        return 0;
    }

    pthread_mutex_lock(&g_lock);
    if (g_running) {
        pthread_mutex_unlock(&g_lock);
        free(workers);
        return 1;
    }
    g_workers = workers;
    g_worker_count = threads;
    g_total_hashes = 0;
    g_sink = 0;
    g_started_at = monotonic_seconds();
    g_stop_requested = 0;
    g_stratum_connected = 0;
    g_stratum_jobs = 0;
    copy_text(g_pool, sizeof(g_pool), config.pool);
    copy_text(g_user, sizeof(g_user), config.user);
    copy_text(g_pass, sizeof(g_pass), config.pass);
    copy_text(g_stratum_status, sizeof(g_stratum_status), "starting");
    copy_text(g_last_error, sizeof(g_last_error), "");
    g_running = 1;
    pthread_mutex_unlock(&g_lock);

    int started = 0;
    for (int i = 0; i < threads; ++i) {
        workers[i].seed = (uint32_t)(0x9e3779b9U * (uint32_t)(i + 1));
        if (pthread_create(&workers[i].id, NULL, miner_worker, &workers[i]) != 0) {
            break;
        }
        ++started;
    }

    if (pthread_create(&g_stratum_thread, NULL, stratum_worker, NULL) == 0) {
        pthread_mutex_lock(&g_lock);
        g_stratum_started = 1;
        pthread_mutex_unlock(&g_lock);
    } else {
        set_stratum_state("stratum unavailable", "stratum thread failed", 0);
    }

    if (started == threads) {
        return 1;
    }

    pthread_mutex_lock(&g_lock);
    g_stop_requested = 1;
    pthread_mutex_unlock(&g_lock);
    for (int i = 0; i < started; ++i) {
        pthread_join(workers[i].id, NULL);
    }
    pthread_mutex_lock(&g_lock);
    int stratum_started = g_stratum_started;
    pthread_mutex_unlock(&g_lock);
    if (stratum_started) {
        pthread_join(g_stratum_thread, NULL);
    }

    pthread_mutex_lock(&g_lock);
    g_workers = NULL;
    g_worker_count = 0;
    g_stratum_started = 0;
    g_running = 0;
    g_stop_requested = 0;
    pthread_mutex_unlock(&g_lock);
    free(workers);
    return 0;
}

void btcrig_core_stop(void) {
    pthread_mutex_lock(&g_lock);
    if (!g_running || g_stop_requested) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    g_stop_requested = 1;
    miner_worker_t *workers = g_workers;
    int worker_count = g_worker_count;
    int stratum_started = g_stratum_started;
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < worker_count; ++i) {
        pthread_join(workers[i].id, NULL);
    }
    if (stratum_started) {
        pthread_join(g_stratum_thread, NULL);
    }

    pthread_mutex_lock(&g_lock);
    free(g_workers);
    g_workers = NULL;
    g_worker_count = 0;
    g_stratum_started = 0;
    g_stratum_connected = 0;
    g_running = 0;
    g_stop_requested = 0;
    copy_text(g_stratum_status, sizeof(g_stratum_status), "stopped");
    pthread_mutex_unlock(&g_lock);
}

int btcrig_core_is_running(void) {
    pthread_mutex_lock(&g_lock);
    int running = g_running;
    pthread_mutex_unlock(&g_lock);
    return running;
}

int btcrig_core_worker_count(void) {
    pthread_mutex_lock(&g_lock);
    int workers = g_worker_count;
    pthread_mutex_unlock(&g_lock);
    return workers;
}

uint64_t btcrig_core_total_hashes(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t hashes = g_total_hashes;
    pthread_mutex_unlock(&g_lock);
    return hashes;
}

double btcrig_core_hashrate(void) {
    pthread_mutex_lock(&g_lock);
    int running = g_running;
    uint64_t hashes = g_total_hashes;
    double started_at = g_started_at;
    pthread_mutex_unlock(&g_lock);

    double elapsed = monotonic_seconds() - started_at;
    if (!running || elapsed <= 0.0) {
        return 0.0;
    }
    return (double)hashes / elapsed;
}

int btcrig_core_stratum_connected(void) {
    pthread_mutex_lock(&g_lock);
    int connected = g_stratum_connected;
    pthread_mutex_unlock(&g_lock);
    return connected;
}

uint64_t btcrig_core_stratum_jobs(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t jobs = g_stratum_jobs;
    pthread_mutex_unlock(&g_lock);
    return jobs;
}

void btcrig_core_copy_pool(char *out, size_t out_size) {
    pthread_mutex_lock(&g_lock);
    copy_text(out, out_size, g_pool);
    pthread_mutex_unlock(&g_lock);
}

void btcrig_core_copy_stratum_status(char *out, size_t out_size) {
    pthread_mutex_lock(&g_lock);
    copy_text(out, out_size, g_stratum_status);
    pthread_mutex_unlock(&g_lock);
}

void btcrig_core_copy_last_error(char *out, size_t out_size) {
    pthread_mutex_lock(&g_lock);
    copy_text(out, out_size, g_last_error);
    pthread_mutex_unlock(&g_lock);
}

double btcrig_core_benchmark_cpu(int seconds, int threads) {
    if (seconds < 1) {
        seconds = 1;
    } else if (seconds > 60) {
        seconds = 60;
    }
    if (threads < 1) {
        threads = 1;
    } else if (threads > 256) {
        threads = 256;
    }

    bench_worker_t *workers = calloc((size_t)threads, sizeof(*workers));
    pthread_t *ids = calloc((size_t)threads, sizeof(*ids));
    if (workers == NULL || ids == NULL) {
        free(workers);
        free(ids);
        return 0.0;
    }

    double start = monotonic_seconds();
    double stop = start + (double)seconds;
    int started = 0;
    for (int i = 0; i < threads; ++i) {
        workers[i].stop_at = stop;
        workers[i].seed = (uint32_t)(0x9e3779b9U * (uint32_t)(i + 1));
        if (pthread_create(&ids[i], NULL, bench_worker, &workers[i]) != 0) {
            break;
        }
        ++started;
    }

    uint64_t hashes = 0;
    uint8_t sink = 0;
    for (int i = 0; i < started; ++i) {
        pthread_join(ids[i], NULL);
        hashes += workers[i].hashes;
        sink ^= workers[i].sink;
    }
    double end = monotonic_seconds();
    free(workers);
    free(ids);

    if (started == 0 || end <= start) {
        return 0.0;
    }
    return ((double)hashes + (double)(sink & 1U)) / (end - start);
}
