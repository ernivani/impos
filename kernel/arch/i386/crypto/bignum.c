/* Big number arithmetic — 2048-bit integers for RSA */
#include <kernel/crypto.h>
#include <string.h>

void bn_zero(bignum_t *a) {
    memset(a->d, 0, sizeof(a->d));
    a->top = 0;
}

static void bn_fix_top(bignum_t *a) {
    int i = BN_WORDS - 1;
    while (i >= 0 && a->d[i] == 0) i--;
    a->top = i + 1;
}

void bn_from_bytes(bignum_t *a, const uint8_t *buf, size_t len) {
    bn_zero(a);
    /* buf is big-endian */
    for (size_t i = 0; i < len && i < BN_WORDS * 4; i++) {
        size_t byte_pos = len - 1 - i;
        int word_idx = i / 4;
        int bit_shift = (i % 4) * 8;
        a->d[word_idx] |= (uint32_t)buf[byte_pos] << bit_shift;
    }
    bn_fix_top(a);
}

void bn_to_bytes(const bignum_t *a, uint8_t *buf, size_t len) {
    memset(buf, 0, len);
    for (size_t i = 0; i < len && i < BN_WORDS * 4; i++) {
        size_t byte_pos = len - 1 - i;
        int word_idx = i / 4;
        int bit_shift = (i % 4) * 8;
        buf[byte_pos] = (a->d[word_idx] >> bit_shift) & 0xFF;
    }
}

int bn_cmp(const bignum_t *a, const bignum_t *b) {
    for (int i = BN_WORDS - 1; i >= 0; i--) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

void bn_add(bignum_t *r, const bignum_t *a, const bignum_t *b) {
    uint64_t carry = 0;
    for (int i = 0; i < BN_WORDS; i++) {
        uint64_t sum = (uint64_t)a->d[i] + b->d[i] + carry;
        r->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    bn_fix_top(r);
}

void bn_sub(bignum_t *r, const bignum_t *a, const bignum_t *b) {
    int64_t borrow = 0;
    for (int i = 0; i < BN_WORDS; i++) {
        int64_t diff = (int64_t)a->d[i] - b->d[i] - borrow;
        if (diff < 0) {
            diff += (int64_t)1 << 32;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->d[i] = (uint32_t)diff;
    }
    bn_fix_top(r);
}

/* Shift left by 1 bit */
static void bn_shl1(bignum_t *a) {
    uint32_t carry = 0;
    for (int i = 0; i < BN_WORDS; i++) {
        uint32_t next_carry = a->d[i] >> 31;
        a->d[i] = (a->d[i] << 1) | carry;
        carry = next_carry;
    }
    bn_fix_top(a);
}

/* Shift right by 1 bit */
static void bn_shr1(bignum_t *a) {
    uint32_t carry = 0;
    for (int i = BN_WORDS - 1; i >= 0; i--) {
        uint32_t next_carry = a->d[i] & 1;
        a->d[i] = (a->d[i] >> 1) | (carry << 31);
        carry = next_carry;
    }
    bn_fix_top(a);
}

/* Get bit at position n */
static int bn_bit(const bignum_t *a, int n) {
    int word = n / 32;
    int bit = n % 32;
    if (word >= BN_WORDS) return 0;
    return (a->d[word] >> bit) & 1;
}

/* Number of significant bits */
static int bn_num_bits(const bignum_t *a) {
    if (a->top == 0) return 0;
    int bits = (a->top - 1) * 32;
    uint32_t w = a->d[a->top - 1];
    while (w) { bits++; w >>= 1; }
    return bits;
}

/* r = a mod m  (for a < 2*m, simple subtract; general: binary division) */
void bn_mod(bignum_t *r, const bignum_t *a, const bignum_t *m) {
    bignum_t tmp;
    memcpy(&tmp, a, sizeof(bignum_t));

    if (bn_cmp(&tmp, m) < 0) {
        memcpy(r, &tmp, sizeof(bignum_t));
        return;
    }

    /* Binary long division */
    int nbits = bn_num_bits(&tmp);
    int mbits = bn_num_bits(m);

    bignum_t shifted_m;
    memcpy(&shifted_m, m, sizeof(bignum_t));

    /* Shift m left so MSBs align */
    int shift = nbits - mbits;
    for (int i = 0; i < shift; i++)
        bn_shl1(&shifted_m);

    for (int i = shift; i >= 0; i--) {
        if (bn_cmp(&tmp, &shifted_m) >= 0)
            bn_sub(&tmp, &tmp, &shifted_m);
        bn_shr1(&shifted_m);
    }

    memcpy(r, &tmp, sizeof(bignum_t));
}

/* r = (a * b) mod m — using Montgomery-like approach with modular addition */
void bn_mulmod(bignum_t *r, const bignum_t *a, const bignum_t *b, const bignum_t *m) {
    bignum_t result;
    bn_zero(&result);

    int nbits = bn_num_bits(b);

    for (int i = nbits - 1; i >= 0; i--) {
        /* result = result * 2 mod m */
        bn_shl1(&result);
        if (bn_cmp(&result, m) >= 0)
            bn_sub(&result, &result, m);

        /* if bit i of b is set: result = (result + a) mod m */
        if (bn_bit(b, i)) {
            bn_add(&result, &result, a);
            if (bn_cmp(&result, m) >= 0)
                bn_sub(&result, &result, m);
        }
    }

    memcpy(r, &result, sizeof(bignum_t));
}

/* r = base^exp mod mod — binary method (square-and-multiply) */
void bn_modexp(bignum_t *r, const bignum_t *base, const bignum_t *exp, const bignum_t *mod) {
    bignum_t result, b;

    /* result = 1 */
    bn_zero(&result);
    result.d[0] = 1;
    result.top = 1;

    /* b = base mod mod */
    bn_mod(&b, base, mod);

    int nbits = bn_num_bits(exp);

    for (int i = 0; i < nbits; i++) {
        if (bn_bit(exp, i)) {
            bn_mulmod(&result, &result, &b, mod);
        }
        bn_mulmod(&b, &b, &b, mod);
    }

    memcpy(r, &result, sizeof(bignum_t));
}
