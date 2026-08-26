#include "sha256d.h"

#include <arm_neon.h>
#include <string.h>

#if defined(__linux__)
#include <sys/auxv.h>
#if defined(__has_include)
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#endif
#else
#include <asm/hwcap.h>
#endif
#endif

#if defined(__GNUC__)
#define BTC_ARM_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define BTC_ARM_ALWAYS_INLINE static inline
#endif

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

static const uint32_t k_sha256_initial_state[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
};

static const uint32_t k_first_tail_wk1[4] = {
    0xb956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
};

static const uint32_t k_first_tail_wk2[4] = {
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
};

static const uint32_t k_first_tail_wk3[4] = {
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf3f4U,
};

static const uint32_t k_second_hash_wk2[4] = {
    0x5807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
};

static const uint32_t k_second_hash_wk3[4] = {
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf274U,
};

BTC_ARM_ALWAYS_INLINE uint32_t load_be32(const uint8_t *p) {
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

BTC_ARM_ALWAYS_INLINE uint32_t bswap32(uint32_t v) {
#if defined(__GNUC__)
    return __builtin_bswap32(v);
#else
    return ((v & 0x000000ffU) << 24) |
           ((v & 0x0000ff00U) << 8) |
           ((v & 0x00ff0000U) >> 8) |
           ((v & 0xff000000U) >> 24);
#endif
}

BTC_ARM_ALWAYS_INLINE int hash_words_meet_target(const uint32_t hash_words[8], const uint32_t target_words[8]) {
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

int sha256d_arm_sha2_available(void) {
#if defined(__linux__) && defined(AT_HWCAP) && defined(HWCAP_SHA2)
    return (getauxval(AT_HWCAP) & (unsigned long)HWCAP_SHA2) != 0;
#elif defined(__APPLE__) && defined(__aarch64__)
    return 1;
#else
    return 0;
#endif
}

BTC_ARM_ALWAYS_INLINE uint32x4_t make_u32x4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (uint32x4_t){a, b, c, d};
}

BTC_ARM_ALWAYS_INLINE void sha256_arm_compress_btc_tail(uint32x4_t start_abcd,
                                                        uint32x4_t start_efgh,
                                                        uint32x4_t sched0,
                                                        uint32x4_t *out_abcd,
                                                        uint32x4_t *out_efgh) {
    uint32x4_t abcd = start_abcd;
    uint32x4_t efgh = start_efgh;
    uint32x4_t sched1 = make_u32x4(0x80000000U, 0, 0, 0);
    uint32x4_t sched2 = make_u32x4(0, 0, 0, 0);
    uint32x4_t sched3 = make_u32x4(0, 0, 0, 80U * 8U);
    const uint32x4_t abcd_orig = abcd;
    const uint32x4_t efgh_orig = efgh;

#define BTC_SHA256_ARM_ROUND4(abcd_var, efgh_var, schedule, offset) do { \
        const uint32x4_t wk__ = vaddq_u32((schedule), vld1q_u32(&k_sha256_round_constants[(offset)])); \
        const uint32x4_t abcd_prev__ = (abcd_var); \
        (abcd_var) = vsha256hq_u32(abcd_prev__, (efgh_var), wk__); \
        (efgh_var) = vsha256h2q_u32((efgh_var), abcd_prev__, wk__); \
    } while (0)

#define BTC_SHA256_ARM_ROUND4_WK(abcd_var, efgh_var, wk_vec) do { \
        const uint32x4_t abcd_prev__ = (abcd_var); \
        (abcd_var) = vsha256hq_u32(abcd_prev__, (efgh_var), (wk_vec)); \
        (efgh_var) = vsha256h2q_u32((efgh_var), abcd_prev__, (wk_vec)); \
    } while (0)

    BTC_SHA256_ARM_ROUND4(abcd, efgh, sched0, 0);
    BTC_SHA256_ARM_ROUND4_WK(abcd, efgh, vld1q_u32(k_first_tail_wk1));
    BTC_SHA256_ARM_ROUND4_WK(abcd, efgh, vld1q_u32(k_first_tail_wk2));
    BTC_SHA256_ARM_ROUND4_WK(abcd, efgh, vld1q_u32(k_first_tail_wk3));

