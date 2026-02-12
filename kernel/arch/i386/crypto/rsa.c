/* RSA PKCS#1 v1.5 encryption (public key only) */
#include <kernel/crypto.h>
#include <string.h>

int rsa_encrypt(const rsa_pubkey_t *key,
                const uint8_t *msg, size_t msg_len,
                uint8_t *out, size_t out_len) {
    size_t k = key->n_bytes;
    if (out_len < k) return -1;
    if (msg_len > k - 11) return -1;  /* PKCS#1 overhead: 0x00 || 0x02 || PS || 0x00 */

    /* Build PKCS#1 v1.5 type 2 block:
     *   0x00 || 0x02 || PS (non-zero random) || 0x00 || msg */
    uint8_t em[256];  /* max 2048-bit = 256 bytes */
    if (k > sizeof(em)) return -1;

    em[0] = 0x00;
    em[1] = 0x02;

    size_t ps_len = k - msg_len - 3;
    prng_random(em + 2, ps_len);

    /* Ensure PS has no zero bytes */
    for (size_t i = 0; i < ps_len; i++) {
        while (em[2 + i] == 0) {
            prng_random(&em[2 + i], 1);
        }
    }

    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, msg, msg_len);

    /* Convert to bignum and compute: c = m^e mod n */
    bignum_t m, c;
    bn_from_bytes(&m, em, k);
    bn_modexp(&c, &m, &key->e, &key->n);
    bn_to_bytes(&c, out, k);

    return 0;
}
