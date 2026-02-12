/* P-256 (secp256r1) elliptic curve — ECDHE key exchange support */
#include <kernel/ec.h>
#include <kernel/crypto.h>
#include <string.h>

/* P-256 prime: p = 2^256 - 2^224 + 2^192 + 2^96 - 1 */
static const ec_fe_t P256_P = {{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF
}};

/* Generator point G (x coordinate) */
static const ec_fe_t P256_GX = {{
    0xD898C296, 0xF4A13945, 0x2DEB33A0, 0x77037D81,
    0x63A440F2, 0xF8BCE6E5, 0xE12C4247, 0x6B17D1F2
}};

/* Generator point G (y coordinate) */
static const ec_fe_t P256_GY = {{
    0x37BF51F5, 0xCBB64068, 0x6B315ECE, 0x2BCE3357,
    0x7C0F9E16, 0x8EE7EB4A, 0xFE1A7F9B, 0x4FE342E2
}};

/* ── Field element helpers ─────────────────────────────────────── */

static int fe_is_zero(const ec_fe_t *a) {
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) v |= a->d[i];
    return v == 0;
}

int ec_fe_is_zero(const ec_fe_t *a) { return fe_is_zero(a); }

static int fe_cmp(const ec_fe_t *a, const ec_fe_t *b) {
    for (int i = 7; i >= 0; i--) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

void ec_fe_from_bytes(ec_fe_t *a, const uint8_t *buf, size_t len) {
    memset(a, 0, sizeof(*a));
    /* buf is big-endian, d[] is little-endian words */
    for (size_t i = 0; i < len && i < 32; i++) {
        size_t byte_pos = len - 1 - i;
        int word = i / 4;
        int shift = (i % 4) * 8;
        a->d[word] |= (uint32_t)buf[byte_pos] << shift;
    }
}

void ec_fe_to_bytes(const ec_fe_t *a, uint8_t *buf) {
    /* 32 bytes, big-endian */
    for (int i = 0; i < 32; i++) {
        int byte_pos = 31 - i;
        int word = i / 4;
        int shift = (i % 4) * 8;
        buf[byte_pos] = (a->d[word] >> shift) & 0xFF;
    }
}

/* ── Modular arithmetic (mod p) ────────────────────────────────── */

/* r = a + b mod p */
void ec_fe_add(ec_fe_t *r, const ec_fe_t *a, const ec_fe_t *b) {
    uint64_t carry = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t sum = (uint64_t)a->d[i] + b->d[i] + carry;
        r->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    /* Reduce: if carry or r >= p, subtract p */
    if (carry || fe_cmp(r, &P256_P) >= 0) {
        int64_t borrow = 0;
        for (int i = 0; i < 8; i++) {
            int64_t diff = (int64_t)r->d[i] - P256_P.d[i] - borrow;
            if (diff < 0) { diff += (int64_t)1 << 32; borrow = 1; }
            else borrow = 0;
            r->d[i] = (uint32_t)diff;
        }
    }
}

/* r = a - b mod p */
void ec_fe_sub(ec_fe_t *r, const ec_fe_t *a, const ec_fe_t *b) {
    int64_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        int64_t diff = (int64_t)a->d[i] - b->d[i] - borrow;
        if (diff < 0) { diff += (int64_t)1 << 32; borrow = 1; }
        else borrow = 0;
        r->d[i] = (uint32_t)diff;
    }
    /* If borrow, add p back */
    if (borrow) {
        uint64_t carry = 0;
        for (int i = 0; i < 8; i++) {
            uint64_t sum = (uint64_t)r->d[i] + P256_P.d[i] + carry;
            r->d[i] = (uint32_t)sum;
            carry = sum >> 32;
        }
    }
}