#define BTC_SHA256_ARM_ROUNDS16(offset) do { \
        sched0 = vsha256su1q_u32(vsha256su0q_u32(sched0, sched1), sched2, sched3); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched0, (offset)); \
        sched1 = vsha256su1q_u32(vsha256su0q_u32(sched1, sched2), sched3, sched0); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched1, (offset) + 4); \
        sched2 = vsha256su1q_u32(vsha256su0q_u32(sched2, sched3), sched0, sched1); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched2, (offset) + 8); \
        sched3 = vsha256su1q_u32(vsha256su0q_u32(sched3, sched0), sched1, sched2); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched3, (offset) + 12); \
    } while (0)

    BTC_SHA256_ARM_ROUNDS16(16);
    BTC_SHA256_ARM_ROUNDS16(32);
    BTC_SHA256_ARM_ROUNDS16(48);

#undef BTC_SHA256_ARM_ROUNDS16
#undef BTC_SHA256_ARM_ROUND4_WK
#undef BTC_SHA256_ARM_ROUND4

    *out_abcd = vaddq_u32(abcd, abcd_orig);
    *out_efgh = vaddq_u32(efgh, efgh_orig);
}

BTC_ARM_ALWAYS_INLINE void sha256_arm_compress_second_hash(uint32x4_t sched0,
                                                          uint32x4_t sched1,
                                                          uint32x4_t *out_abcd,
                                                          uint32x4_t *out_efgh) {
    uint32x4_t abcd = make_u32x4(k_sha256_initial_state[0],
                                 k_sha256_initial_state[1],
                                 k_sha256_initial_state[2],
                                 k_sha256_initial_state[3]);
    uint32x4_t efgh = make_u32x4(k_sha256_initial_state[4],
                                 k_sha256_initial_state[5],
                                 k_sha256_initial_state[6],
                                 k_sha256_initial_state[7]);
    uint32x4_t sched2 = make_u32x4(0x80000000U, 0, 0, 0);
    uint32x4_t sched3 = make_u32x4(0, 0, 0, 32U * 8U);
    const uint32x4_t abcd_orig = abcd;
    const uint32x4_t efgh_orig = efgh;

#define BTC_SHA256_ARM_ROUND4(abcd_var, efgh_var, schedule, offset) do { \
        const uint32x4_t wk__ = vaddq_u32((schedule), vld1q_u32(&k_sha256_round_constants[(offset)])); \
        const uint32x4_t abcd_prev__ = (abcd_var); \
        (abcd_var) = vsha256hq_u32(abcd_prev__, (efgh_var), wk__); \
        (efgh_var) = vsha256h2q_u32((efgh_var), abcd_prev__, wk__); \
    } while (0)

#define BTC_SHA256_ARM_ROUND4_WK(abcd_var, efgh_var, wk_vec) do { \
        const uint32x4_t abcd_prev__ = (abcd_var); \
        (abcd_var) = vsha256hq_u32(abcd_prev__, (efgh_var), (wk_vec)); \
        (efgh_var) = vsha256h2q_u32((efgh_var), abcd_prev__, (wk_vec)); \
    } while (0)

    BTC_SHA256_ARM_ROUND4(abcd, efgh, sched0, 0);
    BTC_SHA256_ARM_ROUND4(abcd, efgh, sched1, 4);
    BTC_SHA256_ARM_ROUND4_WK(abcd, efgh, vld1q_u32(k_second_hash_wk2));
    BTC_SHA256_ARM_ROUND4_WK(abcd, efgh, vld1q_u32(k_second_hash_wk3));

#define BTC_SHA256_ARM_ROUNDS16(offset) do { \
        sched0 = vsha256su1q_u32(vsha256su0q_u32(sched0, sched1), sched2, sched3); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched0, (offset)); \
        sched1 = vsha256su1q_u32(vsha256su0q_u32(sched1, sched2), sched3, sched0); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched1, (offset) + 4); \
        sched2 = vsha256su1q_u32(vsha256su0q_u32(sched2, sched3), sched0, sched1); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched2, (offset) + 8); \
        sched3 = vsha256su1q_u32(vsha256su0q_u32(sched3, sched0), sched1, sched2); \
        BTC_SHA256_ARM_ROUND4(abcd, efgh, sched3, (offset) + 12); \
    } while (0)

    BTC_SHA256_ARM_ROUNDS16(16);
    BTC_SHA256_ARM_ROUNDS16(32);
    BTC_SHA256_ARM_ROUNDS16(48);

