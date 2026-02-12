#ifndef _KERNEL_EC_H
#define _KERNEL_EC_H

#include <stdint.h>
#include <stddef.h>

/* P-256 (secp256r1) elliptic curve for ECDHE key exchange.
 * Field: GF(p) where p = 2^256 - 2^224 + 2^192 + 2^96 - 1
 * Curve: y^2 = x^3 - 3x + b (mod p) */

/* 256-bit field element stored as 8 x 32-bit words (little-endian) */
typedef struct {
    uint32_t d[8];
} ec_fe_t;

/* Affine point on P-256 */
typedef struct {
    ec_fe_t x;
    ec_fe_t y;
    int     infinity;  /* 1 = point at infinity */
} ec_point_t;

/* Field arithmetic (mod p) */
void ec_fe_from_bytes(ec_fe_t *a, const uint8_t *buf, size_t len);
void ec_fe_to_bytes(const ec_fe_t *a, uint8_t *buf);  /* 32 bytes, big-endian */
int  ec_fe_is_zero(const ec_fe_t *a);
void ec_fe_add(ec_fe_t *r, const ec_fe_t *a, const ec_fe_t *b);
void ec_fe_sub(ec_fe_t *r, const ec_fe_t *a, const ec_fe_t *b);
void ec_fe_mul(ec_fe_t *r, const ec_fe_t *a, const ec_fe_t *b);
void ec_fe_sqr(ec_fe_t *r, const ec_fe_t *a);
void ec_fe_inv(ec_fe_t *r, const ec_fe_t *a);  /* Fermat's little theorem */

/* Point operations */
void ec_point_double(ec_point_t *r, const ec_point_t *p);
void ec_point_add(ec_point_t *r, const ec_point_t *p, const ec_point_t *q);
void ec_scalar_mul(ec_point_t *r, const uint8_t *k, size_t k_len, const ec_point_t *p);

/* Get the P-256 base point G */
void ec_get_generator(ec_point_t *g);

/* Generate an ECDHE keypair: private key (32 bytes), public point */
void ec_generate_keypair(uint8_t *privkey, ec_point_t *pubkey);

/* Compute ECDH shared secret: result = privkey * peer_pubkey */
void ec_compute_shared(ec_fe_t *shared_x, const uint8_t *privkey,
                       const ec_point_t *peer_pubkey);

#endif
