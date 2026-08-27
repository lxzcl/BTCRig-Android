#ifndef BTCRIG_CORE_H
#define BTCRIG_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *btcrig_core_backend_name(void);
int btcrig_core_self_test(void);
int btcrig_core_start(const char *config_path);
void btcrig_core_stop(void);
int btcrig_core_is_running(void);
int btcrig_core_worker_count(void);
uint64_t btcrig_core_total_hashes(void);
double btcrig_core_hashrate(void);
int btcrig_core_stratum_connected(void);
uint64_t btcrig_core_stratum_jobs(void);
uint64_t btcrig_core_stratum_submits(void);
uint64_t btcrig_core_stratum_accepts(void);
uint64_t btcrig_core_stratum_rejects(void);
void btcrig_core_copy_pool(char *out, size_t out_size);
void btcrig_core_copy_stratum_status(char *out, size_t out_size);
void btcrig_core_copy_last_error(char *out, size_t out_size);
void btcrig_core_copy_opencl_status(const char *config_path, char *out, size_t out_size);
double btcrig_core_benchmark_cpu(int seconds, int threads);

#ifdef __cplusplus
}
#endif

#endif
