#include "btcrig_core.h"

#include <ctype.h>
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

#define STRATUM_MAX_MERKLE_BRANCHES 64
#define STRATUM_MAX_COINBASE_HEX 8192
#define SHARE_QUEUE_SIZE 64
#define MINER_BATCH_SIZE 4096

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

typedef struct {
    uint64_t seq;
    char job_id[128];
    char extranonce2[32];
    char ntime[16];
    uint8_t header[80];
    uint8_t target[32];
    int valid;
} miner_job_t;

typedef struct {
    uint64_t seq;
    uint32_t nonce;
    char job_id[128];
    char extranonce2[32];
    char ntime[16];
    uint8_t hash[32];
} miner_share_t;

typedef struct {
    char job_id[128];
    char prevhash[65];
    char coinb1[STRATUM_MAX_COINBASE_HEX];
    char coinb2[STRATUM_MAX_COINBASE_HEX];
    char version[9];
    char nbits[9];
    char ntime[9];
    char merkle[STRATUM_MAX_MERKLE_BRANCHES][65];
    int merkle_count;
    int valid;
} stratum_template_t;

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
static uint64_t g_stratum_submits = 0;
static uint64_t g_stratum_accepts = 0;
static uint64_t g_stratum_rejects = 0;
static char g_pool[256] = "";
static char g_user[128] = "";
static char g_pass[128] = "";
static char g_stratum_status[64] = "idle";
static char g_last_error[128] = "";
static char g_extranonce1[128] = "";
static int g_extranonce2_size = 4;
static uint64_t g_extranonce2_counter = 1;
static double g_difficulty = 1.0;
static uint64_t g_job_seq = 0;
static uint32_t g_next_nonce = 0;
static miner_job_t g_job;
static stratum_template_t g_template;
static miner_share_t g_shares[SHARE_QUEUE_SIZE];
static int g_share_head = 0;
static int g_share_tail = 0;
static int g_share_count = 0;

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

static void sha256_data(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t state[8];
    memcpy(state, k_sha256_initial_state, sizeof(state));
    const uint8_t *p = data;
    size_t left = len;
    while (left >= 64) {
        sha256_compress_block(state, p);
        p += 64;
        left -= 64;
    }
    sha256_finish(state, p, left, len, out);
}

