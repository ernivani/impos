/* HMAC-SHA-256 + TLS 1.2 PRF */
#include <kernel/crypto.h>
#include <string.h>

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len,
                 uint8_t out[HMAC_SHA256_SIZE]) {
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    sha256_ctx_t ctx;

    /* If key > block size, hash it */
    uint8_t key_hash[SHA256_DIGEST_SIZE];
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, key_hash);
        key = key_hash;
        key_len = SHA256_DIGEST_SIZE;
    }

    /* ipad */
    memset(k_pad, 0x36, SHA256_BLOCK_SIZE);
    for (size_t i = 0; i < key_len; i++)
        k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, msg, msg_len);
    uint8_t inner[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, inner);

    /* opad */
    memset(k_pad, 0x5c, SHA256_BLOCK_SIZE);
    for (size_t i = 0; i < key_len; i++)
        k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}

/* P_SHA256(secret, seed) — TLS 1.2 PRF expansion */
static void p_sha256(const uint8_t *secret, size_t secret_len,
                     const uint8_t *seed, size_t seed_len,
                     uint8_t *out, size_t out_len) {
    uint8_t A[SHA256_DIGEST_SIZE];  /* A(i) */
    uint8_t tmp[SHA256_DIGEST_SIZE];

    /* A(1) = HMAC(secret, seed) */
    hmac_sha256(secret, secret_len, seed, seed_len, A);

    size_t pos = 0;
    while (pos < out_len) {
        /* HMAC(secret, A(i) || seed) */
        uint8_t concat[SHA256_DIGEST_SIZE + 256]; /* A + seed */
        memcpy(concat, A, SHA256_DIGEST_SIZE);
        size_t concat_len = SHA256_DIGEST_SIZE;
        if (seed_len <= sizeof(concat) - SHA256_DIGEST_SIZE) {
            memcpy(concat + SHA256_DIGEST_SIZE, seed, seed_len);
            concat_len += seed_len;
        }
        hmac_sha256(secret, secret_len, concat, concat_len, tmp);

        size_t copy = out_len - pos;
        if (copy > SHA256_DIGEST_SIZE)
            copy = SHA256_DIGEST_SIZE;
        memcpy(out + pos, tmp, copy);
        pos += copy;

        /* A(i+1) = HMAC(secret, A(i)) */
        hmac_sha256(secret, secret_len, A, SHA256_DIGEST_SIZE, A);
    }
}

void tls_prf(const uint8_t *secret, size_t secret_len,
             const char *label,
             const uint8_t *seed, size_t seed_len,
             uint8_t *out, size_t out_len) {
    /* Concatenate label + seed */
    size_t label_len = 0;
    while (label[label_len]) label_len++;

    uint8_t ls[256];
    if (label_len + seed_len > sizeof(ls)) return;
    memcpy(ls, label, label_len);
    memcpy(ls + label_len, seed, seed_len);

    p_sha256(secret, secret_len, ls, label_len + seed_len, out, out_len);
}
