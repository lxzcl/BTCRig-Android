#ifndef BTCRIG_COMPAT_OPENSSL_SSL_H
#define BTCRIG_COMPAT_OPENSSL_SSL_H

#define SSL_VERIFY_NONE 0
#define SSL_VERIFY_PEER 1
#define SSL_ERROR_ZERO_RETURN 6
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3

typedef struct btcrig_compat_ssl_ctx SSL_CTX;
typedef struct btcrig_compat_ssl SSL;
typedef struct btcrig_compat_ssl_method SSL_METHOD;

static inline const SSL_METHOD *TLS_client_method(void) {
    return (const SSL_METHOD *)0;
}

static inline SSL_CTX *SSL_CTX_new(const SSL_METHOD *method) {
    (void)method;
    return (SSL_CTX *)0;
}

static inline void SSL_CTX_free(SSL_CTX *ctx) {
    (void)ctx;
}

static inline int SSL_CTX_set_default_verify_paths(SSL_CTX *ctx) {
    (void)ctx;
    return 0;
}

static inline void SSL_CTX_set_verify(SSL_CTX *ctx, int mode, void *callback) {
    (void)ctx;
    (void)mode;
    (void)callback;
}

static inline SSL *SSL_new(SSL_CTX *ctx) {
    (void)ctx;
    return (SSL *)0;
}

static inline void SSL_free(SSL *ssl) {
    (void)ssl;
}

static inline int SSL_set_tlsext_host_name(SSL *ssl, const char *name) {
    (void)ssl;
    (void)name;
    return 0;
}

static inline int SSL_set1_host(SSL *ssl, const char *name) {
    (void)ssl;
    (void)name;
    return 0;
}

static inline int SSL_set_fd(SSL *ssl, int fd) {
    (void)ssl;
    (void)fd;
    return 0;
}

static inline int SSL_connect(SSL *ssl) {
    (void)ssl;
    return 0;
}

static inline int SSL_shutdown(SSL *ssl) {
    (void)ssl;
    return 0;
}

static inline const char *SSL_get_cipher(const SSL *ssl) {
    (void)ssl;
    return "none";
}

static inline int SSL_write(SSL *ssl, const void *data, int len) {
    (void)ssl;
    (void)data;
    (void)len;
    return -1;
}

static inline int SSL_read(SSL *ssl, void *data, int len) {
    (void)ssl;
    (void)data;
    (void)len;
    return -1;
}

static inline int SSL_get_error(const SSL *ssl, int ret) {
    (void)ssl;
    (void)ret;
    return 0;
}

static inline int SSL_pending(const SSL *ssl) {
    (void)ssl;
    return 0;
}

#endif
