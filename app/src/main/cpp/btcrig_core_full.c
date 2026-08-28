#include "btcrig_core.h"

#include "miner.h"
#include "opencl_miner.h"
#include "sha256d.h"
#include "stratum.h"

#include <jansson.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CORE_LOG_MAX_BYTES (1024 * 1024)
#define BENCHMARK_DIFFICULTY 100000.0
#define DONATION_CYCLE_MINUTES 100
#define DONATION_USER "bc1qqz0wutk9kk5mmaf7fu4dm5w4fq4fhaah9hpzr3"

typedef struct {
    char pool[256];
    char user[256];
    char donation_user[256];
    char pass[128];
    int cpu_enabled;
    int cpu_threads;
    int cpu_affinity;
    int retries;
    int retry_pause;
    int tls_compat;
    int donation_percent;
    double difficulty;
    double stats_interval;
    miner_opencl_config_t opencl;
} core_config_t;

typedef struct {
    uint64_t hashes;
    uint64_t jobs;
    uint64_t submits;
    uint64_t accepts;
    uint64_t rejects;
    uint64_t base_hashes;
    uint64_t base_jobs;
    uint64_t base_submits;
    uint64_t base_accepts;
    uint64_t base_rejects;
    uint64_t conn_hashes;
    uint64_t conn_jobs;
    uint64_t conn_submits;
    uint64_t conn_accepts;
    uint64_t conn_rejects;
    double hashrate;
    double last_time;
    uint64_t last_hashes;
    int worker_count;
    int connected;
} core_stats_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_thread_started = 0;
static int g_running = 0;
static volatile int g_stop_requested = 0;
static core_config_t g_config;
static core_stats_t g_stats;
static char g_status[64] = "idle";
static char g_last_error[128] = "";
static char g_log_path[512] = "";
static int g_log_redirected = 0;

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

static int donation_level_or_zero(int level) {
    if (level <= 0) {
        return 0;
    }
    return level >= DONATION_CYCLE_MINUTES ? DONATION_CYCLE_MINUTES - 1 : level;
}

static double donation_phase_seconds(int level, int donating) {
    int minutes = donating ? level : DONATION_CYCLE_MINUTES - level;
    return (double)minutes * 60.0;
}

static double donation_initial_user_seconds(int level) {
    uint64_t seed = (uint64_t)time(NULL);
    seed ^= (uint64_t)getpid() << 32;
    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;
    double unit = (double)((seed * UINT64_C(2685821657736338717)) >> 11) * (1.0 / 9007199254740992.0);
    return donation_phase_seconds(level, 0) * (0.5 + unit);
}

static void set_status(const char *status, const char *error) {
    pthread_mutex_lock(&g_lock);
    copy_text(g_status, sizeof(g_status), status);
    if (error != NULL) {
        copy_text(g_last_error, sizeof(g_last_error), error);
    }
    pthread_mutex_unlock(&g_lock);
}

static void build_log_path(const char *config_path, char *out, size_t out_size) {
    if (config_path == NULL || config_path[0] == '\0') {
        copy_text(out, out_size, "btcrig.log");
        return;
    }

    const char *slash = strrchr(config_path, '/');
    if (slash == NULL) {
        copy_text(out, out_size, "btcrig.log");
        return;
    }

    size_t dir_len = (size_t)(slash - config_path);
    if (dir_len + strlen("/btcrig.log") + 1 > out_size) {
        copy_text(out, out_size, "btcrig.log");
        return;
    }
    memcpy(out, config_path, dir_len);
    memcpy(out + dir_len, "/btcrig.log", strlen("/btcrig.log") + 1);
}

static void redirect_native_log(const char *config_path) {
    pthread_mutex_lock(&g_lock);
    if (g_log_redirected) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    build_log_path(config_path, g_log_path, sizeof(g_log_path));
    char path[sizeof(g_log_path)];
    copy_text(path, sizeof(path), g_log_path);
    g_log_redirected = 1;
    pthread_mutex_unlock(&g_lock);

    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > CORE_LOG_MAX_BYTES) {
        char rotated[sizeof(g_log_path) + 2];
        snprintf(rotated, sizeof(rotated), "%s.1", path);
        rename(path, rotated);
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_last_error, sizeof(g_last_error), "log open failed: %s", strerror(errno));
        pthread_mutex_unlock(&g_lock);
        return;
    }
    // ponytail: process-wide redirect; replace with logger callbacks if native libraries need separate sinks.
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

static void reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

