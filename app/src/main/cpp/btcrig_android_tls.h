#ifndef BTCRIG_ANDROID_TLS_H
#define BTCRIG_ANDROID_TLS_H

#if defined(__ANDROID__)

#include <jni.h>
#include <stddef.h>

int btcrig_android_tls_onload(JavaVM *vm);
int btcrig_android_tls_open(const char *host, const char *port, int verify);
int btcrig_android_tls_write(int id, const void *data, size_t len);
int btcrig_android_tls_read(int id, void *data, size_t len);
int btcrig_android_tls_pending(int id);
void btcrig_android_tls_close(int id);
void btcrig_android_tls_cipher(int id, char *out, size_t out_size);
void btcrig_android_tls_last_error(char *out, size_t out_size);

#endif

#endif
