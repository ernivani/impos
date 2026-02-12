#ifndef _KERNEL_CRYPTO_H
#define _KERNEL_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/* ── SHA-256 ─────────────────────────────────────────────────── */

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

/* ── HMAC-SHA-256 ────────────────────────────────────────────── */

#define HMAC_SHA256_SIZE 32

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len,
                 uint8_t out[HMAC_SHA256_SIZE]);

/* TLS 1.2 PRF (P_SHA256) */
void tls_prf(const uint8_t *secret, size_t secret_len,
             const char *label,
             const uint8_t *seed, size_t seed_len,
             uint8_t *out, size_t out_len);

/* ── AES-128 ─────────────────────────────────────────────────── */

#define AES_BLOCK_SIZE 16
#define AES128_KEY_SIZE 16
#define AES128_ROUNDS 10
#define AES128_EXPANDED_KEY_SIZE (4 * (AES128_ROUNDS + 1))  /* 44 uint32_t */

typedef struct {
    uint32_t rk[AES128_EXPANDED_KEY_SIZE];
} aes128_ctx_t;

void aes128_init(aes128_ctx_t *ctx, const uint8_t key[AES128_KEY_SIZE]);
void aes128_encrypt_block(const aes128_ctx_t *ctx,
                          const uint8_t in[AES_BLOCK_SIZE],
                          uint8_t out[AES_BLOCK_SIZE]);
void aes128_decrypt_block(const aes128_ctx_t *ctx,
                          const uint8_t in[AES_BLOCK_SIZE],
                          uint8_t out[AES_BLOCK_SIZE]);

/* CBC mode — caller manages IV, padding */
void aes128_cbc_encrypt(const aes128_ctx_t *ctx,
                        const uint8_t *iv,
                        const uint8_t *plain, size_t len,
                        uint8_t *cipher);
void aes128_cbc_decrypt(const aes128_ctx_t *ctx,
                        const uint8_t *iv,
                        const uint8_t *cipher, size_t len,
                        uint8_t *plain);

/* ── Big-number (2048-bit) ───────────────────────────────────── */

#define BN_WORDS 64   /* 64 × 32 = 2048 bits */

typedef struct {
    uint32_t d[BN_WORDS];
    int      top;     /* index of highest non-zero word + 1 */
} bignum_t;

void bn_zero(bignum_t *a);
void bn_from_bytes(bignum_t *a, const uint8_t *buf, size_t len);
void bn_to_bytes(const bignum_t *a, uint8_t *buf, size_t len);
int  bn_cmp(const bignum_t *a, const bignum_t *b);
void bn_add(bignum_t *r, const bignum_t *a, const bignum_t *b);
void bn_sub(bignum_t *r, const bignum_t *a, const bignum_t *b);
void bn_mod(bignum_t *r, const bignum_t *a, const bignum_t *m);
void bn_mulmod(bignum_t *r, const bignum_t *a, const bignum_t *b, const bignum_t *m);
void bn_modexp(bignum_t *r, const bignum_t *base, const bignum_t *exp, const bignum_t *mod);

/* ── RSA (public key only) ───────────────────────────────────── */

typedef struct {
    bignum_t n;       /* modulus */
    bignum_t e;       /* public exponent (typically 65537) */
    size_t   n_bytes; /* byte length of modulus */
} rsa_pubkey_t;

/* PKCS#1 v1.5 encrypt: out must be n_bytes long */
int rsa_encrypt(const rsa_pubkey_t *key,
                const uint8_t *msg, size_t msg_len,
                uint8_t *out, size_t out_len);

/* ── ASN.1 / X.509 ──────────────────────────────────────────── */

/* Extract RSA public key from a DER-encoded X.509 certificate */
int asn1_extract_rsa_pubkey(const uint8_t *cert, size_t cert_len,
                            rsa_pubkey_t *key);

/* ── CSPRNG ──────────────────────────────────────────────────── */

void prng_init(void);
void prng_seed(const uint8_t *data, size_t len);
void prng_random(uint8_t *buf, size_t len);

#endif