static void update_stats(void *opaque, const stratum_snapshot_t *snapshot) {
    (void)opaque;
    if (snapshot == NULL) {
        return;
    }

    pthread_mutex_lock(&g_lock);
    if (snapshot->hashes < g_stats.conn_hashes ||
        snapshot->jobs < g_stats.conn_jobs ||
        snapshot->submits < g_stats.conn_submits ||
        snapshot->accepts < g_stats.conn_accepts ||
        snapshot->rejects < g_stats.conn_rejects) {
        g_stats.base_hashes += g_stats.conn_hashes;
        g_stats.base_jobs += g_stats.conn_jobs;
        g_stats.base_submits += g_stats.conn_submits;
        g_stats.base_accepts += g_stats.conn_accepts;
        g_stats.base_rejects += g_stats.conn_rejects;
        g_stats.last_time = 0.0;
        g_stats.last_hashes = 0;
    }

    if (g_stats.last_time <= 0.0) {
        g_stats.last_time = snapshot->now;
        g_stats.last_hashes = snapshot->hashes;
    } else if (snapshot->hashes > g_stats.last_hashes && snapshot->now > g_stats.last_time) {
        g_stats.hashrate = (double)(snapshot->hashes - g_stats.last_hashes) / (snapshot->now - g_stats.last_time);
        g_stats.last_time = snapshot->now;
        g_stats.last_hashes = snapshot->hashes;
    } else if (snapshot->now - g_stats.last_time > 30.0) {
        g_stats.hashrate = 0.0;
    }
    g_stats.conn_hashes = snapshot->hashes;
    g_stats.conn_jobs = snapshot->jobs;
    g_stats.conn_submits = snapshot->submits;
    g_stats.conn_accepts = snapshot->accepts;
    g_stats.conn_rejects = snapshot->rejects;
    g_stats.hashes = g_stats.base_hashes + g_stats.conn_hashes;
    g_stats.worker_count = snapshot->worker_count;
    g_stats.connected = snapshot->connected && snapshot->authorized;
    g_stats.jobs = g_stats.base_jobs + g_stats.conn_jobs;
    g_stats.submits = g_stats.base_submits + g_stats.conn_submits;
    g_stats.accepts = g_stats.base_accepts + g_stats.conn_accepts;
    g_stats.rejects = g_stats.base_rejects + g_stats.conn_rejects;
    if (snapshot->authorized) {
        copy_text(g_status, sizeof(g_status), "authorized");
    } else if (snapshot->subscribed) {
        copy_text(g_status, sizeof(g_status), "subscribed");
    } else if (snapshot->connected) {
        copy_text(g_status, sizeof(g_status), "connected");
    }
    pthread_mutex_unlock(&g_lock);
}

static void mark_disconnected(void) {
    pthread_mutex_lock(&g_lock);
    g_stats.connected = 0;
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
    copy_text(config.donation_user, sizeof(config.donation_user), DONATION_USER);
    config.cpu_enabled = 1;
    config.cpu_threads = 0;
    config.retries = -1;
    config.retry_pause = 2;
    config.tls_compat = 1;
    config.donation_percent = 1;
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
    config.tls_compat = json_bool_or(json_object_get(root, "tls_compat"), config.tls_compat);
    config.donation_percent = json_int_or(json_object_get(root, "donate-level"), config.donation_percent);
    config.donation_percent = json_int_or(json_object_get(root, "donation_percent"), config.donation_percent);
    config.stats_interval = json_number_or(json_object_get(root, "print-time"), config.stats_interval);

    json_t *tls = json_object_get(root, "tls");
    if (json_is_object(tls)) {
        config.tls_compat = json_bool_or(json_object_get(tls, "compat"), config.tls_compat);
    }

    json_t *donation = json_object_get(root, "donation");
    if (json_is_object(donation)) {
        config.donation_percent = json_int_or(json_object_get(donation, "percent"), config.donation_percent);
        config.donation_percent = json_int_or(json_object_get(donation, "level"), config.donation_percent);
        json_copy_string(json_object_get(donation, "address"), config.donation_user, sizeof(config.donation_user));
        json_copy_string(json_object_get(donation, "user"), config.donation_user, sizeof(config.donation_user));
        json_copy_string(json_object_get(donation, "wallet"), config.donation_user, sizeof(config.donation_user));
    }

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
    config.donation_percent = donation_level_or_zero(config.donation_percent);
    return config;
}

