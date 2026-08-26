#include "btcrig_core.h"

#include "miner.h"
#include "sha256d.h"
#include "stratum.h"

#include <jansson.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char pool[256];
    char user[256];
    char pass[128];
    int cpu_enabled;
    int cpu_threads;
    int cpu_affinity;
    int retries;
    int retry_pause;
    double difficulty;
    double stats_interval;
    miner_opencl_config_t opencl;
} core_config_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_thread_started = 0;
static int g_running = 0;
static volatile int g_stop_requested = 0;
static core_config_t g_config;
static char g_status[64] = "idle";
static char g_last_error[128] = "";

int btcrig_android_core_should_stop(void) {
    return g_stop_requested;
}

static void copy_text(char *out, size_t out_size, const char *text) {
    if (out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", text != NULL ? text : "");
}

static int available_processors(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count < 1) {
        return 1;
    }
    return count > 256 ? 256 : (int)count;
}

static void set_status(const char *status, const char *error) {
    pthread_mutex_lock(&g_lock);
    copy_text(g_status, sizeof(g_status), status);
    if (error != NULL) {
        copy_text(g_last_error, sizeof(g_last_error), error);
    }
    pthread_mutex_unlock(&g_lock);
}

static int json_bool_or(json_t *value, int fallback) {
    if (json_is_boolean(value)) {
        return json_is_true(value);
    }
    return fallback;
}

static int json_int_or(json_t *value, int fallback) {
    if (json_is_integer(value)) {
        return (int)json_integer_value(value);
    }
    return fallback;
}

static uint32_t json_u32_or(json_t *value, uint32_t fallback) {
    if (json_is_integer(value)) {
        json_int_t parsed = json_integer_value(value);
        if (parsed >= 0 && parsed <= UINT32_MAX) {
            return (uint32_t)parsed;
        }
    }
    return fallback;
}

static double json_number_or(json_t *value, double fallback) {
    return json_is_number(value) ? json_number_value(value) : fallback;
}

static void json_copy_string(json_t *value, char *out, size_t out_size) {
    const char *text = json_string_value(value);
    if (text != NULL) {
        copy_text(out, out_size, text);
    }
}

static int parse_opencl_backend(const char *text, int fallback) {
    if (text == NULL) {
        return fallback;
    }
    if (strcmp(text, "compat10") == 0 || strcmp(text, "compat") == 0) {
        return MINER_OPENCL_BACKEND_COMPAT10;
    }
    if (strcmp(text, "modern") == 0) {
        return MINER_OPENCL_BACKEND_MODERN;
    }
    return MINER_OPENCL_BACKEND_AUTO;
}

static int parse_opencl_kernel(const char *text, int fallback) {
    if (text == NULL) {
        return fallback;
    }
    if (strcmp(text, "compact") == 0) {
        return MINER_OPENCL_KERNEL_COMPACT;
    }
    if (strcmp(text, "unrolled") == 0) {
        return MINER_OPENCL_KERNEL_UNROLLED;
    }
    if (strcmp(text, "fixed-npi1") == 0) {
        return MINER_OPENCL_KERNEL_FIXED_NPI1;
    }
    if (strcmp(text, "fixed-npi2") == 0) {
        return MINER_OPENCL_KERNEL_FIXED_NPI2;
    }
    if (strcmp(text, "fixed-npi4") == 0) {
        return MINER_OPENCL_KERNEL_FIXED_NPI4;
    }
    if (strcmp(text, "register-heavy") == 0) {
        return MINER_OPENCL_KERNEL_REGISTER_HEAVY;
    }
    if (strcmp(text, "legacy-noatomic") == 0) {
        return MINER_OPENCL_KERNEL_LEGACY_NOATOMIC;
    }
    return MINER_OPENCL_KERNEL_AUTO;
}

