#ifndef BTC_MINER_MINER_H
#define BTC_MINER_MINER_H

#include <stddef.h>
#include <stdint.h>

typedef struct miner miner_t;

#define MINER_OPENCL_MAX_DEVICES 8
#define MINER_OPENCL_KERNEL_AUTO 0
#define MINER_OPENCL_KERNEL_COMPACT 1
#define MINER_OPENCL_KERNEL_UNROLLED 2
#define MINER_OPENCL_KERNEL_FIXED_NPI1 3
#define MINER_OPENCL_KERNEL_FIXED_NPI2 4
#define MINER_OPENCL_KERNEL_FIXED_NPI4 5
#define MINER_OPENCL_KERNEL_REGISTER_HEAVY 6
#define MINER_OPENCL_KERNEL_LEGACY_NOATOMIC 7
#define MINER_OPENCL_KERNEL_LAST MINER_OPENCL_KERNEL_LEGACY_NOATOMIC
#define MINER_OPENCL_BACKEND_AUTO 0
#define MINER_OPENCL_BACKEND_COMPAT10 1
#define MINER_OPENCL_BACKEND_MODERN 2
#define MINER_OPENCL_DEFAULT_BATCH_SIZE 1048576U
#define MINER_OPENCL_DEFAULT_NONCES_PER_WORK_ITEM 1U
#define MINER_OPENCL_DEFAULT_MAX_RESULTS 256U

typedef struct {
    int platform;
    int device;
    uint32_t batch_size;
    uint32_t local_work_size;
    uint32_t nonces_per_work_item;
    uint32_t max_results;
    int backend_variant;
    int kernel_variant;
} miner_opencl_device_config_t;

typedef struct {
    int enabled;
    int all_devices;
    int platform;
    int device;
    uint32_t batch_size;
    uint32_t local_work_size;
    uint32_t nonces_per_work_item;
    uint32_t max_results;
    int backend_variant;
    int kernel_variant;
    int device_count;
    miner_opencl_device_config_t devices[MINER_OPENCL_MAX_DEVICES];
} miner_opencl_config_t;

#ifndef BTCRIG_MINER_CUDA_CONFIG_DEFINED
#define BTCRIG_MINER_CUDA_CONFIG_DEFINED
#define MINER_CUDA_DEFAULT_DEVICE 0
#define MINER_CUDA_DEFAULT_BATCH_SIZE 4194304U
#define MINER_CUDA_DEFAULT_THREADS_PER_BLOCK 256U
#define MINER_CUDA_DEFAULT_NONCES_PER_THREAD 1U
#define MINER_CUDA_DEFAULT_MAX_RESULTS 256U
#define MINER_CUDA_KERNEL_STANDARD 0
#define MINER_CUDA_KERNEL_DUAL 1
#define MINER_CUDA_KERNEL_FIXED_NPT1 2
#define MINER_CUDA_KERNEL_FIXED_NPT2 3
#define MINER_CUDA_KERNEL_FIXED_NPT4 4
#define MINER_CUDA_KERNEL_LOP3 5
#define MINER_CUDA_KERNEL_FIXED_LOP3_NPT1 6
#define MINER_CUDA_KERNEL_FIXED_LOP3_NPT2 7
#define MINER_CUDA_KERNEL_FIXED_LOP3_NPT4 8
#define MINER_CUDA_KERNEL_LAST MINER_CUDA_KERNEL_FIXED_LOP3_NPT4
#define MINER_CUDA_DEFAULT_KERNEL_VARIANT MINER_CUDA_KERNEL_STANDARD

typedef struct {
    int enabled;
    int device;
    uint32_t batch_size;
    uint32_t threads_per_block;
    uint32_t nonces_per_thread;
    uint32_t max_results;
    int kernel_variant;
} miner_cuda_config_t;
#endif

typedef struct {
    uint64_t seq;
    uint32_t nonce;
    double difficulty;
    uint8_t hash[32];
    char job_id[128];
    char extranonce2[32];
    char ntime[16];
} miner_share_t;

typedef struct {
    uint64_t seq;
    char job_id[128];
    char extranonce2[32];
    char ntime[16];
    uint8_t header[80];
    uint8_t target[32];
} miner_job_t;

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
                    double difficulty);

void miner_opencl_config_defaults(miner_opencl_config_t *config);
void miner_cuda_config_defaults(miner_cuda_config_t *config);
miner_t *miner_create(int thread_count);
miner_t *miner_create_with_options(int thread_count, const miner_opencl_config_t *opencl_config);
miner_t *miner_create_with_backend_options(int thread_count,
                                           const miner_opencl_config_t *opencl_config,
                                           const miner_cuda_config_t *cuda_config);
void miner_set_cpu_affinity(miner_t *miner, int enabled);
void miner_destroy(miner_t *miner);
int miner_start(miner_t *miner);
void miner_stop(miner_t *miner);
void miner_set_job(miner_t *miner, const miner_job_t *job);
void miner_set_paused(miner_t *miner, int paused);
int miner_is_paused(miner_t *miner);
int miner_take_nonce_exhausted(miner_t *miner);
int miner_pop_share(miner_t *miner, miner_share_t *share);
uint64_t miner_hashes(miner_t *miner);
int miner_thread_count(miner_t *miner);
int miner_snapshot_thread_hashes(miner_t *miner, uint64_t *out, int max_count);

void miner_format_extranonce2(char *out, size_t out_size, int extranonce2_size, uint64_t value);
void miner_target_from_difficulty(double difficulty, uint8_t target[32]);
int miner_hash_meets_target(const uint8_t hash[32], const uint8_t target[32]);
double miner_hash_difficulty(const uint8_t hash[32]);

#endif