static void *run_core(void *opaque) {
    (void)opaque;
    int attempt = 0;
    int donating = 0;
    int donation_enabled = g_config.donation_percent > 0 &&
        g_config.donation_user[0] != '\0' &&
        strcmp(g_config.donation_user, g_config.user) != 0;
    double phase_seconds = donation_enabled ? donation_initial_user_seconds(g_config.donation_percent) : 0.0;

    if (donation_enabled) {
        printf("[DONATE] level=%d%% address=%s pool=same-as-user\n", g_config.donation_percent, g_config.donation_user);
    }

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
        const char *run_user = donating ? g_config.donation_user : g_config.user;

        stratum_client_config_t client;
        memset(&client, 0, sizeof(client));
        client.thread_count = g_config.cpu_threads;
        client.cpu_affinity = g_config.cpu_affinity;
        client.enable_mining = 1;
        client.opencl = g_config.opencl;
        client.tls_compat = g_config.tls_compat;
        client.stats_interval = g_config.stats_interval;
        client.session_seconds = phase_seconds;
        client.session_label = donating ? "donate" : "user";
        client.on_stats = update_stats;

        int rc = stratum_run_client(g_config.pool, run_user, g_config.pass, g_config.difficulty, &client);
        mark_disconnected();
        if (g_stop_requested) {
            break;
        }
        if (donation_enabled && rc == 0) {
            donating = !donating;
            phase_seconds = donation_phase_seconds(g_config.donation_percent, donating);
            attempt = 0;
            continue;
        }
        if (donating) {
            donating = 0;
            phase_seconds = donation_phase_seconds(g_config.donation_percent, 0);
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
    redirect_native_log(config_path);
    core_config_t config = read_config(config_path);

    pthread_mutex_lock(&g_lock);
    if (g_running) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    g_stop_requested = 0;
    g_config = config;
    reset_stats();
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
    g_stats.connected = 0;
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
    int workers = g_stats.worker_count > 0 ? g_stats.worker_count : g_config.cpu_threads;
    pthread_mutex_unlock(&g_lock);
    return workers;
}

uint64_t btcrig_core_total_hashes(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t hashes = g_stats.hashes;
    pthread_mutex_unlock(&g_lock);
    return hashes;
}

double btcrig_core_hashrate(void) {
    pthread_mutex_lock(&g_lock);
    double hashrate = g_stats.hashrate;
    pthread_mutex_unlock(&g_lock);
    return hashrate;
}

int btcrig_core_stratum_connected(void) {
    pthread_mutex_lock(&g_lock);
    int connected = g_stats.connected;
    pthread_mutex_unlock(&g_lock);
    return connected;
}

uint64_t btcrig_core_stratum_jobs(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t jobs = g_stats.jobs;
    pthread_mutex_unlock(&g_lock);
    return jobs;
}

uint64_t btcrig_core_stratum_submits(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t submits = g_stats.submits;
    pthread_mutex_unlock(&g_lock);
    return submits;
}

uint64_t btcrig_core_stratum_accepts(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t accepts = g_stats.accepts;
    pthread_mutex_unlock(&g_lock);
    return accepts;
}

uint64_t btcrig_core_stratum_rejects(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t rejects = g_stats.rejects;
    pthread_mutex_unlock(&g_lock);
    return rejects;
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

void btcrig_core_copy_opencl_status(const char *config_path, char *out, size_t out_size) {
#if defined(BTC_MINER_OPENCL)
    core_config_t config = read_config(config_path);
    opencl_miner_describe_devices(&config.opencl, out, out_size);
#else
    (void)config_path;
    copy_text(out, out_size, "Config: unavailable\nRuntime: not built\nMode: CPU only");
#endif
}

static void prepare_benchmark_job(miner_job_t *job) {
    memset(job, 0, sizeof(*job));
    copy_text(job->job_id, sizeof(job->job_id), "bench");
    copy_text(job->extranonce2, sizeof(job->extranonce2), "00000000");
    copy_text(job->ntime, sizeof(job->ntime), "00000000");
    miner_target_from_difficulty(BENCHMARK_DIFFICULTY, job->target);
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
    prepare_benchmark_job(&job);
    miner_set_job(miner, &job);

    struct timespec ts;
    ts.tv_sec = seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    uint64_t hashes = miner_hashes(miner);
    miner_destroy(miner);
    return (double)hashes / (double)seconds;
}

double btcrig_core_benchmark_cpu_backend(const char *backend, int seconds, int threads) {
    sha256d_backend_t requested;
    if (backend == NULL ||
        sha256d_parse_backend(backend, &requested) != 0 ||
        !sha256d_backend_available(requested) ||
        btcrig_core_is_running()) {
        return -1.0;
    }

    sha256d_backend_t previous = sha256d_get_backend();
    if (sha256d_set_backend(requested) != 0) {
        return -1.0;
    }
    double hps = btcrig_core_benchmark_cpu(seconds, threads);
    (void)sha256d_set_backend(previous);
    return hps;
}

double btcrig_core_benchmark_opencl(const char *config_path, int seconds) {
#if defined(BTC_MINER_OPENCL)
    if (btcrig_core_is_running()) {
        return -1.0;
    }
    if (seconds < 1) {
        seconds = 1;
    }

    core_config_t config = read_config(config_path);
    config.opencl.enabled = 1;
    miner_t *miner = miner_create_with_backend_options(0, &config.opencl, NULL);
    if (miner == NULL || miner_start(miner) != 0) {
        miner_destroy(miner);
        return -1.0;
    }

    miner_job_t job;
    prepare_benchmark_job(&job);
    miner_set_job(miner, &job);

    struct timespec ts;
    ts.tv_sec = seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

    uint64_t hashes = miner_hashes(miner);
    miner_destroy(miner);
    return (double)hashes / (double)seconds;
#else
    (void)config_path;
    (void)seconds;
    return -1.0;
#endif
}