static void parse_opencl_device(json_t *device, miner_opencl_device_config_t *dst, const miner_opencl_config_t *base) {
    memset(dst, 0, sizeof(*dst));
    dst->platform = json_int_or(json_object_get(device, "platform"), base->platform);
    dst->device = json_int_or(json_object_get(device, "device"), base->device);
    dst->batch_size = json_u32_or(json_object_get(device, "batch-size"), base->batch_size);
    dst->batch_size = json_u32_or(json_object_get(device, "batch_size"), dst->batch_size);
    dst->local_work_size = json_u32_or(json_object_get(device, "local-work-size"), base->local_work_size);
    dst->local_work_size = json_u32_or(json_object_get(device, "local_work_size"), dst->local_work_size);
    dst->nonces_per_work_item = json_u32_or(json_object_get(device, "nonces-per-work-item"), base->nonces_per_work_item);
    dst->nonces_per_work_item = json_u32_or(json_object_get(device, "nonces_per_work_item"), dst->nonces_per_work_item);
    dst->nonces_per_work_item = json_u32_or(json_object_get(device, "npi"), dst->nonces_per_work_item);
    dst->max_results = json_u32_or(json_object_get(device, "max-results"), base->max_results);
    dst->max_results = json_u32_or(json_object_get(device, "max_results"), dst->max_results);
    dst->backend_variant = parse_opencl_backend(json_string_value(json_object_get(device, "backend")), base->backend_variant);
    dst->backend_variant = parse_opencl_backend(json_string_value(json_object_get(device, "backend-variant")), dst->backend_variant);
    dst->backend_variant = parse_opencl_backend(json_string_value(json_object_get(device, "backend_variant")), dst->backend_variant);
    dst->kernel_variant = parse_opencl_kernel(json_string_value(json_object_get(device, "kernel")), base->kernel_variant);
    dst->kernel_variant = parse_opencl_kernel(json_string_value(json_object_get(device, "kernel-variant")), dst->kernel_variant);
    dst->kernel_variant = parse_opencl_kernel(json_string_value(json_object_get(device, "kernel_variant")), dst->kernel_variant);
}

