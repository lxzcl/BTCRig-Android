#ifndef BTCRIG_OPENCL_MINER_H
#define BTCRIG_OPENCL_MINER_H

#include "miner.h"
#include "sha256d.h"

#include <stddef.h>
#include <stdint.h>

typedef struct opencl_miner opencl_miner_t;

typedef struct {
    char backend[32];
    char device_name[128];
    char device_version[128];
    uint32_t checked_nonces;
} opencl_self_test_result_t;

opencl_miner_t *opencl_miner_create(const miner_opencl_config_t *config,
                                    char *error,
                                    size_t error_size);
void opencl_miner_destroy(opencl_miner_t *miner);
uint32_t opencl_miner_batch_size(const opencl_miner_t *miner);
uint32_t opencl_miner_local_work_size(const opencl_miner_t *miner);
uint32_t opencl_miner_nonces_per_work_item(const opencl_miner_t *miner);
const char *opencl_miner_backend_name(const opencl_miner_t *miner);
const char *opencl_miner_device_name(const opencl_miner_t *miner);
const char *opencl_miner_device_version(const opencl_miner_t *miner);
int opencl_miner_self_test(const miner_opencl_config_t *config,
                           opencl_self_test_result_t *result,
                           char *error,
                           size_t error_size);
int opencl_miner_resolve_devices(const miner_opencl_config_t *config,
                                 miner_opencl_device_config_t *devices,
                                 int max_devices,
                                 char *error,
                                 size_t error_size);

int opencl_miner_scan(opencl_miner_t *miner,
                      const sha256_midstate_t *state,
                      const uint32_t tail_words[4],
                      const uint32_t target_words[8],
                      uint32_t start_nonce,
                      uint32_t nonce_count,
                      void *opaque,
                      sha256d_scan_match_func_t on_match);

#endif
