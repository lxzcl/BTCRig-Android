#ifndef BTCRIG_COMPAT_OPENSSL_SHA_H
#define BTCRIG_COMPAT_OPENSSL_SHA_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LENGTH 32

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    unsigned int used;
} SHA256_CTX;

int SHA256_Init(SHA256_CTX *ctx);
int SHA256_Update(SHA256_CTX *ctx, const void *data, size_t len);
int SHA256_Final(unsigned char *md, SHA256_CTX *ctx);
void SHA256_Transform(SHA256_CTX *ctx, const unsigned char data[64]);
unsigned char *SHA256(const unsigned char *data, size_t len, unsigned char *md);

#endif