static core_config_t read_config(const char *path) {
    core_config_t config;
    memset(&config, 0, sizeof(config));
    copy_text(config.pass, sizeof(config.pass), "x");
    config.cpu_enabled = 1;
    config.cpu_threads = 0;
    config.retries = -1;
    config.retry_pause = 2;
    config.stats_interval = 10.0;
    miner_opencl_config_defaults(&config.opencl);
    config.opencl.enabled = 1;

    if (path == NULL || path[0] == '\0') {
        return config;
    }

    json_error_t error;
    json_t *root = json_load_file(path, 0, &error);
    if (root == NULL) {
        set_status("bad config", error.text);
        return config;
    }

    json_copy_string(json_object_get(root, "pool"), config.pool, sizeof(config.pool));
    json_copy_string(json_object_get(root, "user"), config.user, sizeof(config.user));
    json_copy_string(json_object_get(root, "pass"), config.pass, sizeof(config.pass));
    config.cpu_threads = json_int_or(json_object_get(root, "cpu_threads"), config.cpu_threads);
    config.retries = json_int_or(json_object_get(root, "retries"), config.retries);
    config.retry_pause = json_int_or(json_object_get(root, "retry-pause"), config.retry_pause);
    config.stats_interval = json_number_or(json_object_get(root, "print-time"), config.stats_interval);

    json_t *cpu = json_object_get(root, "cpu");
    if (json_is_object(cpu)) {
        config.cpu_enabled = json_bool_or(json_object_get(cpu, "enabled"), config.cpu_enabled);
        config.cpu_threads = json_int_or(json_object_get(cpu, "threads"), config.cpu_threads);
        config.cpu_affinity = json_bool_or(json_object_get(cpu, "affinity"), config.cpu_affinity);
    }

    json_t *opencl = json_object_get(root, "opencl");
    if (json_is_object(opencl)) {
        config.opencl.enabled = json_bool_or(json_object_get(opencl, "enabled"), config.opencl.enabled);
        config.opencl.all_devices = json_bool_or(json_object_get(opencl, "all-devices"), config.opencl.all_devices);
        config.opencl.all_devices = json_bool_or(json_object_get(opencl, "all_devices"), config.opencl.all_devices);
        config.opencl.platform = json_int_or(json_object_get(opencl, "platform"), config.opencl.platform);
        config.opencl.device = json_int_or(json_object_get(opencl, "device"), config.opencl.device);
        config.opencl.batch_size = json_u32_or(json_object_get(opencl, "batch-size"), config.opencl.batch_size);
        config.opencl.batch_size = json_u32_or(json_object_get(opencl, "batch_size"), config.opencl.batch_size);
        config.opencl.local_work_size = json_u32_or(json_object_get(opencl, "local-work-size"), config.opencl.local_work_size);
        config.opencl.local_work_size = json_u32_or(json_object_get(opencl, "local_work_size"), config.opencl.local_work_size);
        config.opencl.nonces_per_work_item = json_u32_or(json_object_get(opencl, "nonces-per-work-item"), config.opencl.nonces_per_work_item);
        config.opencl.nonces_per_work_item = json_u32_or(json_object_get(opencl, "nonces_per_work_item"), config.opencl.nonces_per_work_item);
        config.opencl.nonces_per_work_item = json_u32_or(json_object_get(opencl, "npi"), config.opencl.nonces_per_work_item);
        config.opencl.max_results = json_u32_or(json_object_get(opencl, "max-results"), config.opencl.max_results);
        config.opencl.max_results = json_u32_or(json_object_get(opencl, "max_results"), config.opencl.max_results);
        config.opencl.backend_variant = parse_opencl_backend(json_string_value(json_object_get(opencl, "backend")), config.opencl.backend_variant);
        config.opencl.kernel_variant = parse_opencl_kernel(json_string_value(json_object_get(opencl, "kernel")), config.opencl.kernel_variant);

        json_t *devices = json_object_get(opencl, "devices");
        if (json_is_array(devices)) {
            config.opencl.device_count = 0;
            config.opencl.all_devices = 0;
            size_t index;
            json_t *device;
            json_array_foreach(devices, index, device) {
                if (json_is_object(device) && config.opencl.device_count < MINER_OPENCL_MAX_DEVICES) {
                    parse_opencl_device(device, &config.opencl.devices[config.opencl.device_count++], &config.opencl);
                }
            }
        }
    }

    json_t *pools = json_object_get(root, "pools");
    if (json_is_array(pools) && json_array_size(pools) > 0) {
        json_t *pool = json_array_get(pools, 0);
        if (json_is_object(pool)) {
            json_copy_string(json_object_get(pool, "url"), config.pool, sizeof(config.pool));
            json_copy_string(json_object_get(pool, "user"), config.user, sizeof(config.user));
            json_copy_string(json_object_get(pool, "pass"), config.pass, sizeof(config.pass));
            config.difficulty = json_number_or(json_object_get(pool, "diff"), config.difficulty);
            config.difficulty = json_number_or(json_object_get(pool, "difficulty"), config.difficulty);
        }
    }

    json_decref(root);
    if (config.cpu_threads <= 0 && config.cpu_enabled) {
        config.cpu_threads = available_processors();
    }
    if (!config.cpu_enabled) {
        config.cpu_threads = 0;
    }
    if (config.retry_pause < 1) {
        config.retry_pause = 1;
    }
    return config;
}

