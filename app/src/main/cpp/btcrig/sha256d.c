#include "sha256d.h"

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

static sha256d_backend_t g_backend = SHA256D_BACKEND_OPENSSL;

#if defined(BTC_MINER_ARM_SHA2)
int sha256d_arm_sha2_available(void);
void sha256d_80_midstate_hash_tail_words_arm_sha2(const sha256_midstate_t *state,
                                                  const uint32_t tail_words[4],
                                                  uint32_t out_words[8]);
void sha256d_80_midstate_hash_words_arm_sha2(const sha256_midstate_t *state,
                                             const uint8_t header_tail16[16],
                                             uint32_t out_words[8]);
void sha256d_scan_nonce_range_arm_sha2(const sha256_midstate_t *state,
                                       const uint32_t tail_words[4],
                                       const uint32_t target_words[8],
                                       uint32_t start_nonce,
                                       uint32_t nonce_count,
                                       void *opaque,
                                       sha256d_scan_match_func_t on_match);
#endif

#if defined(BTC_MINER_X86_SHA_NI)
int sha256d_x86_sha_ni_available(void);
void sha256d_80_midstate_hash_tail_words_x86_sha_ni(const sha256_midstate_t *state,
                                                    const uint32_t tail_words[4],
                                                    uint32_t out_words[8]);
void sha256d_scan_nonce_range_x86_sha_ni(const sha256_midstate_t *state,
                                         const uint32_t tail_words[4],
                                         const uint32_t target_words[8],
                                         uint32_t start_nonce,
                                         uint32_t nonce_count,
                                         void *opaque,
                                         sha256d_scan_match_func_t on_match);
#endif

const char *sha256d_backend_name(sha256d_backend_t backend) {
    switch (backend) {
    case SHA256D_BACKEND_OPENSSL:
        return "openssl";
    case SHA256D_BACKEND_FAST_C:
        return "fast-c";
    case SHA256D_BACKEND_ARM_SHA2:
        return "arm-sha2";
    case SHA256D_BACKEND_X86_SHA_NI:
        return "x86-sha-ni";
    default:
        return "unknown";
    }
}

int sha256d_backend_available(sha256d_backend_t backend) {
    switch (backend) {
    case SHA256D_BACKEND_OPENSSL:
    case SHA256D_BACKEND_FAST_C:
        return 1;
    case SHA256D_BACKEND_ARM_SHA2:
#if defined(BTC_MINER_ARM_SHA2)
        return sha256d_arm_sha2_available();
#else
        return 0;
#endif
    case SHA256D_BACKEND_X86_SHA_NI:
#if defined(BTC_MINER_X86_SHA_NI)
        return sha256d_x86_sha_ni_available();
#else
        return 0;
#endif
    default:
        return 0;
    }
}

sha256d_backend_t sha256d_auto_backend(void) {
    if (sha256d_backend_available(SHA256D_BACKEND_X86_SHA_NI)) {
        return SHA256D_BACKEND_X86_SHA_NI;
    }
    if (sha256d_backend_available(SHA256D_BACKEND_ARM_SHA2)) {
        return SHA256D_BACKEND_ARM_SHA2;
    }
    return SHA256D_BACKEND_OPENSSL;
}

int sha256d_set_backend(sha256d_backend_t backend) {
    if (!sha256d_backend_available(backend)) {
        return -1;
    }
    g_backend = backend;
    return 0;
}

sha256d_backend_t sha256d_get_backend(void) {
    return g_backend;
}

int sha256d_parse_backend(const char *text, sha256d_backend_t *out) {
    if (text == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(text, "openssl") == 0 || strcmp(text, "ossl") == 0) {
        *out = SHA256D_BACKEND_OPENSSL;
        return 0;
    }
    if (strcmp(text, "fast-c") == 0 ||
        strcmp(text, "fastc") == 0 ||
        strcmp(text, "portable") == 0 ||
        strcmp(text, "c") == 0) {
        *out = SHA256D_BACKEND_FAST_C;
        return 0;
    }
    if (strcmp(text, "arm-sha2") == 0 ||
        strcmp(text, "armv8-sha2") == 0 ||
        strcmp(text, "sha2") == 0 ||
        strcmp(text, "neon") == 0) {
        *out = SHA256D_BACKEND_ARM_SHA2;
        return 0;
    }
    if (strcmp(text, "x86-sha-ni") == 0 ||
        strcmp(text, "x86-shani") == 0 ||
        strcmp(text, "sha-ni") == 0 ||
        strcmp(text, "shani") == 0) {
        *out = SHA256D_BACKEND_X86_SHA_NI;
        return 0;
    }
    return -1;
}