#undef BTC_SHA256_ARM_ROUNDS16
#undef BTC_SHA256_ARM_ROUND4_WK
#undef BTC_SHA256_ARM_ROUND4

    *out_abcd = vaddq_u32(abcd, abcd_orig);
    *out_efgh = vaddq_u32(efgh, efgh_orig);
}

BTC_ARM_ALWAYS_INLINE void sha256_arm_compress_btc_tail2(uint32x4_t start_abcd,
                                                         uint32x4_t start_efgh,
                                                         uint32x4_t a_sched0,
                                                         uint32x4_t b_sched0,
                                                         uint32x4_t *a_out_abcd,
                                                         uint32x4_t *a_out_efgh,
                                                         uint32x4_t *b_out_abcd,
                                                         uint32x4_t *b_out_efgh) {
    uint32x4_t a_abcd = start_abcd;
    uint32x4_t a_efgh = start_efgh;
    uint32x4_t b_abcd = start_abcd;
    uint32x4_t b_efgh = start_efgh;
    uint32x4_t a_sched1 = make_u32x4(0x80000000U, 0, 0, 0);
    uint32x4_t a_sched2 = make_u32x4(0, 0, 0, 0);
    uint32x4_t a_sched3 = make_u32x4(0, 0, 0, 80U * 8U);
    uint32x4_t b_sched1 = a_sched1;
    uint32x4_t b_sched2 = a_sched2;
    uint32x4_t b_sched3 = a_sched3;

#define BTC_SHA256_ARM_ROUND4_PAIR(a_schedule, b_schedule, offset) do { \
        const uint32x4_t k__ = vld1q_u32(&k_sha256_round_constants[(offset)]); \
        const uint32x4_t a_wk__ = vaddq_u32((a_schedule), k__); \
        const uint32x4_t b_wk__ = vaddq_u32((b_schedule), k__); \
        const uint32x4_t a_abcd_prev__ = a_abcd; \
        const uint32x4_t b_abcd_prev__ = b_abcd; \
        a_abcd = vsha256hq_u32(a_abcd_prev__, a_efgh, a_wk__); \
        b_abcd = vsha256hq_u32(b_abcd_prev__, b_efgh, b_wk__); \
        a_efgh = vsha256h2q_u32(a_efgh, a_abcd_prev__, a_wk__); \
        b_efgh = vsha256h2q_u32(b_efgh, b_abcd_prev__, b_wk__); \
    } while (0)

#define BTC_SHA256_ARM_ROUND4_WK_PAIR(wk_vec) do { \
        const uint32x4_t wk__ = (wk_vec); \
        const uint32x4_t a_abcd_prev__ = a_abcd; \
        const uint32x4_t b_abcd_prev__ = b_abcd; \
        a_abcd = vsha256hq_u32(a_abcd_prev__, a_efgh, wk__); \
        b_abcd = vsha256hq_u32(b_abcd_prev__, b_efgh, wk__); \
        a_efgh = vsha256h2q_u32(a_efgh, a_abcd_prev__, wk__); \
        b_efgh = vsha256h2q_u32(b_efgh, b_abcd_prev__, wk__); \
    } while (0)

    BTC_SHA256_ARM_ROUND4_PAIR(a_sched0, b_sched0, 0);
    BTC_SHA256_ARM_ROUND4_WK_PAIR(vld1q_u32(k_first_tail_wk1));
    BTC_SHA256_ARM_ROUND4_WK_PAIR(vld1q_u32(k_first_tail_wk2));
    BTC_SHA256_ARM_ROUND4_WK_PAIR(vld1q_u32(k_first_tail_wk3));