static void *run_core(void *opaque) {
    (void)opaque;
    int attempt = 0;

    for (;;) {
        if (g_stop_requested) {
            break;
        }
        if (g_config.pool[0] == '\0') {
            set_status("no pool configured", "");
            break;
        }
        if (g_config.retries >= 0 && attempt > g_config.retries) {
            set_status("retry limit reached", "");
            break;
        }

        ++attempt;
        set_status("running full core", "");

        stratum_client_config_t client;
        memset(&client, 0, sizeof(client));
        client.thread_count = g_config.cpu_threads;
        client.cpu_affinity = g_config.cpu_affinity;
        client.enable_mining = 1;
        client.opencl = g_config.opencl;
        client.stats_interval = g_config.stats_interval;

        int rc = stratum_run_client(g_config.pool, g_config.user, g_config.pass, g_config.difficulty, &client);
        if (g_stop_requested) {
            break;
        }
        char error[128];
        snprintf(error, sizeof(error), "core returned %d", rc);
        set_status("reconnecting", error);
        for (int i = 0; i < g_config.retry_pause && !g_stop_requested; ++i) {
            sleep(1);
        }
    }

    pthread_mutex_lock(&g_lock);
    g_running = 0;
    copy_text(g_status, sizeof(g_status), "stopped");
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

const char *btcrig_core_backend_name(void) {
    return sha256d_backend_name(sha256d_get_backend());
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
    sha256d_set_backend(sha256d_auto_backend());
    sha256d_80(header, out);
    return memcmp(out, expected, sizeof(expected)) == 0;
}

int btcrig_core_start(const char *config_path) {
    core_config_t config = read_config(config_path);

    pthread_mutex_lock(&g_lock);
    if (g_running) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    g_stop_requested = 0;
    g_config = config;
    copy_text(g_status, sizeof(g_status), "starting");
    copy_text(g_last_error, sizeof(g_last_error), "");
    g_running = 1;
    pthread_mutex_unlock(&g_lock);

    sha256d_set_backend(sha256d_auto_backend());
    if (pthread_create(&g_thread, NULL, run_core, NULL) != 0) {
        pthread_mutex_lock(&g_lock);
        g_running = 0;
        copy_text(g_status, sizeof(g_status), "thread failed");
        copy_text(g_last_error, sizeof(g_last_error), "thread failed");
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    g_thread_started = 1;
    return 1;
}

void btcrig_core_stop(void) {
    pthread_mutex_lock(&g_lock);
    int started = g_thread_started;
    g_stop_requested = 1;
    pthread_mutex_unlock(&g_lock);

    if (started) {
        pthread_join(g_thread, NULL);
    }

    pthread_mutex_lock(&g_lock);
    g_thread_started = 0;
    g_running = 0;
    copy_text(g_status, sizeof(g_status), "stopped");
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
    int workers = g_config.cpu_threads + (g_config.opencl.enabled ? 1 : 0);
    pthread_mutex_unlock(&g_lock);
    return workers;
}

uint64_t btcrig_core_total_hashes(void) {
    return 0;
}

double btcrig_core_hashrate(void) {
    return 0.0;
}

int btcrig_core_stratum_connected(void) {
    return btcrig_core_is_running();
}

uint64_t btcrig_core_stratum_jobs(void) {
    return 0;
}

uint64_t btcrig_core_stratum_submits(void) {
    return 0;
}

uint64_t btcrig_core_stratum_accepts(void) {
    return 0;
}

uint64_t btcrig_core_stratum_rejects(void) {
    return 0;
}

void btcrig_core_copy_pool(char *out, size_t out_size) {
    pthread_mutex_lock(&g_lock);
    copy_text(out, out_size, g_config.pool);
    pthread_mutex_unlock(&g_lock);
}

void btcrig_core_copy_stratum_status(char *out, size_t out_size) {
    pthread_mutex_lock(&g_lock);
    copy_text(out, out_size, g_status);
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
    }
    if (threads < 1) {
        threads = 1;
    }

    miner_t *miner = miner_create(threads);
    if (miner == NULL || miner_start(miner) != 0) {
        miner_destroy(miner);
        return 0.0;
    }

    miner_job_t job;
    memset(&job, 0, sizeof(job));
    copy_text(job.job_id, sizeof(job.job_id), "bench");
    copy_text(job.extranonce2, sizeof(job.extranonce2), "00000000");
    copy_text(job.ntime, sizeof(job.ntime), "00000000");
    memset(job.target, 0xff, sizeof(job.target));
    miner_set_job(miner, &job);

    struct timespec ts;
    ts.tv_sec = seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    uint64_t hashes = miner_hashes(miner);
    miner_destroy(miner);
    return (double)hashes / (double)seconds;
}
