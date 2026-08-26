#ifndef BTCRIG_CORE_H
#define BTCRIG_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

const char *btcrig_core_backend_name(void);
int btcrig_core_self_test(void);
int btcrig_core_start(void);
void btcrig_core_stop(void);
int btcrig_core_is_running(void);
double btcrig_core_benchmark_cpu(int seconds, int threads);

#ifdef __cplusplus
}
#endif

#endif
