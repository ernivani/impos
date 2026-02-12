/* Minimal ASN.1 DER parser — extract RSA public key from X.509 cert */
#include <kernel/crypto.h>
#include <kernel/io.h>
#include <string.h>

/* ASN.1 tag types */
#define ASN1_INTEGER     0x02
#define ASN1_BITSTRING   0x03
#define ASN1_OCTETSTRING 0x04
#define ASN1_NULL        0x05
#define ASN1_OID         0x06
#define ASN1_SEQUENCE    0x30
#define ASN1_SET         0x31

/* OID for rsaEncryption: 1.2.840.113549.1.1.1 */
static const uint8_t oid_rsa_enc[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
};

/* Parse DER tag+length, return pointer past length field.
 * Sets *tag and *len. Returns NULL on error. */
static const uint8_t *der_read_tl(const uint8_t *p, const uint8_t *end,
                                   uint8_t *tag, size_t *len) {
    if (p >= end) return NULL;
    *tag = *p++;

    if (p >= end) return NULL;
    if (*p < 0x80) {
        *len = *p++;
    } else {
        int nbytes = *p++ & 0x7F;
        if (nbytes > 4 || p + nbytes > end) return NULL;
        *len = 0;
        for (int i = 0; i < nbytes; i++)
            *len = (*len << 8) | *p++;
    }

    if (p + *len > end) return NULL;
    return p;
}

/* Skip a DER element (read tag+length, advance past content) */
static const uint8_t *der_skip(const uint8_t *p, const uint8_t *end) {
    uint8_t tag;
    size_t len;
    p = der_read_tl(p, end, &tag, &len);
    if (!p) return NULL;
    return p + len;
}

/* Find SubjectPublicKeyInfo containing rsaEncryption OID */
static const uint8_t *find_rsa_spki(const uint8_t *cert, size_t cert_len,
                                     size_t *spki_len) {
    const uint8_t *p = cert;
    const uint8_t *end = cert + cert_len;
    uint8_t tag;
    size_t len;

    /* Certificate ::= SEQUENCE { tbsCertificate, ... } */
    p = der_read_tl(p, end, &tag, &len);
    if (!p || tag != ASN1_SEQUENCE) return NULL;
    end = p + len;

    /* TBSCertificate ::= SEQUENCE { ... } */
    p = der_read_tl(p, end, &tag, &len);
    if (!p || tag != ASN1_SEQUENCE) return NULL;
    const uint8_t *tbs_end = p + len;

    /* Skip version [0] EXPLICIT (if present) */
    if (*p == 0xA0) {
        p = der_skip(p, tbs_end);
        if (!p) return NULL;
    }

    /* Skip serialNumber INTEGER */
    p = der_skip(p, tbs_end);
    if (!p) return NULL;

    /* Skip signature AlgorithmIdentifier SEQUENCE */
    p = der_skip(p, tbs_end);
    if (!p) return NULL;

    /* Skip issuer Name SEQUENCE */
    p = der_skip(p, tbs_end);
    if (!p) return NULL;

    /* Skip validity SEQUENCE */
    p = der_skip(p, tbs_end);
    if (!p) return NULL;

    /* Skip subject Name SEQUENCE */
    p = der_skip(p, tbs_end);
    if (!p) return NULL;

    /* SubjectPublicKeyInfo SEQUENCE — this is what we want */
    p = der_read_tl(p, tbs_end, &tag, &len);
    if (!p || tag != ASN1_SEQUENCE) return NULL;

    *spki_len = len;
    return p;  /* points to SPKI contents */
}

/* Extract RSA modulus and exponent from SubjectPublicKeyInfo */
static int parse_rsa_pubkey(const uint8_t *spki, size_t spki_len,
                            rsa_pubkey_t *key) {
    const uint8_t *p = spki;
    const uint8_t *end = spki + spki_len;
    uint8_t tag;
    size_t len;

    /* AlgorithmIdentifier SEQUENCE */
    p = der_read_tl(p, end, &tag, &len);
    if (!p || tag != ASN1_SEQUENCE) return -1;
    const uint8_t *alg_end = p + len;

    /* Check OID is rsaEncryption */
    p = der_read_tl(p, alg_end, &tag, &len);
    if (!p || tag != ASN1_OID) return -1;
    if (len != sizeof(oid_rsa_enc) || memcmp(p, oid_rsa_enc, len) != 0)
        return -2;  /* Not RSA */
    p = alg_end;  /* skip past AlgorithmIdentifier */

    /* BIT STRING containing RSAPublicKey */
    p = der_read_tl(p, end, &tag, &len);
    if (!p || tag != ASN1_BITSTRING) return -1;
    if (len < 1 || *p != 0x00) return -1;  /* unused bits must be 0 */
    p++; len--;  /* skip unused-bits byte */

    /* RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER } */
    const uint8_t *rsa_end = p + len;
    p = der_read_tl(p, rsa_end, &tag, &len);
    if (!p || tag != ASN1_SEQUENCE) return -1;

    /* Modulus */
    p = der_read_tl(p, rsa_end, &tag, &len);
    if (!p || tag != ASN1_INTEGER) return -1;
    /* Skip leading zero byte if present (sign byte) */
    const uint8_t *mod_data = p;
    size_t mod_len = len;
    if (mod_len > 0 && mod_data[0] == 0x00) {
        mod_data++;
        mod_len--;
    }
    bn_from_bytes(&key->n, mod_data, mod_len);
    key->n_bytes = mod_len;
    p += len;

    /* Public exponent */
    p = der_read_tl(p, rsa_end, &tag, &len);
    if (!p || tag != ASN1_INTEGER) return -1;
    const uint8_t *exp_data = p;
    size_t exp_len = len;
    if (exp_len > 0 && exp_data[0] == 0x00) {
        exp_data++;
        exp_len--;
    }
    bn_from_bytes(&key->e, exp_data, exp_len);

    return 0;
}

int asn1_extract_rsa_pubkey(const uint8_t *cert, size_t cert_len,
                            rsa_pubkey_t *key) {
    size_t spki_len;
    const uint8_t *spki = find_rsa_spki(cert, cert_len, &spki_len);
    if (!spki) {
        DBG("asn1: failed to find SPKI in certificate");
        return -1;
    }
    int ret = parse_rsa_pubkey(spki, spki_len, key);
    if (ret < 0) {
        DBG("asn1: failed to parse RSA pubkey (%d)", ret);
    }
    return ret;
}