#define BTC_SHA256_ARM_ROUNDS16_PAIR(offset) do { \
        a_sched0 = vsha256su1q_u32(vsha256su0q_u32(a_sched0, a_sched1), a_sched2, a_sched3); \
        b_sched0 = vsha256su1q_u32(vsha256su0q_u32(b_sched0, b_sched1), b_sched2, b_sched3); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched0, b_sched0, (offset)); \
        a_sched1 = vsha256su1q_u32(vsha256su0q_u32(a_sched1, a_sched2), a_sched3, a_sched0); \
        b_sched1 = vsha256su1q_u32(vsha256su0q_u32(b_sched1, b_sched2), b_sched3, b_sched0); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched1, b_sched1, (offset) + 4); \
        a_sched2 = vsha256su1q_u32(vsha256su0q_u32(a_sched2, a_sched3), a_sched0, a_sched1); \
        b_sched2 = vsha256su1q_u32(vsha256su0q_u32(b_sched2, b_sched3), b_sched0, b_sched1); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched2, b_sched2, (offset) + 8); \
        a_sched3 = vsha256su1q_u32(vsha256su0q_u32(a_sched3, a_sched0), a_sched1, a_sched2); \
        b_sched3 = vsha256su1q_u32(vsha256su0q_u32(b_sched3, b_sched0), b_sched1, b_sched2); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched3, b_sched3, (offset) + 12); \
    } while (0)

    BTC_SHA256_ARM_ROUNDS16_PAIR(16);
    BTC_SHA256_ARM_ROUNDS16_PAIR(32);
    BTC_SHA256_ARM_ROUNDS16_PAIR(48);

#undef BTC_SHA256_ARM_ROUNDS16_PAIR
#undef BTC_SHA256_ARM_ROUND4_WK_PAIR
#undef BTC_SHA256_ARM_ROUND4_PAIR

    *a_out_abcd = vaddq_u32(a_abcd, start_abcd);
    *a_out_efgh = vaddq_u32(a_efgh, start_efgh);
    *b_out_abcd = vaddq_u32(b_abcd, start_abcd);
    *b_out_efgh = vaddq_u32(b_efgh, start_efgh);
}

BTC_ARM_ALWAYS_INLINE void sha256_arm_compress_second_hash2(uint32x4_t a_sched0,
                                                            uint32x4_t a_sched1,
                                                            uint32x4_t b_sched0,
                                                            uint32x4_t b_sched1,
                                                            uint32x4_t *a_out_abcd,
                                                            uint32x4_t *a_out_efgh,
                                                            uint32x4_t *b_out_abcd,
                                                            uint32x4_t *b_out_efgh) {
    const uint32x4_t initial_abcd = make_u32x4(k_sha256_initial_state[0],
                                               k_sha256_initial_state[1],
                                               k_sha256_initial_state[2],
                                               k_sha256_initial_state[3]);
    const uint32x4_t initial_efgh = make_u32x4(k_sha256_initial_state[4],
                                               k_sha256_initial_state[5],
                                               k_sha256_initial_state[6],
                                               k_sha256_initial_state[7]);
    uint32x4_t a_abcd = initial_abcd;
    uint32x4_t a_efgh = initial_efgh;
    uint32x4_t b_abcd = initial_abcd;
    uint32x4_t b_efgh = initial_efgh;
    uint32x4_t a_sched2 = make_u32x4(0x80000000U, 0, 0, 0);
    uint32x4_t a_sched3 = make_u32x4(0, 0, 0, 32U * 8U);
    uint32x4_t b_sched2 = a_sched2;
    uint32x4_t b_sched3 = a_sched3;

#define BTC_SHA256_ARM_ROUND4_PAIR(a_schedule, b_schedule, offset) do { \
        const uint32x4_t k__ = vld1q_u32(&k_sha256_round_constants[(offset)]); \
        const uint32x4_t a_wk__ = vaddq_u32((a_schedule), k__); \
        const uint32x4_t b_wk__ = vaddq_u32((b_schedule), k__); \
        const uint32x4_t a_abcd_prev__ = a_abcd; \
        const uint32x4_t b_abcd_prev__ = b_abcd; \
        a_abcd = vsha256hq_u32(a_abcd_prev__, a_efgh, a_wk__); \
        b_abcd = vsha256hq_u32(b_abcd_prev__, b_efgh, b_wk__); \
        a_efgh = vsha256h2q_u32(a_efgh, a_abcd_prev__, a_wk__); \
        b_efgh = vsha256h2q_u32(b_efgh, b_abcd_prev__, b_wk__); \
    } while (0)

#define BTC_SHA256_ARM_ROUND4_WK_PAIR(wk_vec) do { \
        const uint32x4_t wk__ = (wk_vec); \
        const uint32x4_t a_abcd_prev__ = a_abcd; \
        const uint32x4_t b_abcd_prev__ = b_abcd; \
        a_abcd = vsha256hq_u32(a_abcd_prev__, a_efgh, wk__); \
        b_abcd = vsha256hq_u32(b_abcd_prev__, b_efgh, wk__); \
        a_efgh = vsha256h2q_u32(a_efgh, a_abcd_prev__, wk__); \
        b_efgh = vsha256h2q_u32(b_efgh, b_abcd_prev__, wk__); \
    } while (0)

    BTC_SHA256_ARM_ROUND4_PAIR(a_sched0, b_sched0, 0);
    BTC_SHA256_ARM_ROUND4_PAIR(a_sched1, b_sched1, 4);
    BTC_SHA256_ARM_ROUND4_WK_PAIR(vld1q_u32(k_second_hash_wk2));
    BTC_SHA256_ARM_ROUND4_WK_PAIR(vld1q_u32(k_second_hash_wk3));