/* r = a * b mod p — schoolbook multiply + Solinas reduction for P-256 */
void ec_fe_mul(ec_fe_t *r, const ec_fe_t *a, const ec_fe_t *b) {
    /* Full 512-bit product */
    uint32_t t[16];
    memset(t, 0, sizeof(t));

    for (int i = 0; i < 8; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 8; j++) {
            uint64_t prod = (uint64_t)a->d[i] * b->d[j] + t[i+j] + carry;
            t[i+j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        t[i+8] = (uint32_t)carry;
    }

    /* Solinas reduction for P-256:
     * p = 2^256 - 2^224 + 2^192 + 2^96 - 1
     * We decompose the 512-bit product and reduce using the special form of p.
     *
     * Let c = t[0..15] represent the 512-bit product.
     * Define 256-bit "slices":
     *   s1  = (c7,  c6,  c5,  c4,  c3,  c2,  c1,  c0)
     *   s2  = (c15, c14, c13, c12, c11, 0,   0,   0  )
     *   s3  = (0,   c15, c14, c13, c12, 0,   0,   0  )
     *   s4  = (c15, c14, 0,   0,   0,   c10, c9,  c8 )
     *   s5  = (c8,  c13, c15, c14, c13, c11, c10, c9 )
     *   s6  = (c10, c8,  0,   0,   0,   c13, c12, c11)
     *   s7  = (c11, c9,  0,   0,   c15, c14, c13, c12)
     *   s8  = (c12, 0,   c10, c9,  c8,  c15, c14, c13)
     *   s9  = (c13, 0,   c11, c10, c9,  0,   c15, c14)
     *
     * result = s1 + 2*s2 + 2*s3 + s4 + s5 - s6 - s7 - s8 - s9  (mod p)
     */

    /* Use 64-bit accumulators with signed values to avoid intermediate reductions */
    int64_t acc[8];
    memset(acc, 0, sizeof(acc));

    /* s1 = (c7, c6, c5, c4, c3, c2, c1, c0) */
    for (int i = 0; i < 8; i++) acc[i] += t[i];

    /* 2*s2 = 2*(c15, c14, c13, c12, c11, 0, 0, 0) */
    acc[3] += 2*(int64_t)t[11];
    acc[4] += 2*(int64_t)t[12];
    acc[5] += 2*(int64_t)t[13];
    acc[6] += 2*(int64_t)t[14];
    acc[7] += 2*(int64_t)t[15];

    /* 2*s3 = 2*(0, c15, c14, c13, c12, 0, 0, 0) */
    acc[3] += 2*(int64_t)t[12];
    acc[4] += 2*(int64_t)t[13];
    acc[5] += 2*(int64_t)t[14];
    acc[6] += 2*(int64_t)t[15];

    /* s4 = (c15, c14, 0, 0, 0, c10, c9, c8) */
    acc[0] += t[8];
    acc[1] += t[9];
    acc[2] += t[10];
    acc[6] += t[14];
    acc[7] += t[15];

    /* s5 = (c8, c13, c15, c14, c13, c11, c10, c9) */
    acc[0] += t[9];
    acc[1] += t[10];
    acc[2] += t[11];
    acc[3] += t[13];
    acc[4] += t[14];
    acc[5] += t[15];
    acc[6] += t[13];
    acc[7] += t[8];

    /* -s6 = -(c10, c8, 0, 0, 0, c13, c12, c11) */
    acc[0] -= t[11];
    acc[1] -= t[12];
    acc[2] -= t[13];
    acc[6] -= t[8];
    acc[7] -= t[10];

    /* -s7 = -(c11, c9, 0, 0, c15, c14, c13, c12) */
    acc[0] -= t[12];
    acc[1] -= t[13];
    acc[2] -= t[14];
    acc[3] -= t[15];
    acc[6] -= t[9];
    acc[7] -= t[11];

    /* -s8 = -(c12, 0, c10, c9, c8, c15, c14, c13) */
    acc[0] -= t[13];
    acc[1] -= t[14];
    acc[2] -= t[15];
    acc[3] -= t[8];
    acc[4] -= t[9];
    acc[5] -= t[10];
    acc[7] -= t[12];

    /* -s9 = -(c13, 0, c11, c10, c9, 0, c15, c14) */
    acc[0] -= t[14];
    acc[1] -= t[15];
    acc[3] -= t[9];
    acc[4] -= t[10];
    acc[5] -= t[11];
    acc[7] -= t[13];

    /* Propagate carries through the accumulator */
    int64_t carry = 0;
    for (int i = 0; i < 8; i++) {
        acc[i] += carry;
        r->d[i] = (uint32_t)(acc[i] & 0xFFFFFFFF);
        carry = acc[i] >> 32;
        /* Sign-extend for negative values */
        if (acc[i] < 0 && r->d[i] != 0)
            carry--;
    }

    /* Final reduction: add/sub multiples of p until 0 <= r < p */
    /* carry can be -4..5 roughly, so we loop a few times */
    while (carry < 0) {
        /* r += p (carry is negative, so r < 0 in 256-bit extended) */
        int64_t c = 0;
        for (int i = 0; i < 8; i++) {
            c += (int64_t)r->d[i] + P256_P.d[i];
            r->d[i] = (uint32_t)c;
            c >>= 32;
        }
        carry += c;
    }
    while (carry > 0 || fe_cmp(r, &P256_P) >= 0) {
        int64_t b = 0;
        for (int i = 0; i < 8; i++) {
            b += (int64_t)r->d[i] - P256_P.d[i];
            r->d[i] = (uint32_t)b;
            b >>= 32;
        }
        carry += b;
    }
    /* One final check in case r is still >= p */
    if (fe_cmp(r, &P256_P) >= 0) {
        int64_t b = 0;
        for (int i = 0; i < 8; i++) {
            b += (int64_t)r->d[i] - P256_P.d[i];
            r->d[i] = (uint32_t)b;
            b >>= 32;
        }
    }
}

/* r = a^2 mod p — just calls mul for simplicity */
void ec_fe_sqr(ec_fe_t *r, const ec_fe_t *a) {
    ec_fe_mul(r, a, a);
}

/* r = a^(-1) mod p via Fermat: a^(p-2) mod p */
void ec_fe_inv(ec_fe_t *r, const ec_fe_t *a) {
    /* p-2 = FFFFFFFF 00000001 00000000 00000000 00000000 FFFFFFFF FFFFFFFF FFFFFFFD */
    /* Use square-and-multiply with the binary representation of p-2 */
    ec_fe_t result, base;
    memcpy(&base, a, sizeof(ec_fe_t));

    /* Start with result = 1 */
    memset(&result, 0, sizeof(ec_fe_t));
    result.d[0] = 1;

    /* p-2 in binary, process from bit 255 down to 0 */
    uint32_t exp[8] = {
        0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF
    };

    for (int i = 255; i >= 0; i--) {
        ec_fe_sqr(&result, &result);
        int word = i / 32;
        int bit = i % 32;
        if (exp[word] & (1U << bit))
            ec_fe_mul(&result, &result, a);
    }

    memcpy(r, &result, sizeof(ec_fe_t));
}

/* ── Point operations (affine coordinates) ─────────────────────── */

void ec_get_generator(ec_point_t *g) {
    memcpy(&g->x, &P256_GX, sizeof(ec_fe_t));
    memcpy(&g->y, &P256_GY, sizeof(ec_fe_t));
    g->infinity = 0;
}

/* r = 2*p */
void ec_point_double(ec_point_t *r, const ec_point_t *p) {
    if (p->infinity || fe_is_zero(&p->y)) {
        r->infinity = 1;
        return;
    }

    /* lambda = (3*x^2 + a) / (2*y), where a = -3 for P-256 */
    ec_fe_t x2, num, denom, lambda, tmp;

    /* x^2 */
    ec_fe_sqr(&x2, &p->x);

    /* 3*x^2 */
    ec_fe_add(&num, &x2, &x2);
    ec_fe_add(&num, &num, &x2);

    /* 3*x^2 - 3 = 3*(x^2 - 1) ... actually a = -3, so num = 3*x^2 + a = 3*x^2 - 3 */
    ec_fe_t three;
    memset(&three, 0, sizeof(ec_fe_t));
    three.d[0] = 3;
    ec_fe_sub(&num, &num, &three);

    /* 2*y */
    ec_fe_add(&denom, &p->y, &p->y);

    /* lambda = num / denom */
    ec_fe_inv(&tmp, &denom);
    ec_fe_mul(&lambda, &num, &tmp);

    /* x_r = lambda^2 - 2*x */
    ec_fe_sqr(&r->x, &lambda);
    ec_fe_sub(&r->x, &r->x, &p->x);
    ec_fe_sub(&r->x, &r->x, &p->x);

    /* y_r = lambda * (x - x_r) - y */
    ec_fe_sub(&tmp, &p->x, &r->x);
    ec_fe_mul(&r->y, &lambda, &tmp);
    ec_fe_sub(&r->y, &r->y, &p->y);

    r->infinity = 0;
}

/* r = p + q */
void ec_point_add(ec_point_t *r, const ec_point_t *p, const ec_point_t *q) {
    if (p->infinity) { memcpy(r, q, sizeof(ec_point_t)); return; }
    if (q->infinity) { memcpy(r, p, sizeof(ec_point_t)); return; }

    /* Check if p == q */
    if (fe_cmp(&p->x, &q->x) == 0) {
        if (fe_cmp(&p->y, &q->y) == 0) {
            ec_point_double(r, p);
            return;
        }
        /* p == -q → result is point at infinity */
        r->infinity = 1;
        return;
    }

    /* lambda = (y2 - y1) / (x2 - x1) */
    ec_fe_t dy, dx, inv_dx, lambda, tmp;
    ec_fe_sub(&dy, &q->y, &p->y);
    ec_fe_sub(&dx, &q->x, &p->x);
    ec_fe_inv(&inv_dx, &dx);
    ec_fe_mul(&lambda, &dy, &inv_dx);

    /* x_r = lambda^2 - x1 - x2 */
    ec_fe_sqr(&r->x, &lambda);
    ec_fe_sub(&r->x, &r->x, &p->x);
    ec_fe_sub(&r->x, &r->x, &q->x);

    /* y_r = lambda * (x1 - x_r) - y1 */
    ec_fe_sub(&tmp, &p->x, &r->x);
    ec_fe_mul(&r->y, &lambda, &tmp);
    ec_fe_sub(&r->y, &r->y, &p->y);

    r->infinity = 0;
}

/* r = k * p — double-and-add, constant-ish time (not side-channel hardened) */
void ec_scalar_mul(ec_point_t *r, const uint8_t *k, size_t k_len, const ec_point_t *p) {
    ec_point_t result;
    result.infinity = 1;

    ec_point_t base;
    memcpy(&base, p, sizeof(ec_point_t));

    /* k is big-endian scalar */
    for (int byte_idx = (int)k_len - 1; byte_idx >= 0; byte_idx--) {
        for (int bit = 0; bit < 8; bit++) {
            if (k[byte_idx] & (1 << bit)) {
                ec_point_t tmp;
                ec_point_add(&tmp, &result, &base);
                memcpy(&result, &tmp, sizeof(ec_point_t));
            }
            ec_point_t tmp;
            ec_point_double(&tmp, &base);
            memcpy(&base, &tmp, sizeof(ec_point_t));
        }
    }

    memcpy(r, &result, sizeof(ec_point_t));
}

/* ── ECDHE helpers ─────────────────────────────────────────────── */

void ec_generate_keypair(uint8_t *privkey, ec_point_t *pubkey) {
    /* Generate random 32-byte private key */
    prng_random(privkey, 32);

    /* Clamp: ensure privkey < order n (just set top bit to 0 for safety) */
    privkey[0] &= 0x7F;
    /* Ensure non-zero */
    privkey[31] |= 0x01;

    /* pubkey = privkey * G */
    ec_point_t g;
    ec_get_generator(&g);
    ec_scalar_mul(pubkey, privkey, 32, &g);
}

void ec_compute_shared(ec_fe_t *shared_x, const uint8_t *privkey,
                       const ec_point_t *peer_pubkey) {
    ec_point_t result;
    ec_scalar_mul(&result, privkey, 32, peer_pubkey);
    memcpy(shared_x, &result.x, sizeof(ec_fe_t));
}
