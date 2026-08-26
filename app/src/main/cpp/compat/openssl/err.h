#ifndef BTCRIG_COMPAT_OPENSSL_ERR_H
#define BTCRIG_COMPAT_OPENSSL_ERR_H

static inline unsigned long ERR_get_error(void) {
    return 0;
}

static inline const char *ERR_error_string(unsigned long error, char *buffer) {
    (void)error;
    (void)buffer;
    return "TLS unavailable in this Android build";
}

#endif
