#ifndef BTC_MINER_CPU_INFO_H
#define BTC_MINER_CPU_INFO_H

#define CPU_INFO_MAX_CPUS 256
#define CPU_INFO_SOURCE_SIZE 64
#define CPU_INFO_SIBLINGS_SIZE 64

typedef struct {
    int id;
    int package_id;
    int core_id;
    long max_freq_khz;
    long capacity;
    char thread_siblings[CPU_INFO_SIBLINGS_SIZE];
} cpu_info_entry_t;

typedef struct {
    int logical_count;
    int physical_count;
    int has_smt;
    int recommended_threads;
    int entry_count;
    char source[CPU_INFO_SOURCE_SIZE];
    cpu_info_entry_t entries[CPU_INFO_MAX_CPUS];
} cpu_info_t;

void cpu_info_detect(cpu_info_t *info);
void cpu_info_print(const cpu_info_t *info);
int cpu_info_recommended_threads(void);
int cpu_info_affinity_plan(const cpu_info_t *info, int *out, int max_count);
int cpu_info_affinity_supported(void);
int cpu_info_bind_current_thread(int cpu_id);

#endif
