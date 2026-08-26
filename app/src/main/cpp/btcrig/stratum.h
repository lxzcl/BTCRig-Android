#ifndef BTC_MINER_STRATUM_H
#define BTC_MINER_STRATUM_H

#include "miner.h"

#define STRATUM_PENDING_SHARES 64
#define STRATUM_MAX_MERKLE_BRANCHES 128
#define STRATUM_MAX_COINBASE_HEX 16384

typedef struct {
    int id;
    int active;
    double difficulty;
    double submitted_at;
} stratum_pending_share_t;

typedef struct {
    char extranonce1[128];
    char pool_url[256];
    char pool_host[256];
    char pool_port[16];
    char user[256];
    char current_job_id[128];
    char current_ntime[16];
    char template_job_id[128];
    char template_prevhash[65];
    char template_coinb1[STRATUM_MAX_COINBASE_HEX];
    char template_coinb2[STRATUM_MAX_COINBASE_HEX];
    char template_version[9];
    char template_nbits[9];
    char template_ntime[9];
    char template_merkle[STRATUM_MAX_MERKLE_BRANCHES][65];
    int template_merkle_count;
    int template_valid;
    int extranonce2_size;
    double difficulty;
    double started_at;
    double connected_at;
    double last_share_difficulty;
    double best_share_difficulty;
    double last_result_difficulty;
    double last_result_elapsed;
    unsigned long notify_count;
    unsigned long submit_count;
    unsigned long accept_count;
    unsigned long reject_count;
    unsigned long connect_count;
    int pending_count;
    int last_result_accepted;
    int subscribed;
    int authorized;
    miner_t *miner;
    unsigned long long extranonce2_counter;
    stratum_pending_share_t pending[STRATUM_PENDING_SHARES];
} stratum_state_t;

typedef struct {
    int thread_count;
    int cpu_affinity;
    int enable_mining;
    miner_opencl_config_t opencl;
    miner_cuda_config_t cuda;
    double stats_interval;
    double stop_at;
    double session_seconds;
    const char *session_label;
} stratum_client_config_t;

void stratum_state_init(stratum_state_t *state);
int stratum_process_line(stratum_state_t *state, const char *line);
int stratum_run_client(const char *url,
                       const char *user,
                       const char *password,
                       double suggest_difficulty,
                       const stratum_client_config_t *config);

#endif
