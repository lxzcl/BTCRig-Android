#ifndef BTC_MINER_SHA256D_H
#define BTC_MINER_SHA256D_H

#include <stdint.h>

#include <openssl/sha.h>

typedef enum {
    SHA256D_BACKEND_OPENSSL = 0,
    SHA256D_BACKEND_FAST_C = 1,
    SHA256D_BACKEND_ARM_SHA2 = 2,
    SHA256D_BACKEND_X86_SHA_NI = 3,
} sha256d_backend_t;

typedef struct {
    SHA256_CTX ctx;
    SHA256_CTX second_ctx;
    uint32_t fast_state[8];
} sha256_midstate_t;

typedef void (*sha256d_tail_words_func_t)(const sha256_midstate_t *state,
                                          const uint32_t tail_words[4],
                                          uint32_t out_words[8]);

typedef void (*sha256d_scan_match_func_t)(void *opaque,
                                          uint32_t nonce,
                                          const uint32_t hash_words[8]);

typedef void (*sha256d_nonce_range_func_t)(const sha256_midstate_t *state,
                                           const uint32_t tail_words[4],
                                           const uint32_t target_words[8],
                                           uint32_t start_nonce,
                                           uint32_t nonce_count,
                                           void *opaque,
                                           sha256d_scan_match_func_t on_match);

const char *sha256d_backend_name(sha256d_backend_t backend);
int sha256d_backend_available(sha256d_backend_t backend);
sha256d_backend_t sha256d_auto_backend(void);
int sha256d_set_backend(sha256d_backend_t backend);
sha256d_backend_t sha256d_get_backend(void);
int sha256d_parse_backend(const char *text, sha256d_backend_t *out);
sha256d_tail_words_func_t sha256d_tail_words_func(void);
sha256d_nonce_range_func_t sha256d_nonce_range_func(void);

void sha256d_80(const uint8_t header[80], uint8_t out[32]);
void sha256d_80_midstate_prepare(sha256_midstate_t *state, const uint8_t header_first64[64]);
void sha256d_words_to_hash(const uint32_t words[8], uint8_t out[32]);
void sha256d_80_midstate_hash_tail_words(const sha256_midstate_t *state,
                                         const uint32_t tail_words[4],
                                         uint32_t out_words[8]);
void sha256d_80_midstate_hash_words(const sha256_midstate_t *state,
                                    const uint8_t header_tail16[16],
                                    uint32_t out_words[8]);
void sha256d_80_midstate_hash(const sha256_midstate_t *state, const uint8_t header_tail16[16], uint8_t out[32]);

#endif