static uint32_t rotr32(uint32_t x, unsigned int n) {
    return (x >> n) | (x << (32U - n));
}

static uint32_t bswap32(uint32_t v) {
#if defined(__GNUC__)
    return __builtin_bswap32(v);
#else
    return ((v & 0x000000ffU) << 24) |
           ((v & 0x0000ff00U) << 8) |
           ((v & 0x00ff0000U) >> 8) |
           ((v & 0xff000000U) >> 24);
#endif
}

static uint32_t load_be32(const uint8_t *p) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
#else
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
#endif
}

static void put_be32(uint8_t *out, uint32_t v) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t be = __builtin_bswap32(v);
    memcpy(out, &be, sizeof(be));
#else
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)v;
#endif
}

static void put_be64(uint8_t *out, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void sha256_compress_words(uint32_t state[8], uint32_t w[64]) {
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

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

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_compress_block(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load_be32(block + i * 4);
    }
    sha256_compress_words(state, w);
}

void sha256d_80(const uint8_t header[80], uint8_t out[32]) {
    uint8_t first[SHA256_DIGEST_LENGTH];
    SHA256(header, 80, first);
    SHA256(first, SHA256_DIGEST_LENGTH, out);
}

void sha256d_80_midstate_prepare(sha256_midstate_t *state, const uint8_t header_first64[64]) {
    SHA256_Init(&state->ctx);
    SHA256_Update(&state->ctx, header_first64, 64);
    SHA256_Init(&state->second_ctx);

    memcpy(state->fast_state, k_sha256_initial_state, sizeof(state->fast_state));
    sha256_compress_block(state->fast_state, header_first64);
}

static void sha256d_80_midstate_hash_openssl(const sha256_midstate_t *state,
                                             const uint32_t tail_words[4],
                                             uint32_t out_words[8]) {
    uint8_t first_block[64];
    uint8_t second_block[64];
    SHA256_CTX ctx = state->ctx;
    SHA256_CTX second_ctx = state->second_ctx;

    for (int i = 0; i < 4; ++i) {
        put_be32(first_block + i * 4, tail_words[i]);
    }
    first_block[16] = 0x80;
    memset(first_block + 17, 0, 39);
    put_be64(first_block + 56, 80U * 8U);
    SHA256_Transform(&ctx, first_block);

    for (int i = 0; i < 8; ++i) {
        put_be32(second_block + i * 4, ctx.h[i]);
    }
    second_block[32] = 0x80;
    memset(second_block + 33, 0, 23);
    put_be64(second_block + 56, 32U * 8U);

    SHA256_Transform(&second_ctx, second_block);

    for (int i = 0; i < 8; ++i) {
        out_words[i] = second_ctx.h[i];
    }
}

static void sha256d_80_midstate_hash_fast_c(const sha256_midstate_t *state,
                                            const uint32_t tail_words[4],
                                            uint32_t out_words[8]) {
    uint32_t first_state[8];
    uint32_t second_state[8];
    uint32_t w[64];

    memcpy(first_state, state->fast_state, sizeof(first_state));

    w[0] = tail_words[0];
    w[1] = tail_words[1];
    w[2] = tail_words[2];
    w[3] = tail_words[3];
    w[4] = 0x80000000U;
    for (int i = 5; i < 15; ++i) {
        w[i] = 0;
    }
    w[15] = 80U * 8U;
    sha256_compress_words(first_state, w);

    memcpy(second_state, k_sha256_initial_state, sizeof(second_state));
    for (int i = 0; i < 8; ++i) {
        w[i] = first_state[i];
    }
    w[8] = 0x80000000U;
    for (int i = 9; i < 15; ++i) {
        w[i] = 0;
    }
    w[15] = 32U * 8U;
    sha256_compress_words(second_state, w);

    for (int i = 0; i < 8; ++i) {
        out_words[i] = second_state[i];
    }
}

void sha256d_words_to_hash(const uint32_t words[8], uint8_t out[32]) {
    for (int i = 0; i < 8; ++i) {
        put_be32(out + i * 4, words[i]);
    }
}

static int hash_words_meet_target(const uint32_t hash_words[8], const uint32_t target_words[8]) {
    for (int i = 7; i >= 0; --i) {
        uint32_t h = bswap32(hash_words[i]);
        uint32_t t = target_words[i];
        if (h < t) {
            return 1;
        }
        if (h > t) {
            return 0;
        }
    }
    return 1;
}

static void sha256d_scan_nonce_range_generic(const sha256_midstate_t *state,
                                             const uint32_t base_tail_words[4],
                                             const uint32_t target_words[8],
                                             uint32_t start_nonce,
                                             uint32_t nonce_count,
                                             void *opaque,
                                             sha256d_scan_match_func_t on_match) {
    uint32_t tail_words[4];
    uint32_t hash_words[8];
    sha256d_tail_words_func_t hash_tail_words = sha256d_tail_words_func();

    memcpy(tail_words, base_tail_words, sizeof(tail_words));
    for (uint32_t i = 0; i < nonce_count; ++i) {
        uint32_t nonce = start_nonce + i;
        tail_words[3] = bswap32(nonce);
        hash_tail_words(state, tail_words, hash_words);
        if (hash_words_meet_target(hash_words, target_words) && on_match != NULL) {
            on_match(opaque, nonce, hash_words);
        }
    }
}

sha256d_tail_words_func_t sha256d_tail_words_func(void) {
    switch (g_backend) {
    case SHA256D_BACKEND_FAST_C:
        return sha256d_80_midstate_hash_fast_c;
    case SHA256D_BACKEND_ARM_SHA2:
#if defined(BTC_MINER_ARM_SHA2)
        return sha256d_80_midstate_hash_tail_words_arm_sha2;
#else
        return sha256d_80_midstate_hash_openssl;
#endif
    case SHA256D_BACKEND_X86_SHA_NI:
#if defined(BTC_MINER_X86_SHA_NI)
        return sha256d_80_midstate_hash_tail_words_x86_sha_ni;
#else
        return sha256d_80_midstate_hash_openssl;
#endif
    case SHA256D_BACKEND_OPENSSL:
    default:
        return sha256d_80_midstate_hash_openssl;
    }
}

sha256d_nonce_range_func_t sha256d_nonce_range_func(void) {
    switch (g_backend) {
    case SHA256D_BACKEND_X86_SHA_NI:
#if defined(BTC_MINER_X86_SHA_NI)
        return sha256d_scan_nonce_range_x86_sha_ni;
#else
        return sha256d_scan_nonce_range_generic;
#endif
    case SHA256D_BACKEND_ARM_SHA2:
#if defined(BTC_MINER_ARM_SHA2)
        return sha256d_scan_nonce_range_arm_sha2;
#else
        return sha256d_scan_nonce_range_generic;
#endif
    case SHA256D_BACKEND_OPENSSL:
    case SHA256D_BACKEND_FAST_C:
    default:
        return sha256d_scan_nonce_range_generic;
    }
}

void sha256d_80_midstate_hash_tail_words(const sha256_midstate_t *state,
                                         const uint32_t tail_words[4],
                                         uint32_t out_words[8]) {
    sha256d_tail_words_func()(state, tail_words, out_words);
}

void sha256d_80_midstate_hash_words(const sha256_midstate_t *state,
                                    const uint8_t header_tail16[16],
                                    uint32_t out_words[8]) {
    uint32_t tail_words[4];
    for (int i = 0; i < 4; ++i) {
        tail_words[i] = load_be32(header_tail16 + i * 4);
    }
    sha256d_80_midstate_hash_tail_words(state, tail_words, out_words);
}

void sha256d_80_midstate_hash(const sha256_midstate_t *state, const uint8_t header_tail16[16], uint8_t out[32]) {
    uint32_t words[8];
    sha256d_80_midstate_hash_words(state, header_tail16, words);
    sha256d_words_to_hash(words, out);
}
