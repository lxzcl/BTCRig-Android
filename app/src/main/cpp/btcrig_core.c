#include "btcrig_core.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_running = 0;
static int g_stop_requested = 0;
static miner_worker_t *g_workers = NULL;
static int g_worker_count = 0;
static uint64_t g_total_hashes = 0;
static uint8_t g_sink = 0;
static double g_started_at = 0.0;

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

static int read_cpu_threads(const char *config_path) {
    if (config_path == NULL || config_path[0] == '\0') {
        return 0;
    }

    FILE *file = fopen(config_path, "rb");
    if (file == NULL) {
        return 0;
    }

    char text[4097];
    size_t n = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[n] = '\0';

    const char *key = strstr(text, "\"cpu_threads\"");
    if (key == NULL) {
        return 0;
    }
    const char *colon = strchr(key, ':');
    if (colon == NULL) {
        return 0;
    }

    char *end = NULL;
    long value = strtol(colon + 1, &end, 10);
    if (end == colon + 1 || value < 0) {
        return 0;
    }
    if (value > 256) {
        return 256;
    }
    return (int)value;
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
    int threads = clamp_threads(read_cpu_threads(config_path));
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
    g_workers = NULL;
    g_worker_count = 0;
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
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < worker_count; ++i) {
        pthread_join(workers[i].id, NULL);
    }

    pthread_mutex_lock(&g_lock);
    free(g_workers);
    g_workers = NULL;
    g_worker_count = 0;
    g_running = 0;
    g_stop_requested = 0;
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
