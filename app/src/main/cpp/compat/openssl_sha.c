#include <openssl/sha.h>

#include <string.h>

static const uint32_t k_sha256_initial_state[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
};

static const uint32_t k_sha256_round_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static uint32_t rotr32(uint32_t x, unsigned int n) {
    return (x >> n) | (x << (32U - n));
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void put_be32(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)v;
}

static void put_be64(uint8_t *out, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out[i] = (uint8_t)v;
        v >>= 8;
    }
}

void SHA256_Transform(SHA256_CTX *ctx, const unsigned char data[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load_be32(data + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->h[0];
    uint32_t b = ctx->h[1];
    uint32_t c = ctx->h[2];
    uint32_t d = ctx->h[3];
    uint32_t e = ctx->h[4];
    uint32_t f = ctx->h[5];
    uint32_t g = ctx->h[6];
    uint32_t h = ctx->h[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k_sha256_round_constants[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
    ctx->h[5] += f;
    ctx->h[6] += g;
    ctx->h[7] += h;
}

int SHA256_Init(SHA256_CTX *ctx) {
    if (ctx == NULL) {
        return 0;
    }
    memcpy(ctx->h, k_sha256_initial_state, sizeof(ctx->h));
    ctx->bytes = 0;
    ctx->used = 0;
    memset(ctx->block, 0, sizeof(ctx->block));
    return 1;
}

int SHA256_Update(SHA256_CTX *ctx, const void *data, size_t len) {
    if (ctx == NULL || (data == NULL && len != 0)) {
        return 0;
    }
    const uint8_t *p = (const uint8_t *)data;
    ctx->bytes += len;
    while (len > 0) {
        size_t take = sizeof(ctx->block) - ctx->used;
        if (take > len) {
            take = len;
        }
        memcpy(ctx->block + ctx->used, p, take);
        ctx->used += (unsigned int)take;
        p += take;
        len -= take;
        if (ctx->used == sizeof(ctx->block)) {
            SHA256_Transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
    return 1;
}

int SHA256_Final(unsigned char *md, SHA256_CTX *ctx) {
    if (ctx == NULL || md == NULL) {
        return 0;
    }
    uint64_t bits = ctx->bytes * 8U;
    ctx->block[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        memset(ctx->block + ctx->used, 0, sizeof(ctx->block) - ctx->used);
        SHA256_Transform(ctx, ctx->block);
        ctx->used = 0;
    }
    memset(ctx->block + ctx->used, 0, 56 - ctx->used);
    put_be64(ctx->block + 56, bits);
    SHA256_Transform(ctx, ctx->block);
    for (int i = 0; i < 8; ++i) {
        put_be32(md + i * 4, ctx->h[i]);
    }
    return 1;
}

unsigned char *SHA256(const unsigned char *data, size_t len, unsigned char *md) {
    SHA256_CTX ctx;
    if (md == NULL) {
        return NULL;
    }
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(md, &ctx);
    return md;
}
