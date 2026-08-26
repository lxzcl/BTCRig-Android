#define _GNU_SOURCE

#include "miner.h"

#include "console.h"
#include "cpu_info.h"
#include "sha256d.h"

#if defined(BTC_MINER_OPENCL)
#include "opencl_miner.h"
#endif
#if defined(BTC_MINER_CUDA)
#include "cuda_miner.h"
#endif

#include <errno.h>
#include <math.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MINER_BATCH_SIZE 262144U
#define MINER_MIXED_CPU_BATCH_SIZE 65536U
#define SHARE_QUEUE_SIZE 256

#if defined(BTC_MINER_OPENCL) || defined(BTC_MINER_CUDA)
static void miner_sleep_ms(unsigned int ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000U);
#endif
}
#endif

typedef struct {
    miner_job_t public_job;
    sha256_midstate_t midstate;
    uint32_t tail_words[4];
    uint32_t target_words[8];
    uint64_t pause_epoch;
    int valid;
} active_job_t;

struct miner {
    pthread_mutex_t lock;
    pthread_cond_t work_cond;
    pthread_t *threads;
#if defined(BTC_MINER_OPENCL)
    pthread_t *opencl_threads;
    opencl_miner_t **opencl_devices;
    uint64_t *opencl_hashes;
#endif
#if defined(BTC_MINER_CUDA)
    pthread_t cuda_thread;
    cuda_miner_t *cuda_device;
    uint64_t cuda_hashes;
#endif
    int thread_count;
    int cpu_affinity_enabled;
    int cpu_affinity_count;
    int cpu_affinity_plan[CPU_INFO_MAX_CPUS];
    int started;
    int opencl_started;
    int cuda_started;
    int stop;
    int paused;
    uint64_t pause_epoch;
    miner_opencl_config_t opencl_config;
    miner_cuda_config_t cuda_config;

    active_job_t job;
    uint64_t next_nonce;
    int nonce_exhausted;
    uint64_t hashes;
    uint64_t *thread_hashes;

    miner_share_t shares[SHARE_QUEUE_SIZE];
    int share_head;
    int share_tail;
    int share_count;
};

typedef struct {
    miner_t *miner;
    int id;
    int target_cpu;
} worker_arg_t;

#if defined(BTC_MINER_OPENCL)
typedef struct {
    miner_t *miner;
    opencl_miner_t *opencl;
    int id;
} opencl_worker_arg_t;
#endif

#if defined(BTC_MINER_CUDA)
typedef struct {
    miner_t *miner;
    cuda_miner_t *cuda;
} cuda_worker_arg_t;
#endif

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
    size_t hex_len = strlen(hex);
    if ((hex_len & 1U) != 0) {
        return -1;
    }

    size_t len = hex_len / 2;
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

static void sha256d_data(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint8_t first[SHA256_DIGEST_LENGTH];
    SHA256(data, len, first);
    SHA256(first, SHA256_DIGEST_LENGTH, out);
}

static uint32_t load_le32(const uint8_t *p) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
#else
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
#endif
}

static uint32_t load_be32(const uint8_t *p) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
#else
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
#endif
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

