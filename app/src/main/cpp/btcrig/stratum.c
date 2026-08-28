#define _POSIX_C_SOURCE 200809L

#include "stratum.h"

#include "btcrig_android_stop.h"
#include "btcrig_version.h"
#include "console.h"
#include "miner.h"

#include <errno.h>
#include <jansson.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#endif

typedef struct {
    char host[256];
    char port[16];
    int use_tls;
    int verify_tls;
} pool_endpoint_t;

typedef struct {
    int fd;
    int use_tls;
    SSL_CTX *ctx;
    SSL *ssl;
} stratum_conn_t;

typedef struct {
    stratum_conn_t *conn;
    char buffer[65536];
    size_t used;
} line_reader_t;

typedef struct {
    int enabled;
#if !defined(_WIN32)
    struct termios original;
#endif
} keyboard_input_t;

static double monotonic_seconds(void);
static int rebuild_miner_job(stratum_state_t *state, const char *reason);

static int verbose_shares_enabled(void) {
    static int cached = -1;
    const char *value = NULL;

    if (cached >= 0) {
        return cached;
    }

    value = getenv("BTC_MINER_VERBOSE_SHARES");
    cached = value == NULL ||
             value[0] == '\0' ||
             (strcmp(value, "0") != 0 &&
              strcmp(value, "false") != 0 &&
              strcmp(value, "off") != 0);
    return cached;
}

static int network_init(void) {
#if defined(_WIN32)
    static int initialized = 0;
    static int failed = 0;
    if (initialized) {
        return failed ? -1 : 0;
    }

    WSADATA data;
    int rc = WSAStartup(MAKEWORD(2, 2), &data);
    initialized = 1;
    failed = rc != 0;
    if (failed) {
        fprintf(stderr, "%s[NET]%s WSAStartup failed: %d\n", C_BRIGHT_RED, C_RESET, rc);
        return -1;
    }
#else
    static int initialized = 0;
    if (!initialized) {
        signal(SIGPIPE, SIG_IGN);
        initialized = 1;
    }
#endif
    return 0;
}

static void socket_close_fd(int fd) {
    if (fd < 0) {
        return;
    }
#if defined(_WIN32)
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
}