static void sha256d_data(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint8_t first[32];
    sha256_data(data, len, first);
    sha256_32(first, out);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    size_t hex_len = strlen(hex);
    if ((hex_len & 1U) != 0 || hex_len / 2 != out_len) {
        return -1;
    }
    for (size_t i = 0; i < out_len; ++i) {
        int hi = hex_value(hex[i * 2]);
        int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int hex_to_alloc(const char *hex, uint8_t **out, size_t *out_len) {
    size_t len = strlen(hex);
    if ((len & 1U) != 0) {
        return -1;
    }
    len /= 2;
    uint8_t *buf = malloc(len == 0 ? 1 : len);
    if (buf == NULL) {
        return -1;
    }
    if (hex_to_bytes(hex, buf, len) != 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = len;
    return 0;
}

static int copy_checked(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if (len >= dst_size) {
        return -1;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

static int hex_u32_to_le(const char *hex, uint8_t out[4]) {
    uint8_t tmp[4];
    if (hex_to_bytes(hex, tmp, sizeof(tmp)) != 0) {
        return -1;
    }
    out[0] = tmp[3];
    out[1] = tmp[2];
    out[2] = tmp[1];
    out[3] = tmp[0];
    return 0;
}

static int prevhash_to_header_bytes(const char *hex, uint8_t out[32]) {
    if (strlen(hex) != 64) {
        return -1;
    }
    for (int i = 0; i < 32; i += 4) {
        uint8_t tmp[4];
        char chunk[9];
        memcpy(chunk, hex + i * 2, 8);
        chunk[8] = '\0';
        if (hex_to_bytes(chunk, tmp, sizeof(tmp)) != 0) {
            return -1;
        }
        out[i + 0] = tmp[3];
        out[i + 1] = tmp[2];
        out[i + 2] = tmp[1];
        out[i + 3] = tmp[0];
    }
    return 0;
}

static void target_from_difficulty(double difficulty, uint8_t target[32]) {
    uint8_t be_target[32];
    memset(be_target, 0, sizeof(be_target));
    memset(target, 0, 32);
    if (difficulty <= 0.0) {
        difficulty = 1.0;
    }
    double mant = 65535.0 / difficulty;
    int exp = 29;
    while (mant >= 0x800000 && exp < 32) {
        mant /= 256.0;
        ++exp;
    }
    while (mant > 0.0 && mant < 0x8000 && exp > 3) {
        mant *= 256.0;
        --exp;
    }
    uint32_t m = (uint32_t)mant;
    int idx = 32 - exp;
    if (idx >= 0 && idx + 2 < 32) {
        be_target[idx] = (uint8_t)(m >> 16);
        be_target[idx + 1] = (uint8_t)(m >> 8);
        be_target[idx + 2] = (uint8_t)m;
        for (int i = 0; i < 32; ++i) {
            target[i] = be_target[31 - i];
        }
    } else if (idx < 0) {
        memset(target, 0xff, 32);
    }
}

static int hash_meets_target(const uint8_t hash[32], const uint8_t target[32]) {
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target[i]) {
            return 1;
        }
        if (hash[i] > target[i]) {
            return 0;
        }
    }
    return 1;
}

static void format_extranonce2(char *out, size_t out_size, int extranonce2_size, uint64_t value) {
    int chars = extranonce2_size * 2;
    if (chars <= 0 || (size_t)chars + 1 > out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return;
    }
    snprintf(out, out_size, "%0*llx", chars, (unsigned long long)value);
    if ((int)strlen(out) != chars) {
        out[0] = '\0';
    }
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

static int build_job(miner_job_t *out,
                     const stratum_template_t *tpl,
                     const char *extranonce1,
                     const char *extranonce2,
                     double difficulty,
                     uint64_t seq) {
    char *coinbase_hex = NULL;
    uint8_t *coinbase = NULL;
    size_t coinbase_len = 0;
    uint8_t merkle_root[32];
    int rc = -1;

    memset(out, 0, sizeof(*out));
    if (copy_checked(out->job_id, sizeof(out->job_id), tpl->job_id) != 0 ||
            copy_checked(out->extranonce2, sizeof(out->extranonce2), extranonce2) != 0 ||
            copy_checked(out->ntime, sizeof(out->ntime), tpl->ntime) != 0) {
        return -1;
    }

    size_t coinbase_hex_len = strlen(tpl->coinb1) + strlen(extranonce1) + strlen(extranonce2) + strlen(tpl->coinb2);
    coinbase_hex = malloc(coinbase_hex_len + 1);
    if (coinbase_hex == NULL) {
        return -1;
    }
    snprintf(coinbase_hex, coinbase_hex_len + 1, "%s%s%s%s", tpl->coinb1, extranonce1, extranonce2, tpl->coinb2);
    if (hex_to_alloc(coinbase_hex, &coinbase, &coinbase_len) != 0) {
        goto done;
    }
    sha256d_data(coinbase, coinbase_len, merkle_root);

    for (int i = 0; i < tpl->merkle_count; ++i) {
        uint8_t branch[32];
        uint8_t combined[64];
        if (hex_to_bytes(tpl->merkle[i], branch, sizeof(branch)) != 0) {
            goto done;
        }
        memcpy(combined, merkle_root, 32);
        memcpy(combined + 32, branch, 32);
        sha256d_data(combined, sizeof(combined), merkle_root);
    }

    uint8_t *p = out->header;
    if (hex_u32_to_le(tpl->version, p) != 0) {
        goto done;
    }
    p += 4;
    if (prevhash_to_header_bytes(tpl->prevhash, p) != 0) {
        goto done;
    }
    p += 32;
    memcpy(p, merkle_root, 32);
    p += 32;
    if (hex_u32_to_le(tpl->ntime, p) != 0) {
        goto done;
    }
    p += 4;
    if (hex_u32_to_le(tpl->nbits, p) != 0) {
        goto done;
    }
    p += 4;
    memset(p, 0, 4);

    target_from_difficulty(difficulty, out->target);
    out->seq = seq;
    out->valid = 1;
    rc = 0;

done:
    free(coinbase);
    free(coinbase_hex);
    return rc;
}

static int rebuild_job_locked(const char *status) {
    if (!g_template.valid || g_extranonce1[0] == '\0' || g_extranonce2_size <= 0) {
        return 0;
    }

    char extranonce2[32];
    format_extranonce2(extranonce2, sizeof(extranonce2), g_extranonce2_size, g_extranonce2_counter++);
    if (extranonce2[0] == '\0') {
        g_job.valid = 0;
        copy_text(g_last_error, sizeof(g_last_error), "bad extranonce2");
        return 0;
    }

    miner_job_t job;
    if (build_job(&job, &g_template, g_extranonce1, extranonce2, g_difficulty, ++g_job_seq) != 0) {
        g_job.valid = 0;
        copy_text(g_last_error, sizeof(g_last_error), "bad mining job");
        return 0;
    }

    g_job = job;
    g_next_nonce = 0;
    g_share_head = 0;
    g_share_tail = 0;
    g_share_count = 0;
    if (status != NULL) {
        copy_text(g_stratum_status, sizeof(g_stratum_status), status);
    }
    return 1;
}

static int copy_active_job(miner_job_t *out, uint32_t *nonce) {
    pthread_mutex_lock(&g_lock);
    int valid = g_job.valid;
    if (valid) {
        *out = g_job;
        *nonce = g_next_nonce;
        g_next_nonce += MINER_BATCH_SIZE;
    }
    pthread_mutex_unlock(&g_lock);
    return valid;
}

static void queue_share(const miner_job_t *job, uint32_t nonce, const uint8_t hash[32]) {
    pthread_mutex_lock(&g_lock);
    // ponytail: one global share queue is enough for this Android shell; split per backend if contention shows up.
    if (g_share_count < SHARE_QUEUE_SIZE) {
        miner_share_t *share = &g_shares[g_share_tail];
        memset(share, 0, sizeof(*share));
        share->seq = job->seq;
        share->nonce = nonce;
        memcpy(share->hash, hash, 32);
        copy_checked(share->job_id, sizeof(share->job_id), job->job_id);
        copy_checked(share->extranonce2, sizeof(share->extranonce2), job->extranonce2);
        copy_checked(share->ntime, sizeof(share->ntime), job->ntime);
        g_share_tail = (g_share_tail + 1) % SHARE_QUEUE_SIZE;
        ++g_share_count;
    }
    pthread_mutex_unlock(&g_lock);
}

static int pop_share(miner_share_t *out) {
    pthread_mutex_lock(&g_lock);
    int ok = g_share_count > 0;
    if (ok) {
        *out = g_shares[g_share_head];
        g_share_head = (g_share_head + 1) % SHARE_QUEUE_SIZE;
        --g_share_count;
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
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

    while (!miner_should_stop()) {
        miner_job_t job;
        if (copy_active_job(&job, &nonce)) {
            memcpy(header, job.header, sizeof(header));
            for (int i = 0; i < MINER_BATCH_SIZE; ++i) {
                uint32_t share_nonce = nonce++;
                put_le32(header + 76, share_nonce);
                sha256d_80(header, out);
                sink ^= out[0];
                if (hash_meets_target(out, job.target)) {
                    queue_share(&job, share_nonce, out);
                }
            }
        } else {
            for (int i = 0; i < 80; ++i) {
                header[i] = (uint8_t)(i + nonce);
            }
            for (int i = 0; i < MINER_BATCH_SIZE; ++i) {
                put_le32(header + 76, nonce++);
                sha256d_80(header, out);
                sink ^= out[0];
            }
        }
        hashes += MINER_BATCH_SIZE;

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
    if (!connected) {
        g_job.valid = 0;
        g_share_head = 0;
        g_share_tail = 0;
        g_share_count = 0;
    }
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

static int socket_read_available(int fd, char *buffer, size_t *used, size_t buffer_size) {
    if (*used + 1 >= buffer_size) {
        return -1;
    }

    struct pollfd poll_fd;
    memset(&poll_fd, 0, sizeof(poll_fd));
    poll_fd.fd = fd;
    poll_fd.events = POLLIN;

    int ready = poll(&poll_fd, 1, 250);
    if (ready == 0) {
        return 0;
    }
    if (ready < 0) {
        return errno == EINTR ? 0 : -1;
    }
    if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return -1;
    }
    if ((poll_fd.revents & POLLIN) == 0) {
        return 0;
    }

    ssize_t n = recv(fd, buffer + *used, buffer_size - *used - 1, 0);
    if (n == 0) {
        return -1;
    }
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    *used += (size_t)n;
    buffer[*used] = '\0';
    return 1;
}

static int send_submit_share(int fd, int id, const char *user, const miner_share_t *share) {
    char nonce_hex[9];
    char request[768];
    snprintf(nonce_hex, sizeof(nonce_hex), "%08x", share->nonce);
    int n = snprintf(request, sizeof(request),
            "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            id,
            user,
            share->job_id,
            share->extranonce2,
            share->ntime,
            nonce_hex);
    if (n < 0 || (size_t)n >= sizeof(request)) {
        return 0;
    }
    return socket_send_all(fd, request);
}

static int flush_shares(int fd, const char *user, int *next_rpc_id) {
    miner_share_t share;
    while (pop_share(&share)) {
        int id = (*next_rpc_id)++;
        if (!send_submit_share(fd, id, user, &share)) {
            return 0;
        }
        pthread_mutex_lock(&g_lock);
        ++g_stratum_submits;
        if (g_stratum_accepts == 0 && g_stratum_rejects == 0) {
            copy_text(g_stratum_status, sizeof(g_stratum_status), "share submitted");
        }
        pthread_mutex_unlock(&g_lock);
    }
    return 1;
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

static const char *skip_json_ws(const char *p) {
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static const char *skip_json_string(const char *p) {
    if (*p != '"') {
        return NULL;
    }
    ++p;
    while (*p != '\0') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        ++p;
    }
    return NULL;
}

static const char *skip_json_value(const char *p) {
    p = skip_json_ws(p);
    if (*p == '"') {
        return skip_json_string(p);
    }
    if (*p == '[') {
        ++p;
        p = skip_json_ws(p);
        if (*p == ']') {
            return p + 1;
        }
        while (*p != '\0') {
            p = skip_json_value(p);
            if (p == NULL) {
                return NULL;
            }
            p = skip_json_ws(p);
            if (*p == ',') {
                ++p;
                continue;
            }
            return *p == ']' ? p + 1 : NULL;
        }
        return NULL;
    }
    if (*p == '{') {
        ++p;
        p = skip_json_ws(p);
        if (*p == '}') {
            return p + 1;
        }
        while (*p != '\0') {
            p = skip_json_string(skip_json_ws(p));
            if (p == NULL) {
                return NULL;
            }
            p = skip_json_ws(p);
            if (*p++ != ':') {
                return NULL;
            }
            p = skip_json_value(p);
            if (p == NULL) {
                return NULL;
            }
            p = skip_json_ws(p);
            if (*p == ',') {
                ++p;
                continue;
            }
            return *p == '}' ? p + 1 : NULL;
        }
        return NULL;
    }
    while (*p != '\0' && *p != ',' && *p != ']' && *p != '}') {
        ++p;
    }
    return p;
}

static int read_json_string_token(const char *p, char *out, size_t out_size) {
    p = skip_json_ws(p);
    if (out_size == 0 || *p != '"') {
        return 0;
    }
    ++p;
    size_t i = 0;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            ++p;
        }
        if (i + 1 >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[i++] = *p++;
    }
    if (*p != '"') {
        out[0] = '\0';
        return 0;
    }
    out[i] = '\0';
    return 1;
}

static const char *json_array_value(const char *line, const char *key) {
    const char *value = find_json_value(line, key);
    if (value == NULL) {
        return NULL;
    }
    value = skip_json_ws(value);
    return *value == '[' ? value : NULL;
}

static const char *json_array_item(const char *array, int index) {
    const char *p = skip_json_ws(array);
    if (*p++ != '[') {
        return NULL;
    }
    for (int i = 0; *p != '\0'; ++i) {
        p = skip_json_ws(p);
        if (*p == ']') {
            return NULL;
        }
        if (i == index) {
            return p;
        }
        p = skip_json_value(p);
        if (p == NULL) {
            return NULL;
        }
        p = skip_json_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        return NULL;
    }
    return NULL;
}

static int json_array_string_at(const char *line, const char *key, int index, char *out, size_t out_size) {
    const char *array = json_array_value(line, key);
    const char *item = array == NULL ? NULL : json_array_item(array, index);
    return item != NULL && read_json_string_token(item, out, out_size);
}

static int json_array_int_at(const char *line, const char *key, int index, int *out) {
    const char *array = json_array_value(line, key);
    const char *item = array == NULL ? NULL : json_array_item(array, index);
    if (item == NULL) {
        return 0;
    }
    const char *start = skip_json_ws(item);
    char *end = NULL;
    long value = strtol(start, &end, 10);
    if (end == start) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

static int json_array_double_at(const char *line, const char *key, int index, double *out) {
    const char *array = json_array_value(line, key);
    const char *item = array == NULL ? NULL : json_array_item(array, index);
    if (item == NULL) {
        return 0;
    }
    const char *start = skip_json_ws(item);
    char *end = NULL;
    double value = strtod(start, &end);
    if (end == start) {
        return 0;
    }
    *out = value;
    return 1;
}

static int json_string_array_at(const char *line,
                                const char *key,
                                int index,
                                char out[][65],
                                int max_count,
                                int *count) {
    const char *array = json_array_value(line, key);
    const char *item = array == NULL ? NULL : json_array_item(array, index);
    const char *p = item == NULL ? NULL : skip_json_ws(item);
    if (p == NULL || *p++ != '[') {
        return 0;
    }
    *count = 0;
    while (*p != '\0') {
        p = skip_json_ws(p);
        if (*p == ']') {
            return 1;
        }
        if (*count < max_count && !read_json_string_token(p, out[*count], 65)) {
            return 0;
        }
        if (*count < max_count) {
            ++*count;
        }
        p = skip_json_value(p);
        if (p == NULL) {
            return 0;
        }
        p = skip_json_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        return *p == ']';
    }
    return 0;
}

static int json_id(const char *line) {
    const char *value = find_json_value(line, "id");
    if (value == NULL) {
        return -1;
    }
    value = skip_json_ws(value);
    if (strncmp(value, "null", 4) == 0) {
        return -1;
    }
    char *end = NULL;
    long id = strtol(value, &end, 10);
    return end == value ? -1 : (int)id;
}

static int json_method_is(const char *line, const char *method) {
    char value[64];
    const char *p = find_json_value(line, "method");
    return p != NULL && read_json_string_token(p, value, sizeof(value)) && strcmp(value, method) == 0;
}

static int json_result_is(const char *line, const char *value) {
    const char *p = find_json_value(line, "result");
    if (p == NULL) {
        return 0;
    }
    p = skip_json_ws(p);
    return strncmp(p, value, strlen(value)) == 0;
}

static int json_error_present(const char *line) {
    const char *p = find_json_value(line, "error");
    if (p == NULL) {
        return 0;
    }
    p = skip_json_ws(p);
    return strncmp(p, "null", 4) != 0;
}

static void handle_subscribe_line(const char *line) {
    char extranonce1[128];
    int extranonce2_size = 0;
    if (!json_array_string_at(line, "result", 1, extranonce1, sizeof(extranonce1)) ||
            !json_array_int_at(line, "result", 2, &extranonce2_size)) {
        set_stratum_state("bad subscribe", "bad subscribe", 1);
        return;
    }
    pthread_mutex_lock(&g_lock);
    copy_text(g_extranonce1, sizeof(g_extranonce1), extranonce1);
    g_extranonce2_size = extranonce2_size;
    copy_text(g_stratum_status, sizeof(g_stratum_status), "subscribed");
    copy_text(g_last_error, sizeof(g_last_error), "");
    rebuild_job_locked("mining");
    pthread_mutex_unlock(&g_lock);
}

static void handle_authorize_line(const char *line) {
    if (json_result_is(line, "true")) {
        set_stratum_state("authorized", "", 1);
    } else {
        set_stratum_state("authorize failed", "authorize failed", 1);
    }
}

static void handle_set_difficulty_line(const char *line) {
    double difficulty = 0.0;
    if (!json_array_double_at(line, "params", 0, &difficulty)) {
        set_stratum_state("bad difficulty", "bad difficulty", 1);
        return;
    }
    pthread_mutex_lock(&g_lock);
    g_difficulty = difficulty;
    if (!rebuild_job_locked("difficulty updated")) {
        copy_text(g_stratum_status, sizeof(g_stratum_status), "difficulty set");
    }
    pthread_mutex_unlock(&g_lock);
}

static void handle_set_extranonce_line(const char *line) {
    char extranonce1[128];
    int extranonce2_size = 0;
    if (!json_array_string_at(line, "params", 0, extranonce1, sizeof(extranonce1)) ||
            !json_array_int_at(line, "params", 1, &extranonce2_size)) {
        set_stratum_state("bad extranonce", "bad extranonce", 1);
        return;
    }
    pthread_mutex_lock(&g_lock);
    copy_text(g_extranonce1, sizeof(g_extranonce1), extranonce1);
    g_extranonce2_size = extranonce2_size;
    rebuild_job_locked("extranonce updated");
    pthread_mutex_unlock(&g_lock);
}

static void handle_notify_line(const char *line) {
    stratum_template_t tpl;
    memset(&tpl, 0, sizeof(tpl));

    if (!json_array_string_at(line, "params", 0, tpl.job_id, sizeof(tpl.job_id)) ||
            !json_array_string_at(line, "params", 1, tpl.prevhash, sizeof(tpl.prevhash)) ||
            !json_array_string_at(line, "params", 2, tpl.coinb1, sizeof(tpl.coinb1)) ||
            !json_array_string_at(line, "params", 3, tpl.coinb2, sizeof(tpl.coinb2)) ||
            !json_string_array_at(line, "params", 4, tpl.merkle, STRATUM_MAX_MERKLE_BRANCHES, &tpl.merkle_count) ||
            !json_array_string_at(line, "params", 5, tpl.version, sizeof(tpl.version)) ||
            !json_array_string_at(line, "params", 6, tpl.nbits, sizeof(tpl.nbits)) ||
            !json_array_string_at(line, "params", 7, tpl.ntime, sizeof(tpl.ntime))) {
        set_stratum_state("bad notify", "bad notify", 1);
        return;
    }
    tpl.valid = 1;

    pthread_mutex_lock(&g_lock);
    g_template = tpl;
    ++g_stratum_jobs;
    copy_text(g_stratum_status, sizeof(g_stratum_status), "job received");
    copy_text(g_last_error, sizeof(g_last_error), "");
    rebuild_job_locked("mining");
    pthread_mutex_unlock(&g_lock);
}

static void handle_submit_response_line(const char *line) {
    int accepted = !json_error_present(line) && json_result_is(line, "true");
    pthread_mutex_lock(&g_lock);
    if (accepted) {
        ++g_stratum_accepts;
        copy_text(g_stratum_status, sizeof(g_stratum_status), "share accepted");
        copy_text(g_last_error, sizeof(g_last_error), "");
    } else {
        ++g_stratum_rejects;
        copy_text(g_stratum_status, sizeof(g_stratum_status), "share rejected");
        copy_text(g_last_error, sizeof(g_last_error), "share rejected");
    }
    pthread_mutex_unlock(&g_lock);
}

static void handle_stratum_line(const char *line) {
    int id = json_id(line);
    if (id == 1) {
        handle_subscribe_line(line);
    } else if (id == 2) {
        handle_authorize_line(line);
    } else if (id >= 4) {
        handle_submit_response_line(line);
    } else if (json_method_is(line, "mining.set_difficulty")) {
        handle_set_difficulty_line(line);
    } else if (json_method_is(line, "mining.notify")) {
        handle_notify_line(line);
    } else if (json_method_is(line, "mining.set_extranonce")) {
        handle_set_extranonce_line(line);
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

        int next_rpc_id = 4;
        char input[8192];
        size_t used = 0;
        input[0] = '\0';
        while (!miner_should_stop()) {
            if (!flush_shares(fd, user, &next_rpc_id)) {
                set_stratum_state("submit failed", "submit failed", 0);
                break;
            }

            int read_rc = socket_read_available(fd, input, &used, sizeof(input));
            if (read_rc < 0) {
                break;
            }
            if (read_rc == 0) {
                continue;
            }

            char *start = input;
            for (;;) {
                char *newline = memchr(start, '\n', used - (size_t)(start - input));
                if (newline == NULL) {
                    break;
                }
                *newline = '\0';
                if (newline > start && newline[-1] == '\r') {
                    newline[-1] = '\0';
                }
                if (*start != '\0') {
                    handle_stratum_line(start);
                }
                start = newline + 1;
            }
            size_t left = used - (size_t)(start - input);
            memmove(input, start, left);
            used = left;
            input[used] = '\0';
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
    if (memcmp(out, expected, sizeof(expected)) != 0) {
        return 0;
    }

    stratum_template_t tpl;
    memset(&tpl, 0, sizeof(tpl));
    copy_checked(tpl.job_id, sizeof(tpl.job_id), "job1");
    copy_checked(tpl.prevhash, sizeof(tpl.prevhash), "0000000000000000000000000000000000000000000000000000000000000000");
    copy_checked(tpl.coinb1, sizeof(tpl.coinb1), "0200000001");
    copy_checked(tpl.coinb2, sizeof(tpl.coinb2), "");
    copy_checked(tpl.version, sizeof(tpl.version), "20000000");
    copy_checked(tpl.nbits, sizeof(tpl.nbits), "170fffff");
    copy_checked(tpl.ntime, sizeof(tpl.ntime), "665ee001");
    miner_job_t job;
    if (build_job(&job, &tpl, "abcd1234", "00000001", 0.000000001, 1) != 0) {
        return 0;
    }
    memcpy(header, job.header, sizeof(header));
    put_le32(header + 76, 5);
    sha256d_80(header, out);
    return hash_meets_target(out, job.target);
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
    g_stratum_submits = 0;
    g_stratum_accepts = 0;
    g_stratum_rejects = 0;
    g_extranonce1[0] = '\0';
    g_extranonce2_size = 4;
    g_extranonce2_counter = 1;
    g_difficulty = 1.0;
    g_job_seq = 0;
    g_next_nonce = 0;
    memset(&g_job, 0, sizeof(g_job));
    memset(&g_template, 0, sizeof(g_template));
    g_share_head = 0;
    g_share_tail = 0;
    g_share_count = 0;
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

uint64_t btcrig_core_stratum_submits(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t submits = g_stratum_submits;
    pthread_mutex_unlock(&g_lock);
    return submits;
}

uint64_t btcrig_core_stratum_accepts(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t accepts = g_stratum_accepts;
    pthread_mutex_unlock(&g_lock);
    return accepts;
}

uint64_t btcrig_core_stratum_rejects(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t rejects = g_stratum_rejects;
    pthread_mutex_unlock(&g_lock);
    return rejects;
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