void miner_target_from_difficulty(double difficulty, uint8_t target[32]) {
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

int miner_hash_meets_target(const uint8_t hash[32], const uint8_t target[32]) {
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

double miner_hash_difficulty(const uint8_t hash[32]) {
    static const double diff1 =
        26959535291011309493156476344723991336010898738574164086137773096960.0;
    double value = 0.0;
    for (int i = 31; i >= 0; --i) {
        value = value * 256.0 + hash[i];
    }
    if (value <= 0.0) {
        return INFINITY;
    }
    return diff1 / value;
}

void miner_format_extranonce2(char *out, size_t out_size, int extranonce2_size, uint64_t value) {
    int chars = extranonce2_size * 2;
    if (chars <= 0 || (size_t)chars + 1 > out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return;
    }
    snprintf(out, out_size, "%0*llx", chars, (unsigned long long)value);
}

int miner_build_job(miner_job_t *out,
                    const char *job_id,
                    const char *prevhash,
                    const char *coinb1,
                    const char *coinb2,
                    const char **merkle,
                    int merkle_count,
                    const char *version,
                    const char *nbits,
                    const char *ntime,
                    const char *extranonce1,
                    const char *extranonce2,
                    double difficulty) {
    char *coinbase_hex = NULL;
    uint8_t *coinbase = NULL;
    size_t coinbase_len = 0;
    uint8_t merkle_root[32];
    int rc = -1;

    memset(out, 0, sizeof(*out));
    if (copy_checked(out->job_id, sizeof(out->job_id), job_id) != 0 ||
        copy_checked(out->extranonce2, sizeof(out->extranonce2), extranonce2) != 0 ||
        copy_checked(out->ntime, sizeof(out->ntime), ntime) != 0) {
        return -1;
    }

    size_t coinbase_hex_len = strlen(coinb1) + strlen(extranonce1) + strlen(extranonce2) + strlen(coinb2);
    coinbase_hex = malloc(coinbase_hex_len + 1);
    if (coinbase_hex == NULL) {
        return -1;
    }
    snprintf(coinbase_hex, coinbase_hex_len + 1, "%s%s%s%s", coinb1, extranonce1, extranonce2, coinb2);

    if (hex_to_alloc(coinbase_hex, &coinbase, &coinbase_len) != 0) {
        goto done;
    }
    sha256d_data(coinbase, coinbase_len, merkle_root);

    for (int i = 0; i < merkle_count; ++i) {
        uint8_t branch[32];
        uint8_t combined[64];
        if (hex_to_bytes(merkle[i], branch, sizeof(branch)) != 0) {
            goto done;
        }
        memcpy(combined, merkle_root, 32);
        memcpy(combined + 32, branch, 32);
        sha256d_data(combined, sizeof(combined), merkle_root);
    }

    uint8_t *p = out->header;
    if (hex_u32_to_le(version, p) != 0) {
        goto done;
    }
    p += 4;
    if (prevhash_to_header_bytes(prevhash, p) != 0) {
        goto done;
    }
    p += 32;
    memcpy(p, merkle_root, 32);
    p += 32;
    if (hex_u32_to_le(ntime, p) != 0) {
        goto done;
    }
    p += 4;
    if (hex_u32_to_le(nbits, p) != 0) {
        goto done;
    }
    p += 4;
    memset(p, 0, 4);

    miner_target_from_difficulty(difficulty, out->target);
    rc = 0;

done:
    free(coinbase);
    free(coinbase_hex);
    return rc;
}

static void queue_share_locked(miner_t *miner, const miner_job_t *job, uint32_t nonce, const uint8_t hash[32]) {
    if (miner->share_count >= SHARE_QUEUE_SIZE) {
        return;
    }

    miner_share_t *share = &miner->shares[miner->share_tail];
    memset(share, 0, sizeof(*share));
    share->seq = job->seq;
    share->nonce = nonce;
    share->difficulty = miner_hash_difficulty(hash);
    memcpy(share->hash, hash, 32);
    copy_checked(share->job_id, sizeof(share->job_id), job->job_id);
    copy_checked(share->extranonce2, sizeof(share->extranonce2), job->extranonce2);
    copy_checked(share->ntime, sizeof(share->ntime), job->ntime);

    miner->share_tail = (miner->share_tail + 1) % SHARE_QUEUE_SIZE;
    ++miner->share_count;
}

static int copy_job_and_nonce_range_sized(miner_t *miner,
                                          active_job_t *job,
                                          uint32_t *start_nonce,
                                          uint32_t *count,
                                          uint32_t batch_size);

static int copy_job_and_nonce_range(miner_t *miner,
                                    active_job_t *job,
                                    uint32_t *start_nonce,
                                    uint32_t *count) {
    uint32_t batch_size = MINER_BATCH_SIZE;
#if defined(BTC_MINER_OPENCL)
    if (miner != NULL && miner->opencl_started > 0) {
        batch_size = MINER_MIXED_CPU_BATCH_SIZE;
    }
#endif
#if defined(BTC_MINER_CUDA)
    if (miner != NULL && miner->cuda_started > 0) {
        batch_size = MINER_MIXED_CPU_BATCH_SIZE;
    }
#endif
    return copy_job_and_nonce_range_sized(miner, job, start_nonce, count, batch_size);
}

static uint32_t miner_cpu_batch_size(const miner_t *miner) {
#if defined(BTC_MINER_OPENCL)
    if (miner != NULL && miner->opencl_started > 0) {
        return MINER_MIXED_CPU_BATCH_SIZE;
    }
#endif
#if defined(BTC_MINER_CUDA)
    if (miner != NULL && miner->cuda_started > 0) {
        return MINER_MIXED_CPU_BATCH_SIZE;
    }
#endif
#if !defined(BTC_MINER_OPENCL) && !defined(BTC_MINER_CUDA)
    (void)miner;
#endif
    return MINER_BATCH_SIZE;
}

static void miner_wait_for_work(miner_t *miner) {
    if (miner == NULL) {
        return;
    }

    pthread_mutex_lock(&miner->lock);
    while (!miner->stop && (miner->paused || !miner->job.valid)) {
        pthread_cond_wait(&miner->work_cond, &miner->lock);
    }
    pthread_mutex_unlock(&miner->lock);
}

static int copy_job_and_nonce_range_sized(miner_t *miner,
                                          active_job_t *job,
                                          uint32_t *start_nonce,
                                          uint32_t *count,
                                          uint32_t batch_size) {
    int ok = 0;
    if (batch_size == 0) {
        batch_size = MINER_BATCH_SIZE;
    }
    pthread_mutex_lock(&miner->lock);
    if (!miner->stop && !miner->paused && miner->job.valid) {
        if (miner->next_nonce > UINT32_MAX) {
            miner->job.valid = 0;
            miner->nonce_exhausted = 1;
        } else {
            uint64_t remaining = (uint64_t)UINT32_MAX - miner->next_nonce + 1U;
            uint32_t range = remaining < batch_size ? (uint32_t)remaining : batch_size;
            *job = miner->job;
            job->pause_epoch = miner->pause_epoch;
            *start_nonce = (uint32_t)miner->next_nonce;
            *count = range;
            miner->next_nonce += range;
            ok = 1;
        }
    }
    pthread_mutex_unlock(&miner->lock);
    return ok;
}

typedef struct {
    miner_t *miner;
    const active_job_t *job;
} scan_match_context_t;

static int scan_result_still_current_locked(const miner_t *miner, const active_job_t *job) {
    return miner != NULL &&
           job != NULL &&
           !miner->stop &&
           !miner->paused &&
           miner->pause_epoch == job->pause_epoch &&
           miner->job.public_job.seq == job->public_job.seq;
}

static void queue_scan_match(void *opaque, uint32_t nonce, const uint32_t hash_words[8]) {
    scan_match_context_t *ctx = (scan_match_context_t *)opaque;
    uint8_t hash[32];

    if (ctx == NULL || ctx->miner == NULL || ctx->job == NULL) {
        return;
    }

    sha256d_words_to_hash(hash_words, hash);
    pthread_mutex_lock(&ctx->miner->lock);
    if (scan_result_still_current_locked(ctx->miner, ctx->job)) {
        queue_share_locked(ctx->miner, &ctx->job->public_job, nonce, hash);
    }
    pthread_mutex_unlock(&ctx->miner->lock);
}

static void *worker_main(void *opaque) {
    worker_arg_t *arg = (worker_arg_t *)opaque;
    miner_t *miner = arg->miner;
    int id = arg->id;
    int target_cpu = arg->target_cpu;
    sha256d_nonce_range_func_t scan_nonce_range = sha256d_nonce_range_func();

    free(arg);
    if (target_cpu >= 0 && cpu_info_bind_current_thread(target_cpu) != 0) {
        fprintf(stderr, "%s[MINER]%s worker=%d failed to bind cpu%d\n",
                C_YELLOW,
                C_RESET,
                id,
                target_cpu);
    }

    for (;;) {
        pthread_mutex_lock(&miner->lock);
        int stop = miner->stop;
        int paused = miner->paused;
        pthread_mutex_unlock(&miner->lock);
        if (stop) {
            break;
        }
        if (paused) {
            miner_wait_for_work(miner);
            continue;
        }

        active_job_t job;
        uint32_t start_nonce = 0;
        uint32_t nonce_count = 0;
        if (!copy_job_and_nonce_range(miner, &job, &start_nonce, &nonce_count)) {
            miner_wait_for_work(miner);
            continue;
        }

        scan_match_context_t scan_context = {
            .miner = miner,
            .job = &job,
        };
        uint64_t local_hashes = 0;
        scan_nonce_range(&job.midstate, job.tail_words, job.target_words,
                         start_nonce, nonce_count, &scan_context, queue_scan_match);
        local_hashes += nonce_count;

        pthread_mutex_lock(&miner->lock);
        if (scan_result_still_current_locked(miner, &job)) {
            miner->hashes += local_hashes;
            if (id >= 0 && id < miner->thread_count) {
                miner->thread_hashes[id] += local_hashes;
            }
        }
        pthread_mutex_unlock(&miner->lock);
    }

    return NULL;
}

void miner_opencl_config_defaults(miner_opencl_config_t *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->enabled = 0;
    config->all_devices = 1;
    config->platform = 0;
    config->device = 0;
    config->batch_size = MINER_OPENCL_DEFAULT_BATCH_SIZE;
    config->local_work_size = 0;
    config->nonces_per_work_item = MINER_OPENCL_DEFAULT_NONCES_PER_WORK_ITEM;
    config->max_results = MINER_OPENCL_DEFAULT_MAX_RESULTS;
    config->backend_variant = MINER_OPENCL_BACKEND_AUTO;
    config->kernel_variant = MINER_OPENCL_KERNEL_AUTO;
}

#if defined(BTC_MINER_OPENCL)
static void opencl_device_to_config(const miner_opencl_config_t *base,
                                    const miner_opencl_device_config_t *device,
                                    miner_opencl_config_t *out) {
    miner_opencl_config_defaults(out);
    if (base != NULL) {
        *out = *base;
    }
    out->enabled = 1;
    out->all_devices = 0;
    out->device_count = 0;
    if (device != NULL) {
        out->platform = device->platform;
        out->device = device->device;
        out->batch_size = device->batch_size != 0 ? device->batch_size : out->batch_size;
        out->local_work_size = device->local_work_size != 0 ? device->local_work_size : out->local_work_size;
        out->nonces_per_work_item = device->nonces_per_work_item != 0 ?
            device->nonces_per_work_item : out->nonces_per_work_item;
        out->max_results = device->max_results != 0 ? device->max_results : out->max_results;
        out->backend_variant = device->backend_variant != MINER_OPENCL_BACKEND_AUTO ?
            device->backend_variant : out->backend_variant;
        out->kernel_variant = device->kernel_variant != MINER_OPENCL_KERNEL_AUTO ?
            device->kernel_variant : out->kernel_variant;
    }
}

static void *opencl_worker_main(void *opaque) {
    opencl_worker_arg_t *arg = (opencl_worker_arg_t *)opaque;
    miner_t *miner = arg->miner;
    opencl_miner_t *opencl = arg->opencl;
    int id = arg->id;
    uint32_t batch_size = opencl_miner_batch_size(opencl);

    free(arg);

    for (;;) {
        pthread_mutex_lock(&miner->lock);
        int stop = miner->stop;
        int paused = miner->paused;
        pthread_mutex_unlock(&miner->lock);
        if (stop) {
            break;
        }
        if (paused) {
            miner_wait_for_work(miner);
            continue;
        }

        active_job_t job;
        uint32_t start_nonce = 0;
        uint32_t nonce_count = 0;
        if (!copy_job_and_nonce_range_sized(miner, &job, &start_nonce, &nonce_count, batch_size)) {
            miner_wait_for_work(miner);
            continue;
        }

        scan_match_context_t scan_context = {
            .miner = miner,
            .job = &job,
        };

        if (opencl_miner_scan(opencl,
                              &job.midstate,
                              job.tail_words,
                              job.target_words,
                              start_nonce,
                              nonce_count,
                              &scan_context,
                              queue_scan_match) != 0) {
            fprintf(stderr, "%s[OPENCL]%s scan failed; retrying\n", C_YELLOW, C_RESET);
            miner_sleep_ms(250);
            continue;
        }

        pthread_mutex_lock(&miner->lock);
        if (scan_result_still_current_locked(miner, &job)) {
            miner->hashes += nonce_count;
            if (id >= 0 && id < miner->opencl_started) {
                miner->opencl_hashes[id] += nonce_count;
            }
        }
        pthread_mutex_unlock(&miner->lock);
    }

    return NULL;
}
#endif

#if defined(BTC_MINER_CUDA)
static void *cuda_worker_main(void *opaque) {
    cuda_worker_arg_t *arg = (cuda_worker_arg_t *)opaque;
    miner_t *miner = arg->miner;
    cuda_miner_t *cuda = arg->cuda;
    uint32_t batch_size = cuda_miner_batch_size(cuda);

    free(arg);

    for (;;) {
        pthread_mutex_lock(&miner->lock);
        int stop = miner->stop;
        int paused = miner->paused;
        pthread_mutex_unlock(&miner->lock);
        if (stop) {
            break;
        }
        if (paused) {
            miner_wait_for_work(miner);
            continue;
        }

        active_job_t job;
        uint32_t start_nonce = 0;
        uint32_t nonce_count = 0;
        if (!copy_job_and_nonce_range_sized(miner, &job, &start_nonce, &nonce_count, batch_size)) {
            miner_wait_for_work(miner);
            continue;
        }

        scan_match_context_t scan_context = {
            .miner = miner,
            .job = &job,
        };

        if (cuda_miner_scan(cuda,
                            &job.midstate,
                            job.tail_words,
                            job.target_words,
                            start_nonce,
                            nonce_count,
                            &scan_context,
                            queue_scan_match) != 0) {
            fprintf(stderr, "%s[CUDA]%s scan failed; retrying\n", C_YELLOW, C_RESET);
            miner_sleep_ms(250);
            continue;
        }

        pthread_mutex_lock(&miner->lock);
        if (scan_result_still_current_locked(miner, &job)) {
            miner->hashes += nonce_count;
            miner->cuda_hashes += nonce_count;
        }
        pthread_mutex_unlock(&miner->lock);
    }

    return NULL;
}
#endif

static void destroy_unstarted_gpu_devices(miner_t *miner) {
#if !defined(BTC_MINER_OPENCL) && !defined(BTC_MINER_CUDA)
    (void)miner;
#endif
#if defined(BTC_MINER_OPENCL)
    for (int i = 0; i < miner->opencl_started; ++i) {
        if (miner->opencl_devices != NULL && miner->opencl_devices[i] != NULL) {
            opencl_miner_destroy(miner->opencl_devices[i]);
            miner->opencl_devices[i] = NULL;
        }
    }
    miner->opencl_started = 0;
#endif
#if defined(BTC_MINER_CUDA)
    if (miner->cuda_device != NULL) {
        cuda_miner_destroy(miner->cuda_device);
        miner->cuda_device = NULL;
    }
    miner->cuda_started = 0;
#endif
}

static int stop_partially_started_cpu_threads(miner_t *miner, int thread_count) {
    destroy_unstarted_gpu_devices(miner);
    miner->thread_count = thread_count;
    miner->started = 1;
    miner_stop(miner);
    return -1;
}

miner_t *miner_create(int thread_count) {
    return miner_create_with_options(thread_count, NULL);
}

miner_t *miner_create_with_options(int thread_count, const miner_opencl_config_t *opencl_config) {
    return miner_create_with_backend_options(thread_count, opencl_config, NULL);
}

miner_t *miner_create_with_backend_options(int thread_count,
                                           const miner_opencl_config_t *opencl_config,
                                           const miner_cuda_config_t *cuda_config) {
    if (thread_count < 0) {
        thread_count = 0;
    }

    miner_t *miner = calloc(1, sizeof(*miner));
    if (miner == NULL) {
        return NULL;
    }

    if (thread_count > 0) {
        miner->threads = calloc((size_t)thread_count, sizeof(*miner->threads));
        miner->thread_hashes = calloc((size_t)thread_count, sizeof(*miner->thread_hashes));
        if (miner->threads == NULL || miner->thread_hashes == NULL) {
            free(miner->thread_hashes);
            free(miner->threads);
            free(miner);
            return NULL;
        }
    }

    pthread_mutex_init(&miner->lock, NULL);
    pthread_cond_init(&miner->work_cond, NULL);
    miner->thread_count = thread_count;
    miner->next_nonce = 0;
    miner_opencl_config_defaults(&miner->opencl_config);
#if defined(BTC_MINER_CUDA)
    miner_cuda_config_defaults(&miner->cuda_config);
#else
    memset(&miner->cuda_config, 0, sizeof(miner->cuda_config));
#endif
    if (opencl_config != NULL) {
        miner->opencl_config = *opencl_config;
        if (miner->opencl_config.batch_size == 0) {
            miner->opencl_config.batch_size = MINER_OPENCL_DEFAULT_BATCH_SIZE;
        }
        if (miner->opencl_config.nonces_per_work_item == 0) {
            miner->opencl_config.nonces_per_work_item = MINER_OPENCL_DEFAULT_NONCES_PER_WORK_ITEM;
        }
        if (miner->opencl_config.max_results == 0) {
            miner->opencl_config.max_results = MINER_OPENCL_DEFAULT_MAX_RESULTS;
        }
        if (miner->opencl_config.backend_variant < MINER_OPENCL_BACKEND_AUTO ||
            miner->opencl_config.backend_variant > MINER_OPENCL_BACKEND_MODERN) {
            miner->opencl_config.backend_variant = MINER_OPENCL_BACKEND_AUTO;
        }
        if (miner->opencl_config.kernel_variant < MINER_OPENCL_KERNEL_AUTO ||
            miner->opencl_config.kernel_variant > MINER_OPENCL_KERNEL_LAST) {
            miner->opencl_config.kernel_variant = MINER_OPENCL_KERNEL_AUTO;
        }
    }
    if (cuda_config != NULL) {
        miner->cuda_config = *cuda_config;
        if (miner->cuda_config.batch_size == 0) {
            miner->cuda_config.batch_size = MINER_CUDA_DEFAULT_BATCH_SIZE;
        }
        if (miner->cuda_config.threads_per_block == 0) {
            miner->cuda_config.threads_per_block = MINER_CUDA_DEFAULT_THREADS_PER_BLOCK;
        }
        if (miner->cuda_config.nonces_per_thread == 0) {
            miner->cuda_config.nonces_per_thread = MINER_CUDA_DEFAULT_NONCES_PER_THREAD;
        }
        if (miner->cuda_config.max_results == 0) {
            miner->cuda_config.max_results = MINER_CUDA_DEFAULT_MAX_RESULTS;
        }
        if (miner->cuda_config.kernel_variant < MINER_CUDA_KERNEL_STANDARD ||
            miner->cuda_config.kernel_variant > MINER_CUDA_KERNEL_LAST) {
            miner->cuda_config.kernel_variant = MINER_CUDA_DEFAULT_KERNEL_VARIANT;
        }
    }
    return miner;
}

void miner_set_cpu_affinity(miner_t *miner, int enabled) {
    if (miner == NULL || miner->started) {
        return;
    }
    miner->cpu_affinity_enabled = enabled ? 1 : 0;
}

void miner_destroy(miner_t *miner) {
    if (miner == NULL) {
        return;
    }
    miner_stop(miner);
    pthread_cond_destroy(&miner->work_cond);
    pthread_mutex_destroy(&miner->lock);
#if defined(BTC_MINER_OPENCL)
    free(miner->opencl_hashes);
    free(miner->opencl_devices);
    free(miner->opencl_threads);
#endif
#if defined(BTC_MINER_CUDA)
    if (miner->cuda_device != NULL) {
        cuda_miner_destroy(miner->cuda_device);
    }
#endif
    free(miner->thread_hashes);
    free(miner->threads);
    free(miner);
}

int miner_start(miner_t *miner) {
    if (miner == NULL || miner->started) {
        return 0;
    }
    int requested_cpu_threads = miner->thread_count;
    int opencl_running = 0;
    int cuda_running = 0;

    if (miner->cpu_affinity_enabled && requested_cpu_threads > 0 && cpu_info_affinity_supported()) {
        cpu_info_t info;
        cpu_info_detect(&info);
        miner->cpu_affinity_count = cpu_info_affinity_plan(&info,
                                                           miner->cpu_affinity_plan,
                                                           CPU_INFO_MAX_CPUS);
        if (miner->cpu_affinity_count <= 0) {
            miner->cpu_affinity_enabled = 0;
        }
    } else {
        miner->cpu_affinity_enabled = 0;
        miner->cpu_affinity_count = 0;
    }

#if defined(BTC_MINER_OPENCL)
    if (miner->opencl_config.enabled) {
        miner_opencl_device_config_t resolved[MINER_OPENCL_MAX_DEVICES];
        char error[2048];
        error[0] = '\0';
        int resolved_count = opencl_miner_resolve_devices(&miner->opencl_config,
                                                          resolved,
                                                          MINER_OPENCL_MAX_DEVICES,
                                                          error,
                                                          sizeof(error));
        if (resolved_count <= 0) {
            fprintf(stderr, "%s[OPENCL]%s unavailable: %s\n",
                    C_YELLOW,
                    C_RESET,
                    error[0] != '\0' ? error : "no usable OpenCL GPU devices");
        } else {
            miner->opencl_threads = calloc((size_t)resolved_count, sizeof(*miner->opencl_threads));
            miner->opencl_devices = calloc((size_t)resolved_count, sizeof(*miner->opencl_devices));
            miner->opencl_hashes = calloc((size_t)resolved_count, sizeof(*miner->opencl_hashes));
            if (miner->opencl_threads == NULL || miner->opencl_devices == NULL || miner->opencl_hashes == NULL) {
                fprintf(stderr, "%s[OPENCL]%s allocation failed\n", C_BRIGHT_RED, C_RESET);
                free(miner->opencl_hashes);
                free(miner->opencl_devices);
                free(miner->opencl_threads);
                miner->opencl_hashes = NULL;
                miner->opencl_devices = NULL;
                miner->opencl_threads = NULL;
            } else {
                for (int i = 0; i < resolved_count; ++i) {
                    miner_opencl_config_t device_config;
                    opencl_device_to_config(&miner->opencl_config, &resolved[i], &device_config);

                    opencl_self_test_result_t test_result;
                    error[0] = '\0';
                    if (opencl_miner_self_test(&device_config, &test_result, error, sizeof(error)) != 0) {
                        fprintf(stderr,
                                "%s[OPENCL]%s platform=%d device=%d self-test failed: %s\n",
                                C_YELLOW,
                                C_RESET,
                                resolved[i].platform,
                                resolved[i].device,
                                error[0] != '\0' ? error : "unknown error");
                        continue;
                    }

                    error[0] = '\0';
                    opencl_miner_t *opencl = opencl_miner_create(&device_config, error, sizeof(error));
                    if (opencl == NULL) {
                        fprintf(stderr,
                                "%s[OPENCL]%s platform=%d device=%d unavailable: %s\n",
                                C_YELLOW,
                                C_RESET,
                                resolved[i].platform,
                                resolved[i].device,
                                error[0] != '\0' ? error : "unknown error");
                        continue;
                    }

                    int slot = miner->opencl_started++;
                    miner->opencl_devices[slot] = opencl;
                    printf("%s[OPENCL]%s #%d platform=%d device=%d backend=%s self-test=ok device=%s%s%s version=%s batch=%u local=%u npi=%u\n",
                           C_CYAN,
                           C_RESET,
                           slot,
                           resolved[i].platform,
                           resolved[i].device,
                           opencl_miner_backend_name(opencl),
                           C_BRIGHT_CYAN,
                           opencl_miner_device_name(opencl),
                           C_RESET,
                           opencl_miner_device_version(opencl),
                           opencl_miner_batch_size(opencl),
                           opencl_miner_local_work_size(opencl),
                           opencl_miner_nonces_per_work_item(opencl));
                }
            }
        }
    }
#else
    if (miner->opencl_config.enabled) {
        fprintf(stderr, "%s[OPENCL]%s unavailable: this build was compiled without OpenCL support\n",
                C_YELLOW,
                C_RESET);
    }
#endif

#if defined(BTC_MINER_CUDA)
    if (miner->cuda_config.enabled) {
        char error[512];
        cuda_self_test_result_t test_result;
        error[0] = '\0';
        if (cuda_miner_self_test(&miner->cuda_config, &test_result, error, sizeof(error)) != 0) {
            fprintf(stderr,
                    "%s[CUDA]%s device=%d self-test failed: %s\n",
                    C_YELLOW,
                    C_RESET,
                    miner->cuda_config.device,
                    error[0] != '\0' ? error : "unknown error");
        } else {
            error[0] = '\0';
            miner->cuda_device = cuda_miner_create(&miner->cuda_config, error, sizeof(error));
            if (miner->cuda_device == NULL) {
                fprintf(stderr,
                        "%s[CUDA]%s device=%d unavailable: %s\n",
                        C_YELLOW,
                        C_RESET,
                        miner->cuda_config.device,
                        error[0] != '\0' ? error : "unknown error");
            } else {
                miner->cuda_started = 1;
                printf("%s[CUDA]%s device=%d self-test=ok name=%s%s%s compute=sm_%d%d driver=%d kernel=%s batch=%u block=%u npt=%u\n",
                       C_CYAN,
                       C_RESET,
                       miner->cuda_config.device,
                       C_BRIGHT_CYAN,
                       cuda_miner_device_name(miner->cuda_device),
                       C_RESET,
                       cuda_miner_compute_major(miner->cuda_device),
                       cuda_miner_compute_minor(miner->cuda_device),
                       cuda_miner_driver_version(miner->cuda_device),
                       cuda_miner_kernel_variant_name(cuda_miner_kernel_variant(miner->cuda_device)),
                       cuda_miner_batch_size(miner->cuda_device),
                       cuda_miner_threads_per_block(miner->cuda_device),
                       cuda_miner_nonces_per_thread(miner->cuda_device));
            }
        }
    }
#else
    if (miner->cuda_config.enabled) {
        fprintf(stderr, "%s[CUDA]%s unavailable: this build was compiled without CUDA support\n",
                C_YELLOW,
                C_RESET);
    }
#endif

    if (requested_cpu_threads <= 0
#if defined(BTC_MINER_OPENCL)
        && miner->opencl_started <= 0
#endif
#if defined(BTC_MINER_CUDA)
        && miner->cuda_started <= 0
#endif
    ) {
        return -1;
    }

    miner->thread_count = 0;
    for (int i = 0; i < requested_cpu_threads; ++i) {
        worker_arg_t *arg = malloc(sizeof(*arg));
        if (arg == NULL) {
            return stop_partially_started_cpu_threads(miner, i);
        }
        arg->miner = miner;
        arg->id = i;
        arg->target_cpu = miner->cpu_affinity_enabled && miner->cpu_affinity_count > 0 ?
            miner->cpu_affinity_plan[i % miner->cpu_affinity_count] : -1;
        miner->thread_count = i + 1;
        if (pthread_create(&miner->threads[i], NULL, worker_main, arg) != 0) {
            free(arg);
            return stop_partially_started_cpu_threads(miner, i);
        }
    }

#if defined(BTC_MINER_OPENCL)
    int opencl_thread_count = miner->opencl_started;
    for (int i = 0; i < opencl_thread_count; ++i) {
        if (miner->opencl_devices[i] == NULL) {
            continue;
        }
        opencl_worker_arg_t *arg = malloc(sizeof(*arg));
        if (arg == NULL) {
            fprintf(stderr, "%s[OPENCL]%s failed to allocate worker arg\n", C_BRIGHT_RED, C_RESET);
            opencl_miner_destroy(miner->opencl_devices[i]);
            miner->opencl_devices[i] = NULL;
            continue;
        }
        arg->miner = miner;
        arg->opencl = miner->opencl_devices[i];
        arg->id = i;
        if (pthread_create(&miner->opencl_threads[i], NULL, opencl_worker_main, arg) != 0) {
            fprintf(stderr, "%s[OPENCL]%s failed to start worker thread\n", C_BRIGHT_RED, C_RESET);
            free(arg);
            opencl_miner_destroy(miner->opencl_devices[i]);
            miner->opencl_devices[i] = NULL;
        } else {
            ++opencl_running;
        }
    }
#if !defined(BTC_MINER_CUDA)
    if (miner->thread_count <= 0 && opencl_running <= 0) {
        miner->started = 1;
        miner_stop(miner);
        return -1;
    }
#endif
#endif

#if defined(BTC_MINER_CUDA)
    if (miner->cuda_started > 0 && miner->cuda_device != NULL) {
        cuda_worker_arg_t *arg = malloc(sizeof(*arg));
        if (arg == NULL) {
            fprintf(stderr, "%s[CUDA]%s failed to allocate worker arg\n", C_BRIGHT_RED, C_RESET);
            cuda_miner_destroy(miner->cuda_device);
            miner->cuda_device = NULL;
            miner->cuda_started = 0;
        } else {
            arg->miner = miner;
            arg->cuda = miner->cuda_device;
            if (pthread_create(&miner->cuda_thread, NULL, cuda_worker_main, arg) != 0) {
                fprintf(stderr, "%s[CUDA]%s failed to start worker thread\n", C_BRIGHT_RED, C_RESET);
                free(arg);
                cuda_miner_destroy(miner->cuda_device);
                miner->cuda_device = NULL;
                miner->cuda_started = 0;
            } else {
                cuda_running = 1;
            }
        }
    }
    if (miner->thread_count <= 0 && opencl_running <= 0 && cuda_running <= 0) {
        return -1;
    }
#endif

    miner->started = 1;
    printf("%s[MINER]%s started cpu-threads=%s%d%s opencl-devices=%s%d%s cuda-devices=%s%d%s cpu-batch=%u affinity=%s%s%s",
           C_MAGENTA,
           C_RESET,
           C_BRIGHT_GREEN,
           miner->thread_count,
           C_RESET,
           opencl_running ? C_BRIGHT_GREEN : C_GRAY,
           opencl_running,
           C_RESET,
           cuda_running ? C_BRIGHT_GREEN : C_GRAY,
           cuda_running,
           C_RESET,
           miner_cpu_batch_size(miner),
           miner->cpu_affinity_enabled ? C_BRIGHT_GREEN : C_GRAY,
           miner->cpu_affinity_enabled ? "on" : "off",
           C_RESET);
    if (miner->cpu_affinity_enabled && miner->cpu_affinity_count > 0) {
        printf(" cpu-order=");
        for (int i = 0; i < miner->cpu_affinity_count; ++i) {
            printf("%s%d", i == 0 ? "" : ",", miner->cpu_affinity_plan[i]);
        }
    }
    printf("\n");
    return 0;
}

void miner_stop(miner_t *miner) {
    if (miner == NULL || !miner->started) {
        return;
    }

    pthread_mutex_lock(&miner->lock);
    miner->stop = 1;
    pthread_cond_broadcast(&miner->work_cond);
    pthread_mutex_unlock(&miner->lock);

    for (int i = 0; i < miner->thread_count; ++i) {
        pthread_join(miner->threads[i], NULL);
    }
#if defined(BTC_MINER_OPENCL)
    for (int i = 0; i < miner->opencl_started; ++i) {
        if (miner->opencl_devices != NULL && miner->opencl_devices[i] != NULL) {
            pthread_join(miner->opencl_threads[i], NULL);
            opencl_miner_destroy(miner->opencl_devices[i]);
            miner->opencl_devices[i] = NULL;
        }
    }
    miner->opencl_started = 0;
#endif
#if defined(BTC_MINER_CUDA)
    if (miner->cuda_started > 0 && miner->cuda_device != NULL) {
        pthread_join(miner->cuda_thread, NULL);
        cuda_miner_destroy(miner->cuda_device);
        miner->cuda_device = NULL;
    }
    miner->cuda_started = 0;
#endif
    miner->started = 0;
}

void miner_set_job(miner_t *miner, const miner_job_t *job) {
    if (miner == NULL || job == NULL) {
        return;
    }

    active_job_t active;
    memset(&active, 0, sizeof(active));
    active.public_job = *job;
    active.valid = 1;
    sha256d_80_midstate_prepare(&active.midstate, active.public_job.header);
    for (int i = 0; i < 4; ++i) {
        active.tail_words[i] = load_be32(active.public_job.header + 64 + i * 4);
    }
    for (int i = 0; i < 8; ++i) {
        active.target_words[i] = load_le32(active.public_job.target + i * 4);
    }

    pthread_mutex_lock(&miner->lock);
    active.public_job.seq = miner->job.public_job.seq + 1;
    miner->job = active;
    miner->next_nonce = 0;
    miner->nonce_exhausted = 0;
    miner->share_head = 0;
    miner->share_tail = 0;
    miner->share_count = 0;
    pthread_cond_broadcast(&miner->work_cond);
    pthread_mutex_unlock(&miner->lock);

    printf("%s[MINER]%s job=%s%s%s seq=%llu en2=%s\n",
           C_MAGENTA,
           C_RESET,
           C_BRIGHT_YELLOW,
           active.public_job.job_id,
           C_RESET,
           (unsigned long long)active.public_job.seq,
           active.public_job.extranonce2);
}

void miner_set_paused(miner_t *miner, int paused) {
    if (miner == NULL) {
        return;
    }

    pthread_mutex_lock(&miner->lock);
    int next_paused = paused ? 1 : 0;
    if (!miner->paused && next_paused) {
        ++miner->pause_epoch;
    }
    miner->paused = next_paused;
    pthread_cond_broadcast(&miner->work_cond);
    pthread_mutex_unlock(&miner->lock);
}

int miner_is_paused(miner_t *miner) {
    if (miner == NULL) {
        return 0;
    }

    pthread_mutex_lock(&miner->lock);
    int paused = miner->paused;
    pthread_mutex_unlock(&miner->lock);
    return paused;
}

int miner_take_nonce_exhausted(miner_t *miner) {
    if (miner == NULL) {
        return 0;
    }

    pthread_mutex_lock(&miner->lock);
    int exhausted = miner->nonce_exhausted;
    miner->nonce_exhausted = 0;
    pthread_mutex_unlock(&miner->lock);
    return exhausted;
}

int miner_pop_share(miner_t *miner, miner_share_t *share) {
    int found = 0;
    pthread_mutex_lock(&miner->lock);
    if (miner->share_count > 0) {
        *share = miner->shares[miner->share_head];
        miner->share_head = (miner->share_head + 1) % SHARE_QUEUE_SIZE;
        --miner->share_count;
        found = 1;
    }
    pthread_mutex_unlock(&miner->lock);
    return found;
}

uint64_t miner_hashes(miner_t *miner) {
    uint64_t hashes;
    pthread_mutex_lock(&miner->lock);
    hashes = miner->hashes;
    pthread_mutex_unlock(&miner->lock);
    return hashes;
}

int miner_thread_count(miner_t *miner) {
    if (miner == NULL) {
        return 0;
    }

    pthread_mutex_lock(&miner->lock);
    int opencl_count = 0;
#if defined(BTC_MINER_OPENCL)
    for (int i = 0; i < miner->opencl_started; ++i) {
        if (miner->opencl_devices != NULL && miner->opencl_devices[i] != NULL) {
            ++opencl_count;
        }
    }
#endif
    int cuda_count = 0;
#if defined(BTC_MINER_CUDA)
    if (miner->cuda_started > 0 && miner->cuda_device != NULL) {
        cuda_count = 1;
    }
#endif
    int count = miner->thread_count + opencl_count + cuda_count;
    pthread_mutex_unlock(&miner->lock);
    return count;
}

int miner_snapshot_thread_hashes(miner_t *miner, uint64_t *out, int max_count) {
    if (miner == NULL || out == NULL || max_count <= 0) {
        return 0;
    }

    pthread_mutex_lock(&miner->lock);
    int opencl_count = 0;
#if defined(BTC_MINER_OPENCL)
    for (int i = 0; i < miner->opencl_started; ++i) {
        if (miner->opencl_devices != NULL && miner->opencl_devices[i] != NULL) {
            ++opencl_count;
        }
    }
#endif
    int cuda_count = 0;
#if defined(BTC_MINER_CUDA)
    if (miner->cuda_started > 0 && miner->cuda_device != NULL) {
        cuda_count = 1;
    }
#endif
    int count = miner->thread_count + opencl_count + cuda_count;
    if (count > max_count) {
        count = max_count;
    }
    int cpu_count = miner->thread_count < count ? miner->thread_count : count;
    for (int i = 0; i < cpu_count; ++i) {
        out[i] = miner->thread_hashes[i];
    }
#if defined(BTC_MINER_OPENCL) || defined(BTC_MINER_CUDA)
    int out_index = cpu_count;
#endif
#if defined(BTC_MINER_OPENCL)
    for (int i = 0; i < miner->opencl_started && out_index < count; ++i) {
        if (miner->opencl_devices != NULL && miner->opencl_devices[i] != NULL) {
            out[out_index++] = miner->opencl_hashes[i];
        }
    }
#endif
#if defined(BTC_MINER_CUDA)
    if (miner->cuda_started > 0 && miner->cuda_device != NULL && out_index < count) {
        out[out_index++] = miner->cuda_hashes;
    }
#endif
    pthread_mutex_unlock(&miner->lock);

    return count;
}