static int socket_interrupted_or_would_block(void) {
#if defined(_WIN32)
    int err = WSAGetLastError();
    return err == WSAEINTR || err == WSAEWOULDBLOCK;
#else
    return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void stratum_state_init(stratum_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->difficulty = 1.0;
    state->started_at = monotonic_seconds();
    state->extranonce2_size = 4;
    state->extranonce2_counter = 1;
}

static const char *json_string_or_empty(json_t *value) {
    const char *text = json_string_value(value);
    return text != NULL ? text : "";
}

static int copy_checked(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(src);
    if (len >= dst_size) {
        return -1;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

static int parse_pool_url(const char *url, pool_endpoint_t *endpoint) {
    const char *p = url;
    char hostport[320];
    char *colon = NULL;
    char *slash = NULL;

    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->verify_tls = 1;

    if (strncmp(p, "stratum+tcp://", 14) == 0) {
        p += 14;
    } else if (strncmp(p, "stratum+tls-insecure://", 23) == 0 ||
               strncmp(p, "stratum+ssl-insecure://", 23) == 0) {
        endpoint->use_tls = 1;
        endpoint->verify_tls = 0;
        p += 23;
    } else if (strncmp(p, "stratum+tls://", 14) == 0) {
        endpoint->use_tls = 1;
        p += 14;
    } else if (strncmp(p, "stratum+ssl://", 14) == 0) {
        endpoint->use_tls = 1;
        p += 14;
    } else if (strncmp(p, "tcp://", 6) == 0) {
        p += 6;
    } else if (strncmp(p, "tls-insecure://", 15) == 0 ||
               strncmp(p, "ssl-insecure://", 15) == 0) {
        endpoint->use_tls = 1;
        endpoint->verify_tls = 0;
        p += 15;
    } else if (strncmp(p, "tls://", 6) == 0 || strncmp(p, "ssl://", 6) == 0) {
        endpoint->use_tls = 1;
        p += 6;
    }

    if (*p == '\0') {
        return -1;
    }

    snprintf(hostport, sizeof(hostport), "%s", p);
    slash = strchr(hostport, '/');
    if (slash != NULL) {
        *slash = '\0';
    }

    colon = strrchr(hostport, ':');
    if (colon == NULL) {
        if (copy_checked(endpoint->host, sizeof(endpoint->host), hostport) != 0 ||
            copy_checked(endpoint->port, sizeof(endpoint->port), endpoint->use_tls ? "4333" : "3333") != 0) {
            return -1;
        }
        return endpoint->host[0] == '\0' ? -1 : 0;
    }

    *colon = '\0';
    if (copy_checked(endpoint->host, sizeof(endpoint->host), hostport) != 0 ||
        copy_checked(endpoint->port, sizeof(endpoint->port), colon + 1) != 0) {
        return -1;
    }
    return endpoint->host[0] == '\0' || endpoint->port[0] == '\0' ? -1 : 0;
}

static int connect_tcp(const pool_endpoint_t *endpoint) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int fd = -1;

    if (network_init() != 0) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(endpoint->host, endpoint->port, &hints, &result);
    if (rc != 0) {
        fprintf(stderr, "%s[NET]%s getaddrinfo failed: %s\n",
                C_BRIGHT_RED, C_RESET, gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        socket_close_fd(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

static void conn_close(stratum_conn_t *conn) {
    if (conn == NULL) {
        return;
    }

    if (conn->ssl != NULL) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->ctx != NULL) {
        SSL_CTX_free(conn->ctx);
        conn->ctx = NULL;
    }
    if (conn->fd >= 0) {
        socket_close_fd(conn->fd);
        conn->fd = -1;
    }
    conn->use_tls = 0;
}

static int conn_open_once(stratum_conn_t *conn,
                          const pool_endpoint_t *endpoint,
                          int verify_tls,
                          int *tls_handshake_failed) {
    if (tls_handshake_failed != NULL) {
        *tls_handshake_failed = 0;
    }

    memset(conn, 0, sizeof(*conn));
    conn->fd = -1;
    conn->use_tls = endpoint->use_tls;

    conn->fd = connect_tcp(endpoint);
    if (conn->fd < 0) {
        return -1;
    }

    if (!endpoint->use_tls) {
        return 0;
    }

    conn->ctx = SSL_CTX_new(TLS_client_method());
    if (conn->ctx == NULL) {
        fprintf(stderr, "%s[TLS]%s SSL_CTX_new failed\n", C_BRIGHT_RED, C_RESET);
        conn_close(conn);
        return -1;
    }

    if (verify_tls) {
        if (SSL_CTX_set_default_verify_paths(conn->ctx) != 1) {
            fprintf(stderr, "%s[TLS]%s default CA paths unavailable, certificate verification may fail\n",
                    C_YELLOW, C_RESET);
        }
        SSL_CTX_set_verify(conn->ctx, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_CTX_set_verify(conn->ctx, SSL_VERIFY_NONE, NULL);
    }

    conn->ssl = SSL_new(conn->ctx);
    if (conn->ssl == NULL) {
        fprintf(stderr, "%s[TLS]%s SSL_new failed\n", C_BRIGHT_RED, C_RESET);
        conn_close(conn);
        return -1;
    }

    (void)SSL_set_tlsext_host_name(conn->ssl, endpoint->host);
    if (verify_tls) {
        (void)SSL_set1_host(conn->ssl, endpoint->host);
    }
    SSL_set_fd(conn->ssl, conn->fd);

    if (SSL_connect(conn->ssl) != 1) {
        unsigned long err = ERR_get_error();
        if (tls_handshake_failed != NULL) {
            *tls_handshake_failed = 1;
        }
        fprintf(stderr, "%s[TLS]%s handshake failed: %s\n",
                C_BRIGHT_RED, C_RESET, err != 0 ? ERR_error_string(err, NULL) : "unknown");
        conn_close(conn);
        return -1;
    }

    printf("%s[TLS]%s %shandshake ok%s verify=%s%s%s cipher=%s\n",
           C_CYAN, C_RESET,
           C_BRIGHT_GREEN, C_RESET,
           verify_tls ? C_BRIGHT_GREEN : C_YELLOW,
           verify_tls ? "on" : "off",
           C_RESET,
           SSL_get_cipher(conn->ssl));
    return 0;
}

static int conn_open(stratum_conn_t *conn, const pool_endpoint_t *endpoint, int tls_compat) {
    int tls_handshake_failed = 0;

    if (conn_open_once(conn, endpoint, endpoint->verify_tls, &tls_handshake_failed) == 0) {
        return 0;
    }

    if (endpoint->use_tls && endpoint->verify_tls && tls_compat && tls_handshake_failed) {
        fprintf(stderr,
                "%s[TLS]%s verified handshake failed, retrying compatible mode verify=off sni=on\n",
                C_YELLOW, C_RESET);
        if (conn_open_once(conn, endpoint, 0, NULL) == 0) {
            return 0;
        }
    }

    if (endpoint->use_tls) {
        pool_endpoint_t plain = *endpoint;
        plain.use_tls = 0;
        plain.verify_tls = 0;
        fprintf(stderr,
                "%s[TLS]%s TLS unavailable, retrying same endpoint as plain TCP\n",
                C_YELLOW, C_RESET);
        return conn_open_once(conn, &plain, 0, NULL);
    }
    return -1;
}

static int conn_write(stratum_conn_t *conn, const char *data, size_t len) {
    if (conn == NULL) {
        return -1;
    }

    if (conn->use_tls) {
        while (len > 0) {
            int n = SSL_write(conn->ssl, data, (int)len);
            if (n <= 0) {
                int err = SSL_get_error(conn->ssl, n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    continue;
                }
                return -1;
            }
            data += n;
            len -= (size_t)n;
        }
        return 0;
    }

    while (len > 0) {
        int n = send(conn->fd, data, (int)len, 0);
        if (n < 0) {
            if (socket_interrupted_or_would_block()) {
                continue;
            }
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static int conn_read(stratum_conn_t *conn, char *data, size_t len) {
    if (conn == NULL) {
        return -1;
    }

    if (conn->use_tls) {
        int n = SSL_read(conn->ssl, data, (int)len);
        if (n <= 0) {
            int err = SSL_get_error(conn->ssl, n);
            if (err == SSL_ERROR_ZERO_RETURN) {
                return 0;
            }
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                return -3;
            }
            return -1;
        }
        return n;
    }

    int n = recv(conn->fd, data, (int)len, 0);
    if (n == 0) {
        return 0;
    }
    if (n < 0) {
        if (socket_interrupted_or_would_block()) {
            return -3;
        }
        return -1;
    }
    return (int)n;
}

static int conn_pending(stratum_conn_t *conn) {
    return conn != NULL && conn->use_tls && conn->ssl != NULL ? SSL_pending(conn->ssl) : 0;
}

static int send_request(stratum_conn_t *conn, int id, const char *method, json_t *params) {
    json_t *root = json_object();
    char *line = NULL;
    int rc = -1;

    if (root == NULL) {
        json_decref(params);
        return -1;
    }

    json_object_set_new(root, "id", json_integer(id));
    json_object_set_new(root, "method", json_string(method));
    json_object_set_new(root, "params", params);

    line = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (line == NULL) {
        return -1;
    }

    if (strcmp(method, "mining.submit") != 0 || verbose_shares_enabled()) {
        printf("%s[SEND]%s id=%d method=%s%s%s\n",
               C_GRAY, C_RESET, id, C_CYAN, method, C_RESET);
    }
    if (conn_write(conn, line, strlen(line)) == 0 && conn_write(conn, "\n", 1) == 0) {
        rc = 0;
    }
    free(line);
    return rc;
}

static int send_subscribe(stratum_conn_t *conn) {
    json_t *params = json_array();
    json_array_append_new(params, json_string(BTCRIG_USER_AGENT));
    return send_request(conn, 1, "mining.subscribe", params);
}

static int send_authorize(stratum_conn_t *conn, const char *user, const char *password) {
    json_t *params = json_array();
    json_array_append_new(params, json_string(user));
    json_array_append_new(params, json_string(password));
    return send_request(conn, 2, "mining.authorize", params);
}

static int send_suggest_difficulty(stratum_conn_t *conn, double difficulty) {
    json_t *params = json_array();
    json_array_append_new(params, json_real(difficulty));
    printf("%s[DIFF]%s suggesting %s%.8f%s\n",
           C_YELLOW, C_RESET, C_BRIGHT_YELLOW, difficulty, C_RESET);
    return send_request(conn, 3, "mining.suggest_difficulty", params);
}

static int send_submit_share(stratum_conn_t *conn, int id, const char *user, const miner_share_t *share) {
    char nonce_hex[9];
    json_t *params = json_array();

    snprintf(nonce_hex, sizeof(nonce_hex), "%08x", share->nonce);
    json_array_append_new(params, json_string(user));
    json_array_append_new(params, json_string(share->job_id));
    json_array_append_new(params, json_string(share->extranonce2));
    json_array_append_new(params, json_string(share->ntime));
    json_array_append_new(params, json_string(nonce_hex));

    if (verbose_shares_enabled()) {
        printf("%s[SUBMIT]%s id=%d job=%s en2=%s ntime=%s nonce=%s diff=%s%.8f%s hash_hi=%02x%02x%02x%02x\n",
               C_MAGENTA, C_RESET,
               id, share->job_id, share->extranonce2, share->ntime, nonce_hex,
               C_BRIGHT_YELLOW, share->difficulty, C_RESET,
               share->hash[31], share->hash[30], share->hash[29], share->hash[28]);
    }
    return send_request(conn, id, "mining.submit", params);
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void keyboard_input_init(keyboard_input_t *input) {
    memset(input, 0, sizeof(*input));
#if defined(_WIN32)
    if (!_isatty(_fileno(stdin))) {
        return;
    }
    input->enabled = 1;
#else
    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &input->original) != 0) {
        return;
    }

    struct termios raw = input->original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return;
    }

    input->enabled = 1;
#endif
}

static void keyboard_input_restore(keyboard_input_t *input) {
    if (input != NULL && input->enabled) {
#if !defined(_WIN32)
        tcsetattr(STDIN_FILENO, TCSANOW, &input->original);
#endif
        input->enabled = 0;
    }
}

static int keyboard_input_poll(keyboard_input_t *input, char *out) {
    if (input == NULL || !input->enabled || out == NULL) {
        return 0;
    }

#if defined(_WIN32)
    if (!_kbhit()) {
        return 0;
    }
    int ch = _getch();
    if (ch == 0 || ch == 224) {
        (void)_getch();
        return 0;
    }
    *out = (char)ch;
    return 1;
#else
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    if (ready <= 0) {
        return 0;
    }

    ssize_t n = read(STDIN_FILENO, out, 1);
    return n == 1 ? 1 : 0;
#endif
}

static void print_hashrate_value(double hashes_per_second) {
    const char *unit = "H/s";
    double value = hashes_per_second;

    if (value >= 1000000000000.0) {
        value /= 1000000000000.0;
        unit = "TH/s";
    } else if (value >= 1000000000.0) {
        value /= 1000000000.0;
        unit = "GH/s";
    } else if (value >= 1000000.0) {
        value /= 1000000.0;
        unit = "MH/s";
    } else if (value >= 1000.0) {
        value /= 1000.0;
        unit = "kH/s";
    }

    printf("%s%.3f %s%s", C_BRIGHT_GREEN, value, unit, C_RESET);
}

static void register_pending_share(stratum_state_t *state, int id, const miner_share_t *share, double now) {
    if (state == NULL || share == NULL) {
        return;
    }

    int slot = -1;
    for (int i = 0; i < STRATUM_PENDING_SHARES; ++i) {
        if (state->pending[i].active && state->pending[i].id == id) {
            slot = i;
            break;
        }
        if (slot < 0 && !state->pending[i].active) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = id % STRATUM_PENDING_SHARES;
        if (slot < 0) {
            slot = 0;
        }
    }

    if (!state->pending[slot].active) {
        ++state->pending_count;
    }

    state->pending[slot].id = id;
    state->pending[slot].active = 1;
    state->pending[slot].difficulty = share->difficulty;
    state->pending[slot].submitted_at = now;
    state->last_share_difficulty = share->difficulty;
    if (share->difficulty > state->best_share_difficulty) {
        state->best_share_difficulty = share->difficulty;
    }
}

static double complete_pending_share(stratum_state_t *state, int id, int accepted, double now, double *difficulty) {
    double elapsed = 0.0;
    double diff = 0.0;

    if (state != NULL) {
        for (int i = 0; i < STRATUM_PENDING_SHARES; ++i) {
            if (state->pending[i].active && state->pending[i].id == id) {
                diff = state->pending[i].difficulty;
                elapsed = now - state->pending[i].submitted_at;
                state->pending[i].active = 0;
                if (state->pending_count > 0) {
                    --state->pending_count;
                }
                break;
            }
        }

        state->last_result_accepted = accepted ? 1 : -1;
        state->last_result_difficulty = diff;
        state->last_result_elapsed = elapsed;
    }

    if (difficulty != NULL) {
        *difficulty = diff;
    }
    return elapsed;
}

static void print_results(stratum_state_t *state) {
    if (state == NULL) {
        return;
    }

    uint64_t hashes = state->miner != NULL ? miner_hashes(state->miner) : 0;
    double elapsed = monotonic_seconds() - state->started_at;
    if (elapsed <= 0.0) {
        elapsed = 0.001;
    }

    double avg_rate = (double)hashes / elapsed;
    printf("%s[RESULTS]%s submit=%lu ok=%s%lu%s reject=%s%lu%s pending=%d\n",
           C_MAGENTA, C_RESET,
           state->submit_count,
           C_BRIGHT_GREEN, state->accept_count, C_RESET,
           C_BRIGHT_RED, state->reject_count, C_RESET,
           state->pending_count);

    printf("%s[RESULTS]%s runtime=%.1fs avg=", C_MAGENTA, C_RESET, elapsed);
    print_hashrate_value(avg_rate);
    printf(" hashes=%s%llu%s\n", C_GRAY, (unsigned long long)hashes, C_RESET);

    printf("%s[RESULTS]%s best-share=%s%.8f%s last-share=%s%.8f%s\n",
           C_MAGENTA, C_RESET,
           C_BRIGHT_YELLOW, state->best_share_difficulty, C_RESET,
           C_BRIGHT_YELLOW, state->last_share_difficulty, C_RESET);

    if (state->last_result_accepted != 0) {
        printf("%s[RESULTS]%s last-result=%s%s%s diff=%s%.8f%s latency=%.3fs\n",
               C_MAGENTA, C_RESET,
               state->last_result_accepted > 0 ? C_BRIGHT_GREEN : C_BRIGHT_RED,
               state->last_result_accepted > 0 ? "accepted" : "rejected",
               C_RESET,
               C_BRIGHT_YELLOW, state->last_result_difficulty, C_RESET,
               state->last_result_elapsed);
    } else {
        printf("%s[RESULTS]%s last-result=%snone%s\n", C_MAGENTA, C_RESET, C_GRAY, C_RESET);
    }
}

static void print_connection_status(stratum_state_t *state) {
    if (state == NULL) {
        return;
    }

    double connected_for = state->connected_at > 0.0 ? monotonic_seconds() - state->connected_at : 0.0;
    int paused = state->miner != NULL ? miner_is_paused(state->miner) : 0;
    const char *job = state->current_job_id[0] != '\0' ? state->current_job_id : "none";

    printf("%s[CONNECTION]%s pool=%s%s:%s%s url=%s\n",
           C_CYAN, C_RESET,
           C_BRIGHT_CYAN, state->pool_host, state->pool_port, C_RESET,
           state->pool_url);
    printf("%s[CONNECTION]%s connected=%.1fs subscribed=%s authorized=%s paused=%s\n",
           C_CYAN, C_RESET,
           connected_for,
           state->subscribed ? "yes" : "no",
           state->authorized ? "yes" : "no",
           paused ? "yes" : "no");
    printf("%s[CONNECTION]%s diff=%s%.8f%s jobs=%lu current-job=%s%s%s ntime=%s\n",
           C_CYAN, C_RESET,
           C_BRIGHT_YELLOW, state->difficulty, C_RESET,
           state->notify_count,
           C_BRIGHT_YELLOW, job, C_RESET,
           state->current_ntime[0] != '\0' ? state->current_ntime : "none");
}

static void reset_thread_hashrate_window(stratum_state_t *state,
                                         uint64_t *last_thread_hashes,
                                         int thread_count,
                                         double *last_thread_time) {
    if (state == NULL || state->miner == NULL || last_thread_hashes == NULL || thread_count <= 0) {
        return;
    }

    (void)miner_snapshot_thread_hashes(state->miner, last_thread_hashes, thread_count);
    if (last_thread_time != NULL) {
        *last_thread_time = monotonic_seconds();
    }
}

static void print_thread_hashrates(stratum_state_t *state,
                                   uint64_t *last_thread_hashes,
                                   int thread_count,
                                   double *last_thread_time) {
    if (state->miner == NULL || last_thread_hashes == NULL || thread_count <= 0) {
        printf("%s[THREADS]%s mining disabled\n", C_MAGENTA, C_RESET);
        return;
    }

    uint64_t *current = calloc((size_t)thread_count, sizeof(*current));
    if (current == NULL) {
        printf("%s[THREADS]%s allocation failed\n", C_BRIGHT_RED, C_RESET);
        return;
    }

    int count = miner_snapshot_thread_hashes(state->miner, current, thread_count);
    double now = monotonic_seconds();
    double elapsed = now - *last_thread_time;
    if (elapsed <= 0.0) {
        elapsed = 0.001;
    }

    double total_rate = 0.0;
    for (int i = 0; i < count; ++i) {
        total_rate += (double)(current[i] - last_thread_hashes[i]) / elapsed;
    }

    printf("%s[THREADS]%s window=%.1fs total=", C_MAGENTA, C_RESET, elapsed);
    print_hashrate_value(total_rate);
    printf("\n");

    for (int i = 0; i < count; ++i) {
        double rate = (double)(current[i] - last_thread_hashes[i]) / elapsed;
        printf("%s[THREADS]%s %s#%02d%s ", C_MAGENTA, C_RESET, C_CYAN, i, C_RESET);
        print_hashrate_value(rate);
        printf(" hashes=%s%llu%s\n", C_GRAY, (unsigned long long)current[i], C_RESET);
        last_thread_hashes[i] = current[i];
    }

    *last_thread_time = now;
    free(current);
}

static void handle_keyboard_input(stratum_state_t *state,
                                  keyboard_input_t *keyboard,
                                  uint64_t *last_thread_hashes,
                                  int thread_count,
                                  double *last_thread_time) {
    char ch;
    while (keyboard_input_poll(keyboard, &ch)) {
        if (ch == 'h' || ch == 'H') {
            print_thread_hashrates(state, last_thread_hashes, thread_count, last_thread_time);
        } else if (ch == 'p' || ch == 'P') {
            if (state->miner == NULL) {
                printf("%s[MINER]%s mining disabled\n", C_MAGENTA, C_RESET);
            } else if (miner_is_paused(state->miner)) {
                printf("%s[MINER]%s already %spaused%s\n", C_MAGENTA, C_RESET, C_BRIGHT_YELLOW, C_RESET);
            } else {
                miner_set_paused(state->miner, 1);
                reset_thread_hashrate_window(state, last_thread_hashes, thread_count, last_thread_time);
                printf("%s[MINER]%s %spaused%s, press %sr%s to resume\n",
                       C_MAGENTA, C_RESET, C_BRIGHT_YELLOW, C_RESET, C_BRIGHT_GREEN, C_RESET);
            }
        } else if (ch == 'r' || ch == 'R') {
            if (state->miner == NULL) {
                printf("%s[MINER]%s mining disabled\n", C_MAGENTA, C_RESET);
            } else if (!miner_is_paused(state->miner)) {
                printf("%s[MINER]%s already %srunning%s\n", C_MAGENTA, C_RESET, C_BRIGHT_GREEN, C_RESET);
            } else {
                miner_set_paused(state->miner, 0);
                reset_thread_hashrate_window(state, last_thread_hashes, thread_count, last_thread_time);
                printf("%s[MINER]%s %sresumed%s\n", C_MAGENTA, C_RESET, C_BRIGHT_GREEN, C_RESET);
            }
        } else if (ch == 's' || ch == 'S') {
            print_results(state);
        } else if (ch == 'c' || ch == 'C') {
            print_connection_status(state);
        }
    }
}

static int read_line(line_reader_t *reader, char *out, size_t out_size, int timeout_ms) {
    for (;;) {
        for (size_t i = 0; i < reader->used; ++i) {
            if (reader->buffer[i] == '\n') {
                size_t line_len = i;
                if (line_len > 0 && reader->buffer[line_len - 1] == '\r') {
                    --line_len;
                }
                if (line_len >= out_size) {
                    return -2;
                }
                memcpy(out, reader->buffer, line_len);
                out[line_len] = '\0';

                const size_t consumed = i + 1;
                memmove(reader->buffer, reader->buffer + consumed, reader->used - consumed);
                reader->used -= consumed;
                return 1;
            }
        }

        if (reader->used == sizeof(reader->buffer)) {
            return -2;
        }

        if (conn_pending(reader->conn) <= 0) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(reader->conn->fd, &readfds);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            int ready = select(reader->conn->fd + 1, &readfds, NULL, NULL, &tv);
            if (ready == 0) {
                return -3;
            }
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
        }

        int n = conn_read(reader->conn,
                          reader->buffer + reader->used,
                          sizeof(reader->buffer) - reader->used);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (n == -3) {
                continue;
            }
            return -1;
        }
        reader->used += (size_t)n;
    }
}

static void handle_response(stratum_state_t *state, json_t *root) {
    json_t *id_value = json_object_get(root, "id");
    json_t *result = json_object_get(root, "result");
    json_t *error = json_object_get(root, "error");
    json_int_t id = json_is_integer(id_value) ? json_integer_value(id_value) : -1;

    if (error != NULL && !json_is_null(error)) {
        char *dump = json_dumps(error, JSON_COMPACT);
        if (id >= 4) {
            ++state->reject_count;
            double diff = 0.0;
            double latency = complete_pending_share(state, (int)id, 0, monotonic_seconds(), &diff);
            printf("%s[SUBMIT-RSP]%s id=%lld %srejected%s diff=%s%.8f%s latency=%.3fs error=%s ok=%s%lu%s reject=%s%lu%s\n",
                   C_BRIGHT_RED, C_RESET,
                   (long long)id,
                   C_BRIGHT_RED, C_RESET,
                   C_BRIGHT_YELLOW, diff, C_RESET,
                   latency,
                   dump != NULL ? dump : "?",
                   C_BRIGHT_GREEN, state->accept_count, C_RESET,
                   C_BRIGHT_RED, state->reject_count, C_RESET);
        } else {
            printf("%s[RPC]%s id=%lld error=%s\n",
                   C_BRIGHT_RED, C_RESET, (long long)id, dump != NULL ? dump : "?");
        }
        free(dump);
        return;
    }

    if (id == 1) {
        if (json_is_array(result) && json_array_size(result) >= 3) {
            const char *extranonce1 = json_string_value(json_array_get(result, 1));
            json_t *extranonce2_size = json_array_get(result, 2);
            if (extranonce1 != NULL) {
                snprintf(state->extranonce1, sizeof(state->extranonce1), "%s", extranonce1);
            }
            if (json_is_integer(extranonce2_size)) {
                state->extranonce2_size = (int)json_integer_value(extranonce2_size);
            }
            state->subscribed = 1;
            printf("%s[SUBSCRIBE]%s extranonce1=%s%s%s extranonce2_size=%d\n",
                   C_CYAN, C_RESET,
                   C_BRIGHT_CYAN, state->extranonce1, C_RESET,
                   state->extranonce2_size);
        } else {
            printf("%s[SUBSCRIBE]%s malformed result\n", C_BRIGHT_RED, C_RESET);
        }
    } else if (id == 2) {
        state->authorized = json_is_true(result) ? 1 : 0;
        printf("%s[AUTHORIZE]%s %s%s%s\n",
               state->authorized ? C_BRIGHT_GREEN : C_BRIGHT_RED,
               C_RESET,
               state->authorized ? C_BRIGHT_GREEN : C_BRIGHT_RED,
               state->authorized ? "ok" : "not accepted",
               C_RESET);
    } else if (id >= 4) {
        double diff = 0.0;
        double latency = complete_pending_share(state, (int)id, json_is_true(result), monotonic_seconds(), &diff);
        if (json_is_true(result)) {
            ++state->accept_count;
            if (verbose_shares_enabled()) {
                printf("%s[SUBMIT-RSP]%s id=%lld %saccepted%s diff=%s%.8f%s latency=%.3fs ok=%s%lu%s reject=%s%lu%s\n",
                       C_BRIGHT_GREEN, C_RESET,
                       (long long)id,
                       C_BRIGHT_GREEN, C_RESET,
                       C_BRIGHT_YELLOW, diff, C_RESET,
                       latency,
                       C_BRIGHT_GREEN, state->accept_count, C_RESET,
                       C_BRIGHT_RED, state->reject_count, C_RESET);
            }
        } else {
            ++state->reject_count;
            char *dump = json_dumps(result, JSON_COMPACT);
            printf("%s[SUBMIT-RSP]%s id=%lld %srejected%s diff=%s%.8f%s latency=%.3fs result=%s ok=%s%lu%s reject=%s%lu%s\n",
                   C_BRIGHT_RED, C_RESET,
                   (long long)id,
                   C_BRIGHT_RED, C_RESET,
                   C_BRIGHT_YELLOW, diff, C_RESET,
                   latency,
                   dump != NULL ? dump : "null",
                   C_BRIGHT_GREEN, state->accept_count, C_RESET,
                   C_BRIGHT_RED, state->reject_count, C_RESET);
            free(dump);
        }
    } else {
        char *dump = json_dumps(result, JSON_COMPACT);
        printf("%s[RPC]%s id=%lld result=%s\n",
               C_GRAY, C_RESET, (long long)id, dump != NULL ? dump : "null");
        free(dump);
    }
}

static void handle_set_difficulty(stratum_state_t *state, json_t *params) {
    json_t *value = json_is_array(params) ? json_array_get(params, 0) : NULL;
    if (json_is_number(value)) {
        // Stratum difficulty changes apply to the next mining.notify job.
        state->difficulty = json_number_value(value);
        printf("%s[DIFF]%s %s%.8f%s\n",
               C_YELLOW, C_RESET, C_BRIGHT_YELLOW, state->difficulty, C_RESET);
        if (state->template_valid) {
            (void)rebuild_miner_job(state, "set_difficulty");
        }
    } else {
        printf("%s[DIFF]%s malformed params\n", C_BRIGHT_RED, C_RESET);
    }
}

static int store_template_string(char *dst, size_t dst_size, const char *src, const char *name) {
    if (copy_checked(dst, dst_size, src) != 0) {
        printf("%s[NOTIFY]%s %s too long, ignoring job template\n", C_BRIGHT_RED, C_RESET, name);
        return -1;
    }
    return 0;
}

static int rebuild_miner_job(stratum_state_t *state, const char *reason) {
    if (state == NULL ||
        state->miner == NULL ||
        !state->template_valid ||
        state->extranonce1[0] == '\0' ||
        state->extranonce2_size <= 0) {
        return 0;
    }

    const char *branches[STRATUM_MAX_MERKLE_BRANCHES];
    for (int i = 0; i < state->template_merkle_count; ++i) {
        branches[i] = state->template_merkle[i];
    }

    char extranonce2[32];
    miner_format_extranonce2(extranonce2, sizeof(extranonce2),
                             state->extranonce2_size,
                             state->extranonce2_counter++);
    if (extranonce2[0] == '\0') {
        printf("%s[MINER]%s invalid extranonce2_size=%d\n",
               C_BRIGHT_RED, C_RESET, state->extranonce2_size);
        return -1;
    }

    miner_job_t job;
    if (miner_build_job(&job,
                        state->template_job_id,
                        state->template_prevhash,
                        state->template_coinb1,
                        state->template_coinb2,
                        branches,
                        state->template_merkle_count,
                        state->template_version,
                        state->template_nbits,
                        state->template_ntime,
                        state->extranonce1,
                        extranonce2,
                        state->difficulty) != 0) {
        printf("%s[MINER]%s failed to build job=%s\n",
               C_BRIGHT_RED, C_RESET, state->template_job_id);
        return -1;
    }

    if (reason != NULL && strcmp(reason, "notify") != 0) {
        printf("%s[MINER]%s rolling extranonce2=%s%s%s reason=%s\n",
               C_MAGENTA, C_RESET, C_BRIGHT_CYAN, extranonce2, C_RESET, reason);
    }
    miner_set_job(state->miner, &job);
    return 1;
}

static void handle_set_extranonce(stratum_state_t *state, json_t *params) {
    json_t *extranonce1 = json_is_array(params) ? json_array_get(params, 0) : NULL;
    json_t *extranonce2_size = json_is_array(params) ? json_array_get(params, 1) : NULL;

    if (json_is_string(extranonce1)) {
        snprintf(state->extranonce1, sizeof(state->extranonce1), "%s", json_string_value(extranonce1));
    }
    if (json_is_integer(extranonce2_size)) {
        state->extranonce2_size = (int)json_integer_value(extranonce2_size);
    }

    printf("%s[EXTRANONCE]%s extranonce1=%s%s%s extranonce2_size=%d\n",
           C_CYAN, C_RESET,
           C_BRIGHT_CYAN, state->extranonce1, C_RESET,
           state->extranonce2_size);

    if (state->template_valid) {
        (void)rebuild_miner_job(state, "set_extranonce");
    }
}

static void handle_notify(stratum_state_t *state, json_t *params) {
    if (!json_is_array(params) || json_array_size(params) < 9) {
        printf("%s[NOTIFY]%s malformed params\n", C_BRIGHT_RED, C_RESET);
        return;
    }

    const char *job_id = json_string_or_empty(json_array_get(params, 0));
    const char *prevhash = json_string_or_empty(json_array_get(params, 1));
    const char *coinb1 = json_string_or_empty(json_array_get(params, 2));
    const char *coinb2 = json_string_or_empty(json_array_get(params, 3));
    json_t *merkle_branch = json_array_get(params, 4);
    const char *version = json_string_or_empty(json_array_get(params, 5));
    const char *nbits = json_string_or_empty(json_array_get(params, 6));
    const char *ntime = json_string_or_empty(json_array_get(params, 7));
    bool clean = json_is_true(json_array_get(params, 8));
    size_t branches = json_is_array(merkle_branch) ? json_array_size(merkle_branch) : 0;

    ++state->notify_count;
    snprintf(state->current_job_id, sizeof(state->current_job_id), "%s", job_id);
    snprintf(state->current_ntime, sizeof(state->current_ntime), "%s", ntime);
    printf("%s[NOTIFY]%s #%lu job=%s%s%s clean=%s%s%s branches=%zu diff=%s%.8f%s\n",
           C_YELLOW, C_RESET,
           state->notify_count,
           C_BRIGHT_YELLOW, job_id, C_RESET,
           clean ? C_BRIGHT_GREEN : C_GRAY,
           clean ? "true" : "false",
           C_RESET,
           branches,
           C_BRIGHT_YELLOW, state->difficulty, C_RESET);
    printf("%s         prevhash=%.16s... version=%s nbits=%s ntime=%s%s\n",
           C_GRAY, prevhash, version, nbits, ntime, C_RESET);

    state->template_valid = 0;
    state->template_merkle_count = 0;
    if (store_template_string(state->template_job_id, sizeof(state->template_job_id), job_id, "job_id") != 0 ||
        store_template_string(state->template_prevhash, sizeof(state->template_prevhash), prevhash, "prevhash") != 0 ||
        store_template_string(state->template_coinb1, sizeof(state->template_coinb1), coinb1, "coinb1") != 0 ||
        store_template_string(state->template_coinb2, sizeof(state->template_coinb2), coinb2, "coinb2") != 0 ||
        store_template_string(state->template_version, sizeof(state->template_version), version, "version") != 0 ||
        store_template_string(state->template_nbits, sizeof(state->template_nbits), nbits, "nbits") != 0 ||
        store_template_string(state->template_ntime, sizeof(state->template_ntime), ntime, "ntime") != 0) {
        return;
    }

    if (json_is_array(merkle_branch)) {
        size_t count = json_array_size(merkle_branch);
        if (count > STRATUM_MAX_MERKLE_BRANCHES) {
            count = STRATUM_MAX_MERKLE_BRANCHES;
            printf("%s[NOTIFY]%s merkle branch truncated to %d entries\n",
                   C_YELLOW, C_RESET, STRATUM_MAX_MERKLE_BRANCHES);
        }
        for (size_t i = 0; i < count; ++i) {
            const char *branch = json_string_or_empty(json_array_get(merkle_branch, i));
            if (store_template_string(state->template_merkle[state->template_merkle_count],
                                      sizeof(state->template_merkle[state->template_merkle_count]),
                                      branch,
                                      "merkle_branch") != 0) {
                return;
            }
            ++state->template_merkle_count;
        }
    }

    state->template_valid = 1;
    (void)rebuild_miner_job(state, "notify");
}

static void handle_method(stratum_state_t *state, json_t *root) {
    const char *method = json_string_value(json_object_get(root, "method"));
    json_t *params = json_object_get(root, "params");

    if (method == NULL) {
        return;
    }

    if (strcmp(method, "mining.set_difficulty") == 0) {
        handle_set_difficulty(state, params);
    } else if (strcmp(method, "mining.notify") == 0) {
        handle_notify(state, params);
    } else if (strcmp(method, "mining.set_extranonce") == 0) {
        handle_set_extranonce(state, params);
    } else if (strcmp(method, "client.show_message") == 0) {
        json_t *message = json_is_array(params) ? json_array_get(params, 0) : NULL;
        printf("%s[MESSAGE]%s %s\n", C_MAGENTA, C_RESET, json_string_or_empty(message));
    } else if (strcmp(method, "client.reconnect") == 0) {
        printf("%s[RECONNECT]%s pool requested reconnect\n", C_YELLOW, C_RESET);
    } else {
        printf("%s[METHOD]%s %s\n", C_GRAY, C_RESET, method);
    }
}

int stratum_process_line(stratum_state_t *state, const char *line) {
    json_error_t error;
    json_t *root = json_loads(line, 0, &error);
    if (root == NULL) {
        fprintf(stderr, "%s[JSON]%s parse failed line=%d col=%d: %s\n",
                C_BRIGHT_RED, C_RESET, error.line, error.column, error.text);
        return -1;
    }

    if (json_object_get(root, "method") != NULL) {
        handle_method(state, root);
    } else {
        handle_response(state, root);
    }

    json_decref(root);
    return 0;
}

static void publish_stats(const stratum_state_t *state, const stratum_client_config_t *config);

static int flush_shares(stratum_state_t *state,
                        stratum_conn_t *conn,
                        const char *user,
                        int *next_rpc_id,
                        const stratum_client_config_t *config) {
    if (state->miner == NULL) {
        return 0;
    }

    miner_share_t share;
    while (miner_pop_share(state->miner, &share)) {
        int id = (*next_rpc_id)++;
        if (send_submit_share(conn, id, user, &share) != 0) {
            return -1;
        }
        register_pending_share(state, id, &share, monotonic_seconds());
        ++state->submit_count;
        publish_stats(state, config);
    }
    return 0;
}

static void maybe_roll_extranonce(stratum_state_t *state) {
    if (state == NULL || state->miner == NULL) {
        return;
    }

    if (miner_take_nonce_exhausted(state->miner)) {
        (void)rebuild_miner_job(state, "nonce_exhausted");
    }
}

static void maybe_print_stats(stratum_state_t *state,
                              double *last_time,
                              uint64_t *last_hashes,
                              double stats_interval) {
    if (state->miner == NULL || stats_interval <= 0.0) {
        return;
    }

    double now = monotonic_seconds();
    if (now - *last_time < stats_interval) {
        return;
    }

    uint64_t hashes = miner_hashes(state->miner);
    double rate = (double)(hashes - *last_hashes) / (now - *last_time);
    printf("%s[STATS]%s rate=",
           C_BRIGHT_GREEN,
           C_RESET);
    print_hashrate_value(rate);
    printf(" hashes=%s%llu%s submit=%lu ok=%s%lu%s reject=%s%lu%s diff=%s%.8f%s\n",
           C_GRAY,
           (unsigned long long)hashes,
           C_RESET,
           state->submit_count,
           C_BRIGHT_GREEN,
           state->accept_count,
           C_RESET,
           C_BRIGHT_RED,
           state->reject_count,
           C_RESET,
           C_BRIGHT_YELLOW,
           state->difficulty,
           C_RESET);

    *last_time = now;
    *last_hashes = hashes;
}

static void publish_stats(const stratum_state_t *state, const stratum_client_config_t *config) {
    if (state == NULL || config == NULL || config->on_stats == NULL) {
        return;
    }

    stratum_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.now = monotonic_seconds();
    snapshot.hashes = state->miner != NULL ? miner_hashes(state->miner) : 0;
    snapshot.worker_count = state->miner != NULL ? miner_thread_count(state->miner) : 0;
    snapshot.connected = 1;
    snapshot.subscribed = state->subscribed;
    snapshot.authorized = state->authorized;
    snapshot.jobs = state->notify_count;
    snapshot.submits = state->submit_count;
    snapshot.accepts = state->accept_count;
    snapshot.rejects = state->reject_count;
    config->on_stats(config->stats_opaque, &snapshot);
}

int stratum_run_client(const char *url,
                       const char *user,
                       const char *password,
                       double suggest_difficulty,
                       const stratum_client_config_t *config) {
    pool_endpoint_t endpoint;
    stratum_state_t state;
    stratum_conn_t conn;
    line_reader_t reader;
    char line[65536];
    int authorize_sent = 0;
    int suggest_sent = 0;
    int next_rpc_id = 4;
    uint64_t last_hashes = 0;
    uint64_t *last_thread_hashes = NULL;
    double last_stats_time = monotonic_seconds();
    double last_thread_time = last_stats_time;
    miner_t *miner = NULL;
    keyboard_input_t keyboard;
    int thread_count = config != NULL && config->thread_count >= 0 ? config->thread_count : 1;
    int stat_worker_count = thread_count > 0 ? thread_count : 1;
    int enable_mining = config == NULL || config->enable_mining;
    double stats_interval = config != NULL ? config->stats_interval : 5.0;
    double stop_at = config != NULL ? config->stop_at : 0.0;
    double session_seconds = config != NULL ? config->session_seconds : 0.0;
    const char *session_label = config != NULL ? config->session_label : NULL;
    int tls_compat = config == NULL || config->tls_compat;
    double session_stop_at = 0.0;
    int run_result = 0;

    memset(&keyboard, 0, sizeof(keyboard));

    if (parse_pool_url(url, &endpoint) != 0) {
        fprintf(stderr, "%s[POOL]%s invalid url: %s\n", C_BRIGHT_RED, C_RESET, url);
        return 2;
    }

    printf("%s[POOL]%s connecting to %s%s:%s%s mode=%s%s%s\n",
           C_CYAN, C_RESET,
           C_BRIGHT_CYAN, endpoint.host, endpoint.port, C_RESET,
           endpoint.use_tls ? C_BRIGHT_GREEN : C_GRAY,
           endpoint.use_tls ? "tls" : "tcp",
           C_RESET);
    if (conn_open(&conn, &endpoint, tls_compat) != 0) {
        perror("[NET] connect");
        return 1;
    }
    printf("%s[POOL]%s %sconnected%s\n", C_CYAN, C_RESET, C_BRIGHT_GREEN, C_RESET);

    stratum_state_init(&state);
    snprintf(state.pool_url, sizeof(state.pool_url), "%s", url);
    snprintf(state.pool_host, sizeof(state.pool_host), "%s", endpoint.host);
    snprintf(state.pool_port, sizeof(state.pool_port), "%s", endpoint.port);
    snprintf(state.user, sizeof(state.user), "%s", user);
    state.connected_at = monotonic_seconds();
    state.connect_count = 1;
    publish_stats(&state, config);
    if (enable_mining) {
        const miner_opencl_config_t *opencl_config = config != NULL ? &config->opencl : NULL;
        const miner_cuda_config_t *cuda_config = config != NULL ? &config->cuda : NULL;
        miner = miner_create_with_backend_options(thread_count, opencl_config, cuda_config);
        if (config != NULL) {
            miner_set_cpu_affinity(miner, config->cpu_affinity);
        }
        if (miner == NULL || miner_start(miner) != 0) {
            fprintf(stderr, "%s[MINER]%s failed to start\n", C_BRIGHT_RED, C_RESET);
            miner_destroy(miner);
            conn_close(&conn);
            return 1;
        }
        state.miner = miner;
        stat_worker_count = miner_thread_count(miner);
        if (stat_worker_count <= 0) {
            stat_worker_count = 1;
        }
        last_thread_hashes = calloc((size_t)stat_worker_count, sizeof(*last_thread_hashes));
        if (last_thread_hashes == NULL) {
            fprintf(stderr, "%s[MINER]%s failed to allocate thread stats\n", C_BRIGHT_RED, C_RESET);
            miner_destroy(miner);
            conn_close(&conn);
            return 1;
        }
        (void)miner_snapshot_thread_hashes(miner, last_thread_hashes, stat_worker_count);
        publish_stats(&state, config);
    }

    memset(&reader, 0, sizeof(reader));
    reader.conn = &conn;

    keyboard_input_init(&keyboard);
    if (send_subscribe(&conn) != 0) {
        fprintf(stderr, "[NET] send subscribe failed, reconnecting\n");
        run_result = 1;
        goto cleanup;
    }

    if (keyboard.enabled && miner != NULL) {
        printf("%s[INPUT]%s commands: %sh%s hashrate, %sp%s pause, %sr%s resume, %ss%s results, %sc%s connection\n",
               C_MAGENTA, C_RESET,
               C_BRIGHT_GREEN, C_RESET,
               C_BRIGHT_YELLOW, C_RESET,
               C_BRIGHT_GREEN, C_RESET,
               C_MAGENTA, C_RESET,
               C_CYAN, C_RESET);
    }

    for (;;) {
        if (btcrig_android_core_should_stop()) {
            break;
        }
        if (stop_at > 0.0 && monotonic_seconds() >= stop_at) {
            printf("%s[RUN]%s runtime limit reached\n", C_GRAY, C_RESET);
            break;
        }
        if (session_stop_at > 0.0 && monotonic_seconds() >= session_stop_at) {
            printf("%s[SESSION]%s %s period complete\n",
                   C_CYAN,
                   C_RESET,
                   session_label != NULL ? session_label : "scheduled");
            break;
        }

        handle_keyboard_input(&state, &keyboard, last_thread_hashes, stat_worker_count, &last_thread_time);

        if (flush_shares(&state, &conn, user, &next_rpc_id, config) != 0) {
            fprintf(stderr, "[NET] submit failed, reconnecting\n");
            run_result = 1;
            break;
        }
        maybe_roll_extranonce(&state);
        maybe_print_stats(&state, &last_stats_time, &last_hashes, stats_interval);
        publish_stats(&state, config);

        int rc = read_line(&reader, line, sizeof(line), 200);
        if (rc == -3) {
            continue;
        }
        if (rc == 0) {
            printf("%s[NET]%s pool closed connection\n", C_YELLOW, C_RESET);
            run_result = 1;
            break;
        }
        if (rc < 0) {
            fprintf(stderr, "[NET] read failed rc=%d, reconnecting\n", rc);
            run_result = 1;
            break;
        }
        if (line[0] != '\0') {
            (void)stratum_process_line(&state, line);
            publish_stats(&state, config);
        }

        if (state.subscribed && !authorize_sent) {
            if (send_authorize(&conn, user, password) != 0) {
                fprintf(stderr, "[NET] send authorize failed, reconnecting\n");
                run_result = 1;
                break;
            }
            authorize_sent = 1;
            publish_stats(&state, config);
        }

        if (state.authorized && suggest_difficulty > 0.0 && !suggest_sent) {
            if (send_suggest_difficulty(&conn, suggest_difficulty) != 0) {
                fprintf(stderr, "[NET] send suggest_difficulty failed, reconnecting\n");
                run_result = 1;
                break;
            }
            suggest_sent = 1;
            publish_stats(&state, config);
        }

        if (session_stop_at == 0.0 && session_seconds > 0.0 &&
            state.authorized && state.notify_count > 0) {
            session_stop_at = monotonic_seconds() + session_seconds;
            printf("%s[SESSION]%s %s active for %.0f seconds\n",
                   C_CYAN,
                   C_RESET,
                   session_label != NULL ? session_label : "scheduled",
                   session_seconds);
        }
    }

cleanup:
    publish_stats(&state, config);
    keyboard_input_restore(&keyboard);
    free(last_thread_hashes);
    miner_destroy(miner);
    conn_close(&conn);
    return run_result;
}