#define BTC_SHA256_ARM_ROUNDS16_PAIR(offset) do { \
        a_sched0 = vsha256su1q_u32(vsha256su0q_u32(a_sched0, a_sched1), a_sched2, a_sched3); \
        b_sched0 = vsha256su1q_u32(vsha256su0q_u32(b_sched0, b_sched1), b_sched2, b_sched3); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched0, b_sched0, (offset)); \
        a_sched1 = vsha256su1q_u32(vsha256su0q_u32(a_sched1, a_sched2), a_sched3, a_sched0); \
        b_sched1 = vsha256su1q_u32(vsha256su0q_u32(b_sched1, b_sched2), b_sched3, b_sched0); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched1, b_sched1, (offset) + 4); \
        a_sched2 = vsha256su1q_u32(vsha256su0q_u32(a_sched2, a_sched3), a_sched0, a_sched1); \
        b_sched2 = vsha256su1q_u32(vsha256su0q_u32(b_sched2, b_sched3), b_sched0, b_sched1); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched2, b_sched2, (offset) + 8); \
        a_sched3 = vsha256su1q_u32(vsha256su0q_u32(a_sched3, a_sched0), a_sched1, a_sched2); \
        b_sched3 = vsha256su1q_u32(vsha256su0q_u32(b_sched3, b_sched0), b_sched1, b_sched2); \
        BTC_SHA256_ARM_ROUND4_PAIR(a_sched3, b_sched3, (offset) + 12); \
    } while (0)

    BTC_SHA256_ARM_ROUNDS16_PAIR(16);
    BTC_SHA256_ARM_ROUNDS16_PAIR(32);
    BTC_SHA256_ARM_ROUNDS16_PAIR(48);

#undef BTC_SHA256_ARM_ROUNDS16_PAIR
#undef BTC_SHA256_ARM_ROUND4_WK_PAIR
#undef BTC_SHA256_ARM_ROUND4_PAIR

    *a_out_abcd = vaddq_u32(a_abcd, initial_abcd);
    *a_out_efgh = vaddq_u32(a_efgh, initial_efgh);
    *b_out_abcd = vaddq_u32(b_abcd, initial_abcd);
    *b_out_efgh = vaddq_u32(b_efgh, initial_efgh);
}

BTC_ARM_ALWAYS_INLINE void sha256d_hash_nonce_pair_arm_raw(const sha256_midstate_t *state,
                                                           uint32_t tail0,
                                                           uint32_t tail1,
                                                           uint32_t tail2,
                                                           uint32_t nonce_a,
                                                           uint32_t nonce_b,
                                                           uint32_t out_a[8],
                                                           uint32_t out_b[8]) {
    const uint32x4_t start_abcd = vld1q_u32(&state->fast_state[0]);
    const uint32x4_t start_efgh = vld1q_u32(&state->fast_state[4]);
    uint32x4_t first_a_abcd;
    uint32x4_t first_a_efgh;
    uint32x4_t first_b_abcd;
    uint32x4_t first_b_efgh;
    uint32x4_t second_a_abcd;
    uint32x4_t second_a_efgh;
    uint32x4_t second_b_abcd;
    uint32x4_t second_b_efgh;

    sha256_arm_compress_btc_tail2(
        start_abcd,
        start_efgh,
        make_u32x4(tail0, tail1, tail2, bswap32(nonce_a)),
        make_u32x4(tail0, tail1, tail2, bswap32(nonce_b)),
        &first_a_abcd,
        &first_a_efgh,
        &first_b_abcd,
        &first_b_efgh);

    sha256_arm_compress_second_hash2(
        first_a_abcd,
        first_a_efgh,
        first_b_abcd,
        first_b_efgh,
        &second_a_abcd,
        &second_a_efgh,
        &second_b_abcd,
        &second_b_efgh);

    vst1q_u32(&out_a[0], second_a_abcd);
    vst1q_u32(&out_a[4], second_a_efgh);
    vst1q_u32(&out_b[0], second_b_abcd);
    vst1q_u32(&out_b[4], second_b_efgh);
}

