#ifndef BTCRIG_CORE_H
#define BTCRIG_CORE_H

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
double btcrig_core_benchmark_cpu(int seconds, int threads);

#ifdef __cplusplus
}
#endif

#endif