BTC_ARM_ALWAYS_INLINE void sha256d_hash_tail_words_arm_raw(const sha256_midstate_t *state,
                                                           const uint32_t tail_words[4],
                                                           uint32_t out_words[8]) {
    uint32x4_t first_abcd = vld1q_u32(&state->fast_state[0]);
    uint32x4_t first_efgh = vld1q_u32(&state->fast_state[4]);
    uint32x4_t second_abcd;
    uint32x4_t second_efgh;

    sha256_arm_compress_btc_tail(
        first_abcd,
        first_efgh,
        make_u32x4(tail_words[0], tail_words[1], tail_words[2], tail_words[3]),
        &first_abcd,
        &first_efgh);

    sha256_arm_compress_second_hash(
        first_abcd,
        first_efgh,
        &second_abcd,
        &second_efgh);

    vst1q_u32(&out_words[0], second_abcd);
    vst1q_u32(&out_words[4], second_efgh);
}

void sha256d_80_midstate_hash_tail_words_arm_sha2(const sha256_midstate_t *state,
                                                  const uint32_t tail_words[4],
                                                  uint32_t out_words[8]) {
    sha256d_hash_tail_words_arm_raw(state, tail_words, out_words);
}

void sha256d_scan_nonce_range_arm_sha2(const sha256_midstate_t *state,
                                       const uint32_t base_tail_words[4],
                                       const uint32_t target_words[8],
                                       uint32_t start_nonce,
                                       uint32_t nonce_count,
                                       void *opaque,
                                       sha256d_scan_match_func_t on_match) {
    const uint32x4_t start_abcd = vld1q_u32(&state->fast_state[0]);
    const uint32x4_t start_efgh = vld1q_u32(&state->fast_state[4]);
    const uint32_t tail0 = base_tail_words[0];
    const uint32_t tail1 = base_tail_words[1];
    const uint32_t tail2 = base_tail_words[2];
    uint32_t hash_words_a[8];
    uint32_t hash_words_b[8];
    uint32_t i = 0;

    const uint32_t paired_count = nonce_count & ~1U;
    for (; i < paired_count; i += 2) {
        uint32_t nonce_a = start_nonce + i;
        uint32_t nonce_b = nonce_a + 1U;

        sha256d_hash_nonce_pair_arm_raw(
            state,
            tail0,
            tail1,
            tail2,
            nonce_a,
            nonce_b,
            hash_words_a,
            hash_words_b);

        if (hash_words_meet_target(hash_words_a, target_words) && on_match != NULL) {
            on_match(opaque, nonce_a, hash_words_a);
        }
        if (hash_words_meet_target(hash_words_b, target_words) && on_match != NULL) {
            on_match(opaque, nonce_b, hash_words_b);
        }
    }

    if (i < nonce_count) {
        uint32_t nonce = start_nonce + i;
        uint32x4_t first_abcd;
        uint32x4_t first_efgh;
        uint32x4_t second_abcd;
        uint32x4_t second_efgh;

        sha256_arm_compress_btc_tail(
            start_abcd,
            start_efgh,
            make_u32x4(tail0, tail1, tail2, bswap32(nonce)),
            &first_abcd,
            &first_efgh);

        sha256_arm_compress_second_hash(
            first_abcd,
            first_efgh,
            &second_abcd,
            &second_efgh);

        vst1q_u32(&hash_words_a[0], second_abcd);
        vst1q_u32(&hash_words_a[4], second_efgh);

        if (hash_words_meet_target(hash_words_a, target_words) && on_match != NULL) {
            on_match(opaque, nonce, hash_words_a);
        }
    }
}

void sha256d_80_midstate_hash_words_arm_sha2(const sha256_midstate_t *state,
                                             const uint8_t header_tail16[16],
                                             uint32_t out_words[8]) {
    uint32_t tail_words[4];
    for (int i = 0; i < 4; ++i) {
        tail_words[i] = load_be32(header_tail16 + i * 4);
    }
    sha256d_80_midstate_hash_tail_words_arm_sha2(state, tail_words, out_words);
}

#undef BTC_ARM_ALWAYS_INLINE
