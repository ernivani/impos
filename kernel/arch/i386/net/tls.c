/* TLS 1.2 client — record layer, handshake, HTTPS GET */
#include <kernel/tls.h>
#include <kernel/crypto.h>
#include <kernel/ec.h>
#include <kernel/socket.h>
#include <kernel/dns.h>
#include <kernel/net.h>
#include <kernel/endian.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Helpers ──────────────────────────────────────────────────── */

static void put_be16(uint8_t *p, uint16_t v) {
    p[0] = v >> 8; p[1] = v;
}

static void put_be24(uint8_t *p, uint32_t v) {
    p[0] = (v >> 16) & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = v & 0xFF;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static uint16_t get_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get_be24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/* ── Raw TCP I/O with timeout ────────────────────────────────── */

/* Read exactly `len` bytes from socket, blocking with timeout */
static int sock_read_full(int fd, uint8_t *buf, size_t len, uint32_t timeout_ms) {
    size_t got = 0;
    while (got < len) {
        int n = socket_recv(fd, buf + got, len - got, timeout_ms);
        if (n <= 0) return -1;
        got += n;
    }
    return (int)got;
}

/* ── TLS Record Layer ────────────────────────────────────────── */

/* Send a TLS record (plaintext if not encrypted, else encrypt) */
static int tls_send_record(tls_conn_t *conn, uint8_t type,
                           const uint8_t *data, size_t len) {
    if (!conn->client_encrypted) {
        /* Plaintext record: type(1) + version(2) + length(2) + data */
        uint8_t hdr[5];
        hdr[0] = type;
        put_be16(hdr + 1, TLS_VERSION_1_2);
        put_be16(hdr + 3, (uint16_t)len);
        if (socket_send(conn->sock_fd, hdr, 5) < 0) return -1;
        if (len > 0 && socket_send(conn->sock_fd, data, len) < 0) return -1;
        return 0;
    }

    /* Encrypted record: MAC-then-encrypt (AES-128-CBC + HMAC-SHA-256) */

    /* Compute MAC over: seq_num(8) || type(1) || version(2) || length(2) || data */
    uint8_t mac_input[13];
    put_be64(mac_input, conn->client_seq);
    mac_input[8] = type;
    put_be16(mac_input + 9, TLS_VERSION_1_2);
    put_be16(mac_input + 11, (uint16_t)len);

    sha256_ctx_t hmac_h;
    /* Manual HMAC to handle two-part data (header + body) */
    uint8_t k_ipad[SHA256_BLOCK_SIZE], k_opad[SHA256_BLOCK_SIZE];
    memset(k_ipad, 0x36, SHA256_BLOCK_SIZE);
    memset(k_opad, 0x5c, SHA256_BLOCK_SIZE);
    for (int i = 0; i < 32; i++) {
        k_ipad[i] ^= conn->client_write_mac_key[i];
        k_opad[i] ^= conn->client_write_mac_key[i];
    }

    sha256_init(&hmac_h);
    sha256_update(&hmac_h, k_ipad, SHA256_BLOCK_SIZE);
    sha256_update(&hmac_h, mac_input, 13);
    sha256_update(&hmac_h, data, len);
    uint8_t inner_hash[32];
    sha256_final(&hmac_h, inner_hash);

    uint8_t mac[32];
    sha256_init(&hmac_h);
    sha256_update(&hmac_h, k_opad, SHA256_BLOCK_SIZE);
    sha256_update(&hmac_h, inner_hash, 32);
    sha256_final(&hmac_h, mac);

    conn->client_seq++;

    /* Build plaintext = data || mac || padding */
    size_t payload_len = len + 32;  /* data + MAC */
    size_t pad_needed = AES_BLOCK_SIZE - (payload_len % AES_BLOCK_SIZE);
    if (pad_needed == 0) pad_needed = AES_BLOCK_SIZE;
    size_t total = payload_len + pad_needed;

    uint8_t *plain = malloc(total);
    if (!plain) return -1;
    memcpy(plain, data, len);
    memcpy(plain + len, mac, 32);
    uint8_t pad_byte = (uint8_t)(pad_needed - 1);
    memset(plain + payload_len, pad_byte, pad_needed);

    /* Generate random IV */
    uint8_t iv[AES_BLOCK_SIZE];
    prng_random(iv, AES_BLOCK_SIZE);

    /* Encrypt */
    uint8_t *cipher = malloc(total);
    if (!cipher) { free(plain); return -1; }
    aes128_cbc_encrypt(&conn->client_aes, iv, plain, total, cipher);
    free(plain);

    /* Send record: hdr(5) + IV(16) + cipher */
    size_t record_len = AES_BLOCK_SIZE + total;
    uint8_t hdr[5];
    hdr[0] = type;
    put_be16(hdr + 1, TLS_VERSION_1_2);
    put_be16(hdr + 3, (uint16_t)record_len);

    DBG("tls: enc record type=%d plain_len=%u pad=%u cipher_len=%u rec_len=%u seq=%u",
        type, (unsigned)len, (unsigned)pad_needed, (unsigned)total,
        (unsigned)record_len, (unsigned)conn->client_seq);

    int ret = 0;
    if (socket_send(conn->sock_fd, hdr, 5) < 0) ret = -1;
    if (ret == 0 && socket_send(conn->sock_fd, iv, AES_BLOCK_SIZE) < 0) ret = -1;
    if (ret == 0 && socket_send(conn->sock_fd, cipher, total) < 0) ret = -1;

    free(cipher);
    return ret;
}

/* Receive and decrypt a TLS record. Returns content type, fills buf, sets *out_len.
 * Returns -1 on error. */
static int tls_recv_record(tls_conn_t *conn, uint8_t *out_type,
                           uint8_t *buf, size_t buf_size, size_t *out_len) {
    /* Read 5-byte header */
    uint8_t hdr[5];
    if (sock_read_full(conn->sock_fd, hdr, 5, 15000) < 0)
        return -1;

    *out_type = hdr[0];
    uint16_t rec_len = get_be16(hdr + 3);

    if (rec_len > TLS_RECV_BUF) {
        DBG("tls: record too large: %u", rec_len);
        return -1;
    }

    /* Read record body */
    uint8_t *rec = malloc(rec_len);
    if (!rec) return -1;
    if (sock_read_full(conn->sock_fd, rec, rec_len, 15000) < 0) {
        free(rec);
        return -1;
    }

    if (!conn->server_encrypted) {
        /* Plaintext */
        if (rec_len > buf_size) { free(rec); return -1; }
        memcpy(buf, rec, rec_len);
        *out_len = rec_len;
        free(rec);
        return 0;
    }

    /* Decrypt: IV(16) + ciphertext */
    if (rec_len < AES_BLOCK_SIZE + AES_BLOCK_SIZE) {
        free(rec);
        return -1;
    }

    const uint8_t *iv = rec;
    const uint8_t *cipher = rec + AES_BLOCK_SIZE;
    size_t cipher_len = rec_len - AES_BLOCK_SIZE;

    if (cipher_len % AES_BLOCK_SIZE != 0) {
        free(rec);
        return -1;
    }

    uint8_t *plain = malloc(cipher_len);
    if (!plain) { free(rec); return -1; }
    aes128_cbc_decrypt(&conn->server_aes, iv, cipher, cipher_len, plain);
    free(rec);

    /* Remove padding */
    uint8_t pad_byte = plain[cipher_len - 1];
    size_t pad_len = (size_t)pad_byte + 1;
    if (pad_len > cipher_len || pad_len > AES_BLOCK_SIZE) {
        free(plain);
        return -1;
    }

    size_t content_len = cipher_len - pad_len - 32; /* minus padding and MAC */
    if (content_len > buf_size) {
        free(plain);
        return -1;
    }

    /* Verify MAC */
    uint8_t mac_input[13];
    put_be64(mac_input, conn->server_seq);
    mac_input[8] = *out_type;
    put_be16(mac_input + 9, TLS_VERSION_1_2);
    put_be16(mac_input + 11, (uint16_t)content_len);

    /* HMAC(server_write_mac_key, mac_input || content) */
    uint8_t k_ipad[SHA256_BLOCK_SIZE], k_opad[SHA256_BLOCK_SIZE];
    memset(k_ipad, 0x36, SHA256_BLOCK_SIZE);
    memset(k_opad, 0x5c, SHA256_BLOCK_SIZE);
    for (int i = 0; i < 32; i++) {
        k_ipad[i] ^= conn->server_write_mac_key[i];
        k_opad[i] ^= conn->server_write_mac_key[i];
    }

    sha256_ctx_t hmac_h;
    sha256_init(&hmac_h);
    sha256_update(&hmac_h, k_ipad, SHA256_BLOCK_SIZE);
    sha256_update(&hmac_h, mac_input, 13);
    sha256_update(&hmac_h, plain, content_len);
    uint8_t inner_hash[32];
    sha256_final(&hmac_h, inner_hash);

    uint8_t computed_mac[32];
    sha256_init(&hmac_h);
    sha256_update(&hmac_h, k_opad, SHA256_BLOCK_SIZE);
    sha256_update(&hmac_h, inner_hash, 32);
    sha256_final(&hmac_h, computed_mac);

    if (memcmp(computed_mac, plain + content_len, 32) != 0) {
        DBG("tls: MAC verification failed!");
        free(plain);
        return -1;
    }

    conn->server_seq++;

    memcpy(buf, plain, content_len);
    *out_len = content_len;
    free(plain);
    return 0;
}

/* ── TLS Handshake ───────────────────────────────────────────── */

/* Send a handshake message (type + 3-byte length + body) and
 * update the handshake transcript hash */
static int tls_send_handshake(tls_conn_t *conn, uint8_t hs_type,
                              const uint8_t *body, size_t body_len) {
    uint8_t hs_hdr[4];
    hs_hdr[0] = hs_type;
    put_be24(hs_hdr + 1, (uint32_t)body_len);

    /* Build complete handshake message for record + hash */
    uint8_t *msg = malloc(4 + body_len);
    if (!msg) return -1;
    memcpy(msg, hs_hdr, 4);
    if (body_len > 0) memcpy(msg + 4, body, body_len);

    /* Update transcript hash */
    sha256_update(&conn->hs_hash, msg, 4 + body_len);

    /* Send as TLS record */
    int ret = tls_send_record(conn, TLS_HANDSHAKE, msg, 4 + body_len);
    free(msg);
    return ret;
}

/* Build and send ClientHello */
static int tls_send_client_hello(tls_conn_t *conn, const char *hostname) {
    /* Generate client random */
    prng_random(conn->client_random, 32);

    /* Calculate hostname length for SNI */
    size_t host_len = strlen(hostname);

    /* Extensions we'll send:
     * 1. SNI
     * 2. signature_algorithms
     * 3. supported_groups (for ECDHE)
     * 4. ec_point_formats (for ECDHE)
     */

    /* SNI extension: type(2) + len(2) + sni_list_len(2) + type(1) + name_len(2) + name */
    size_t sni_ext_len = 2 + 2 + 2 + 1 + 2 + host_len;

    /* Signature algorithms extension */
    uint8_t sig_algs[] = {
        0x00, 0x0d,  /* signature_algorithms extension type */
        0x00, 0x0e,  /* extension length */
        0x00, 0x0c,  /* list length */
        0x04, 0x01,  /* rsa_pkcs1_sha256 */
        0x05, 0x01,  /* rsa_pkcs1_sha384 */
        0x06, 0x01,  /* rsa_pkcs1_sha512 */
        0x02, 0x01,  /* rsa_pkcs1_sha1 */
        0x04, 0x03,  /* ecdsa_secp256r1_sha256 */
        0x02, 0x03,  /* ecdsa_sha1 */
    };

    /* supported_groups extension: type(2) + len(2) + list_len(2) + group(2) */
    uint8_t sup_groups[] = {
        0x00, 0x0a,  /* supported_groups extension type */
        0x00, 0x04,  /* extension length */
        0x00, 0x02,  /* list length */
        0x00, 0x17,  /* secp256r1 (P-256) */
    };

    /* ec_point_formats extension: type(2) + len(2) + formats_len(1) + format(1) */
    uint8_t ec_formats[] = {
        0x00, 0x0b,  /* ec_point_formats extension type */
        0x00, 0x02,  /* extension length */
        0x01,        /* formats length */
        0x00,        /* uncompressed */
    };

    size_t extensions_len = sni_ext_len + sizeof(sig_algs) +
                            sizeof(sup_groups) + sizeof(ec_formats);

    /* Cipher suites: ECDHE first (preferred), then RSA fallback */
    size_t cipher_len = 4; /* 2 cipher suites × 2 bytes */
    size_t body_len = 2 + 32 + 1 + 2 + cipher_len + 1 + 1 + 2 + extensions_len;
    uint8_t *body = malloc(body_len);
    if (!body) return -1;
    uint8_t *p = body;

    /* Protocol version */
    put_be16(p, TLS_VERSION_1_2); p += 2;

    /* Client random */
    memcpy(p, conn->client_random, 32); p += 32;

    /* Session ID (empty) */
    *p++ = 0;

    /* Cipher suites: ECDHE preferred, RSA fallback */
    put_be16(p, (uint16_t)cipher_len); p += 2;
    put_be16(p, TLS_ECDHE_RSA_AES128_CBC_SHA256); p += 2;
    put_be16(p, TLS_RSA_AES128_CBC_SHA256); p += 2;

    /* Compression methods: null only */
    *p++ = 1;
    *p++ = 0;

    /* Extensions length */
    put_be16(p, (uint16_t)extensions_len); p += 2;

    /* SNI extension */
    put_be16(p, 0x0000); p += 2;
    put_be16(p, (uint16_t)(2 + 1 + 2 + host_len)); p += 2;
    put_be16(p, (uint16_t)(1 + 2 + host_len)); p += 2;
    *p++ = 0;
    put_be16(p, (uint16_t)host_len); p += 2;
    memcpy(p, hostname, host_len); p += host_len;

    /* Signature algorithms extension */
    memcpy(p, sig_algs, sizeof(sig_algs)); p += sizeof(sig_algs);

    /* Supported groups extension */
    memcpy(p, sup_groups, sizeof(sup_groups)); p += sizeof(sup_groups);

    /* EC point formats extension */
    memcpy(p, ec_formats, sizeof(ec_formats)); p += sizeof(ec_formats);

    int ret = tls_send_handshake(conn, TLS_HS_CLIENT_HELLO, body, body_len);
    free(body);
    return ret;
}

/* Process ServerHello, Certificate, [ServerKeyExchange], ServerHelloDone */
static int tls_recv_server_hello(tls_conn_t *conn) {
    uint8_t *buf = malloc(TLS_RECV_BUF);
    if (!buf) return -1;
    int got_hello = 0, got_cert = 0, got_done = 0, got_ske = 0;

    while (!got_done) {
        uint8_t type;
        size_t len;
        if (tls_recv_record(conn, &type, buf, TLS_RECV_BUF, &len) < 0) {
            DBG("tls: failed to receive server handshake record");
            free(buf);
            return -1;
        }

        if (type == TLS_ALERT) {
            DBG("tls: received alert: level=%d desc=%d", buf[0], buf[1]);
            free(buf);
            return -1;
        }

        if (type != TLS_HANDSHAKE) {
            DBG("tls: unexpected record type %d during handshake", type);
            free(buf);
            return -1;
        }

        /* Update transcript hash */
        sha256_update(&conn->hs_hash, buf, len);

        /* Parse handshake messages (may be multiple in one record) */
        size_t pos = 0;
        while (pos + 4 <= len) {
            uint8_t hs_type = buf[pos];
            uint32_t hs_len = get_be24(buf + pos + 1);
            if (pos + 4 + hs_len > len) break;
            const uint8_t *hs_body = buf + pos + 4;

            switch (hs_type) {
            case TLS_HS_SERVER_HELLO: {
                /* version(2) + random(32) + session_id_len(1) + session_id + cipher(2) + comp(1) */
                if (hs_len < 35) { free(buf); return -1; }
                memcpy(conn->server_random, hs_body + 2, 32);
                uint8_t sid_len = hs_body[34];
                size_t off = 35 + sid_len;
                if (off + 3 > hs_len) { free(buf); return -1; }
                uint16_t cipher = get_be16(hs_body + off);
                if (cipher != TLS_RSA_AES128_CBC_SHA256 &&
                    cipher != TLS_ECDHE_RSA_AES128_CBC_SHA256) {
                    DBG("tls: server chose unsupported cipher 0x%x", cipher);
                    free(buf);
                    return -1;
                }
                conn->cipher_suite = cipher;
                got_hello = 1;
                DBG("tls: ServerHello OK, cipher=0x%x", cipher);
                break;
            }
            case TLS_HS_CERTIFICATE: {
                /* certificates_length(3) + [ cert_length(3) + cert_data ]* */
                if (hs_len < 3) { free(buf); return -1; }
                uint32_t certs_len = get_be24(hs_body);
                if (3 + certs_len > hs_len) { free(buf); return -1; }

                /* Extract first certificate */
                const uint8_t *cp = hs_body + 3;
                if (certs_len < 3) { free(buf); return -1; }
                uint32_t cert_len = get_be24(cp);
                cp += 3;

                DBG("tls: Certificate length=%u", cert_len);

                /* Extract RSA public key */
                if (asn1_extract_rsa_pubkey(cp, cert_len, &conn->server_key) < 0) {
                    DBG("tls: failed to extract RSA pubkey from cert");
                    free(buf);
                    return -1;
                }
                DBG("tls: RSA key extracted, n_bytes=%u", (unsigned)conn->server_key.n_bytes);
                got_cert = 1;
                break;
            }
            case TLS_HS_SERVER_KEY_EXCHANGE: {
                /* ECDHE ServerKeyExchange:
                 * curve_type(1) + named_curve(2) + pubkey_len(1) + pubkey(65)
                 * + sig_hash_alg(2) + sig_len(2) + signature(...)
                 * We only care about the EC public key part. */
                if (hs_len < 4) { free(buf); return -1; }
                uint8_t curve_type = hs_body[0];
                uint16_t named_curve = get_be16(hs_body + 1);
                uint8_t pubkey_len = hs_body[3];

                DBG("tls: ServerKeyExchange curve_type=%d curve=0x%x pklen=%d",
                    curve_type, named_curve, pubkey_len);

                if (curve_type != 3) { /* 3 = named_curve */
                    DBG("tls: unsupported curve type %d", curve_type);
                    free(buf); return -1;
                }
                if (named_curve != 0x0017) { /* 0x0017 = secp256r1 */
                    DBG("tls: unsupported curve 0x%x", named_curve);
                    free(buf); return -1;
                }
                if (pubkey_len != 65 || hs_len < 4 + 65) {
                    DBG("tls: bad EC pubkey length %d", pubkey_len);
                    free(buf); return -1;
                }

                /* Parse uncompressed point: 0x04 || x(32) || y(32) */
                const uint8_t *pk = hs_body + 4;
                if (pk[0] != 0x04) {
                    DBG("tls: EC point not uncompressed");
                    free(buf); return -1;
                }
                ec_fe_from_bytes(&conn->ecdhe_server_pubkey.x, pk + 1, 32);
                ec_fe_from_bytes(&conn->ecdhe_server_pubkey.y, pk + 33, 32);
                conn->ecdhe_server_pubkey.infinity = 0;

                /* We skip signature verification (no cert chain validation in v1) */
                got_ske = 1;
                DBG("tls: ECDHE server pubkey parsed");
                break;
            }
            case TLS_HS_SERVER_HELLO_DONE:
                got_done = 1;
                DBG("tls: ServerHelloDone");
                break;
            default:
                DBG("tls: ignoring handshake type %d", hs_type);
                break;
            }

            pos += 4 + hs_len;
        }
    }

    free(buf);

    if (!got_hello || !got_cert) {
        DBG("tls: missing ServerHello or Certificate");
        return -1;
    }
    /* ECDHE requires ServerKeyExchange */
    if (conn->cipher_suite == TLS_ECDHE_RSA_AES128_CBC_SHA256 && !got_ske) {
        DBG("tls: ECDHE cipher but no ServerKeyExchange");
        return -1;
    }
    return 0;
}

/* Send ClientKeyExchange + ChangeCipherSpec + Finished */
static int tls_send_client_finish(tls_conn_t *conn) {
    uint8_t pms[48];

    if (conn->cipher_suite == TLS_ECDHE_RSA_AES128_CBC_SHA256) {
        /* ECDHE key exchange */
        DBG("tls: ECDHE key exchange");

        /* Generate our ephemeral keypair */
        ec_point_t our_pubkey;
        ec_generate_keypair(conn->ecdhe_privkey, &our_pubkey);

        /* Compute shared secret: ECDH(our_privkey, server_pubkey).x */
        ec_fe_t shared_x;
        ec_compute_shared(&shared_x, conn->ecdhe_privkey,
                          &conn->ecdhe_server_pubkey);

        /* Pre-master secret = shared_x (32 bytes, big-endian) */
        uint8_t shared_bytes[32];
        ec_fe_to_bytes(&shared_x, shared_bytes);

        /* For ECDHE, PMS is the raw x-coordinate (32 bytes) */
        memset(pms, 0, 48);
        memcpy(pms, shared_bytes, 32);

        /* ClientKeyExchange: pubkey_len(1) + uncompressed_point(65) */
        uint8_t cke[66];
        cke[0] = 65; /* length of uncompressed point */
        cke[1] = 0x04; /* uncompressed */
        ec_fe_to_bytes(&our_pubkey.x, cke + 2);
        ec_fe_to_bytes(&our_pubkey.y, cke + 34);

        if (tls_send_handshake(conn, TLS_HS_CLIENT_KEY_EXCHANGE, cke, 66) < 0)
            return -1;

        /* Derive master_secret from 32-byte PMS */
        uint8_t seed[64];
        memcpy(seed, conn->client_random, 32);
        memcpy(seed + 32, conn->server_random, 32);
        tls_prf(shared_bytes, 32, "master secret", seed, 64,
                conn->master_secret, 48);
    } else {
        /* RSA key exchange */
        DBG("tls: RSA key exchange");

        /* Generate 48-byte pre-master secret: version(2) + random(46) */
        pms[0] = 0x03; pms[1] = 0x03;
        prng_random(pms + 2, 46);

        /* RSA encrypt PMS with server's public key */
        size_t enc_len = conn->server_key.n_bytes;
        uint8_t *enc_pms = malloc(enc_len);
        if (!enc_pms) return -1;

        if (rsa_encrypt(&conn->server_key, pms, 48, enc_pms, enc_len) < 0) {
            DBG("tls: RSA encrypt failed");
            free(enc_pms);
            return -1;
        }

        /* ClientKeyExchange: length(2) + encrypted_pms */
        size_t cke_len = 2 + enc_len;
        uint8_t *cke = malloc(cke_len);
        if (!cke) { free(enc_pms); return -1; }
        put_be16(cke, (uint16_t)enc_len);
        memcpy(cke + 2, enc_pms, enc_len);
        free(enc_pms);

        if (tls_send_handshake(conn, TLS_HS_CLIENT_KEY_EXCHANGE, cke, cke_len) < 0) {
            free(cke);
            return -1;
        }
        free(cke);

        /* Derive master_secret */
        uint8_t seed[64];
        memcpy(seed, conn->client_random, 32);
        memcpy(seed + 32, conn->server_random, 32);
        tls_prf(pms, 48, "master secret", seed, 64,
                conn->master_secret, 48);
    }

    /* Derive key_block = PRF(master_secret, "key expansion", server_random + client_random)
     * For TLS_RSA_WITH_AES_128_CBC_SHA256:
     *   client_write_MAC_key (32) + server_write_MAC_key (32) +
     *   client_write_key (16) + server_write_key (16) = 96 bytes */
    uint8_t ks_seed[64];
    memcpy(ks_seed, conn->server_random, 32);
    memcpy(ks_seed + 32, conn->client_random, 32);
    uint8_t key_block[96];
    tls_prf(conn->master_secret, 48, "key expansion", ks_seed, 64,
            key_block, 96);

    memcpy(conn->client_write_mac_key, key_block, 32);
    memcpy(conn->server_write_mac_key, key_block + 32, 32);
    memcpy(conn->client_write_key, key_block + 64, 16);
    memcpy(conn->server_write_key, key_block + 80, 16);

    aes128_init(&conn->client_aes, conn->client_write_key);
    aes128_init(&conn->server_aes, conn->server_write_key);

    DBG("tls: keys derived");
    DBG("tls: master[0..3]=%x %x %x %x",
        conn->master_secret[0], conn->master_secret[1],
        conn->master_secret[2], conn->master_secret[3]);
    DBG("tls: client_key[0..3]=%x %x %x %x",
        conn->client_write_key[0], conn->client_write_key[1],
        conn->client_write_key[2], conn->client_write_key[3]);
    DBG("tls: client_mac[0..3]=%x %x %x %x",
        conn->client_write_mac_key[0], conn->client_write_mac_key[1],
        conn->client_write_mac_key[2], conn->client_write_mac_key[3]);

    /* Send ChangeCipherSpec */
    uint8_t ccs = 1;
    if (tls_send_record(conn, TLS_CHANGE_CIPHER_SPEC, &ccs, 1) < 0)
        return -1;

    conn->client_encrypted = 1;
    conn->client_seq = 0;

    /* Compute verify_data for Finished message:
     * verify_data = PRF(master_secret, "client finished", Hash(all_handshake_messages)) */
    sha256_ctx_t hash_copy = conn->hs_hash;
    uint8_t hs_digest[SHA256_DIGEST_SIZE];
    sha256_final(&hash_copy, hs_digest);

    uint8_t verify_data[12];
    tls_prf(conn->master_secret, 48, "client finished",
            hs_digest, SHA256_DIGEST_SIZE, verify_data, 12);

    DBG("tls: verify[0..5]=%x %x %x %x %x %x",
        verify_data[0], verify_data[1], verify_data[2],
        verify_data[3], verify_data[4], verify_data[5]);

    /* Send Finished (this goes encrypted now) */
    if (tls_send_handshake(conn, TLS_HS_FINISHED, verify_data, 12) < 0)
        return -1;

    DBG("tls: client Finished sent");
    return 0;
}

/* Receive server's ChangeCipherSpec + Finished */
static int tls_recv_server_finish(tls_conn_t *conn) {
    uint8_t buf[256];
    uint8_t type;
    size_t len;

    /* Expect ChangeCipherSpec */
    if (tls_recv_record(conn, &type, buf, sizeof(buf), &len) < 0)
        return -1;
    if (type == TLS_ALERT && len >= 2) {
        DBG("tls: server alert: level=%d desc=%d", buf[0], buf[1]);
        return -1;
    }
    if (type != TLS_CHANGE_CIPHER_SPEC) {
        DBG("tls: expected CCS, got type %d", type);
        return -1;
    }

    conn->server_encrypted = 1;
    conn->server_seq = 0;

    /* Expect Finished */
    if (tls_recv_record(conn, &type, buf, sizeof(buf), &len) < 0)
        return -1;
    if (type != TLS_HANDSHAKE || len < 4) {
        DBG("tls: expected Finished, got type %d len %u", type, (unsigned)len);
        return -1;
    }
    if (buf[0] != TLS_HS_FINISHED) {
        DBG("tls: expected Finished handshake, got %d", buf[0]);
        return -1;
    }

    DBG("tls: handshake complete!");
    return 0;
}

/* ── Public API ──────────────────────────────────────────────── */

int tls_connect(tls_conn_t *conn, int sock_fd, const char *hostname) {
    memset(conn, 0, sizeof(tls_conn_t));
    conn->sock_fd = sock_fd;
    sha256_init(&conn->hs_hash);

    DBG("tls: starting handshake with %s", hostname);

    /* ClientHello */
    if (tls_send_client_hello(conn, hostname) < 0) return -1;

    /* ServerHello + Certificate + ServerHelloDone */
    if (tls_recv_server_hello(conn) < 0) return -1;

    /* ClientKeyExchange + CCS + Finished */
    if (tls_send_client_finish(conn) < 0) return -1;

    /* Server CCS + Finished */
    if (tls_recv_server_finish(conn) < 0) return -1;

    conn->established = 1;
    return 0;
}

int tls_send(tls_conn_t *conn, const uint8_t *data, size_t len) {
    if (!conn->established) return -1;
    return tls_send_record(conn, TLS_APPLICATION_DATA, data, len);
}

int tls_recv(tls_conn_t *conn, uint8_t *buf, size_t len) {
    if (!conn->established) return -1;

    /* Serve from recv buffer if available */
    if (conn->recv_pos < conn->recv_len) {
        size_t avail = conn->recv_len - conn->recv_pos;
        size_t copy = avail < len ? avail : len;
        memcpy(buf, conn->recv_buf + conn->recv_pos, copy);
        conn->recv_pos += copy;
        return (int)copy;
    }

    /* Read next record */
    uint8_t type;
    size_t rec_len;
    if (tls_recv_record(conn, &type, conn->recv_buf, TLS_RECV_BUF, &rec_len) < 0)
        return -1;

    if (type == TLS_ALERT) {
        DBG("tls: alert during data: level=%d desc=%d",
            conn->recv_buf[0], conn->recv_buf[1]);
        return -1;
    }

    if (type != TLS_APPLICATION_DATA) {
        DBG("tls: unexpected record type %d during data", type);
        return -1;
    }

    conn->recv_len = rec_len;
    conn->recv_pos = 0;

    size_t copy = rec_len < len ? rec_len : len;
    memcpy(buf, conn->recv_buf, copy);
    conn->recv_pos = copy;
    return (int)copy;
}

void tls_close(tls_conn_t *conn) {
    if (conn->established) {
        /* Send close_notify alert */
        uint8_t alert[2] = {1, 0}; /* warning, close_notify */
        tls_send_record(conn, TLS_ALERT, alert, 2);
    }
    conn->established = 0;
}

/* ── HTTPS GET ───────────────────────────────────────────────── */

int https_get(const char *host, uint16_t port, const char *path,
              uint8_t **out_body, size_t *out_len) {
    *out_body = NULL;
    *out_len = 0;

    /* DNS resolve */
    uint8_t ip[4];
    printf("Resolving %s...\n", host);
    if (dns_resolve(host, ip) < 0) {
        printf("DNS resolution failed for %s\n", host);
        return -1;
    }
    printf("Resolved to %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    /* TCP connect */
    int sock = socket_create(SOCK_STREAM);
    if (sock < 0) {
        printf("Failed to create socket\n");
        return -1;
    }

    printf("Connecting to %d.%d.%d.%d:%d...\n", ip[0], ip[1], ip[2], ip[3], port);
    if (socket_connect(sock, ip, port) < 0) {
        printf("TCP connection failed\n");
        socket_close(sock);
        return -1;
    }
    printf("TCP connected\n");

    /* TLS handshake */
    tls_conn_t *tls = malloc(sizeof(tls_conn_t));
    if (!tls) {
        socket_close(sock);
        return -1;
    }

    printf("TLS handshake...\n");
    if (tls_connect(tls, sock, host) < 0) {
        printf("TLS handshake failed\n");
        free(tls);
        socket_close(sock);
        return -1;
    }
    printf("TLS established\n");

    /* Send HTTP/1.0 GET request */
    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: ImposOS/1.0\r\n"
        "\r\n", path, host);

    if (tls_send(tls, (const uint8_t *)req, req_len) < 0) {
        printf("Failed to send HTTP request\n");
        tls_close(tls);
        free(tls);
        socket_close(sock);
        return -1;
    }

    /* Read full response */
    size_t resp_cap = 4096;
    size_t resp_len = 0;
    uint8_t *resp = malloc(resp_cap);
    if (!resp) {
        tls_close(tls);
        free(tls);
        socket_close(sock);
        return -1;
    }

    while (1) {
        if (resp_len + 2048 > resp_cap) {
            resp_cap *= 2;
            uint8_t *nr = realloc(resp, resp_cap);
            if (!nr) break;
            resp = nr;
        }
        int n = tls_recv(tls, resp + resp_len, 2048);
        if (n <= 0) break;
        resp_len += n;
    }

    tls_close(tls);
    free(tls);
    socket_close(sock);

    if (resp_len == 0) {
        free(resp);
        printf("Empty response\n");
        return -1;
    }

    /* Parse HTTP response — find header/body boundary */
    const char *hdr_end = NULL;
    for (size_t i = 0; i + 3 < resp_len; i++) {
        if (resp[i] == '\r' && resp[i+1] == '\n' &&
            resp[i+2] == '\r' && resp[i+3] == '\n') {
            hdr_end = (const char *)(resp + i + 4);
            break;
        }
    }

    if (!hdr_end) {
        /* No header found, return everything */
        *out_body = resp;
        *out_len = resp_len;
        return (int)resp_len;
    }

    /* Parse status code */
    int status = 0;
    if (resp_len > 12 && memcmp(resp, "HTTP/1.", 7) == 0) {
        /* "HTTP/1.x YYY ..." */
        status = (resp[9] - '0') * 100 + (resp[10] - '0') * 10 + (resp[11] - '0');
    }

    size_t hdr_size = (size_t)(hdr_end - (const char *)resp);
    size_t body_len = resp_len - hdr_size;

    /* Handle redirects */
    if (status == 301 || status == 302) {
        /* Find Location header */
        const char *loc = NULL;
        for (size_t i = 0; i + 10 < hdr_size; i++) {
            if ((resp[i] == 'L' || resp[i] == 'l') &&
                memcmp(resp + i, "Location: ", 10) == 0) {
                /* Case-insensitive first char is handled above;
                 * actually do a proper check */
                loc = (const char *)(resp + i + 10);
                break;
            }
            /* try lowercase */
            if (resp[i] == 'l' && i + 10 < hdr_size &&
                memcmp(resp + i, "location: ", 10) == 0) {
                loc = (const char *)(resp + i + 10);
                break;
            }
        }

        if (loc) {
            /* Extract URL up to \r\n */
            char redir_url[512];
            int j = 0;
            while (*loc && *loc != '\r' && *loc != '\n' && j < 511)
                redir_url[j++] = *loc++;
            redir_url[j] = '\0';

            free(resp);
            printf("Redirect %d -> %s\n", status, redir_url);

            /* Parse redirect URL: https://host/path */
            if (memcmp(redir_url, "https://", 8) == 0) {
                char new_host[256];
                char new_path[256];
                const char *hp = redir_url + 8;
                int hi = 0;
                while (*hp && *hp != '/' && hi < 255)
                    new_host[hi++] = *hp++;
                new_host[hi] = '\0';
                if (*hp == '/') {
                    int pi = 0;
                    while (*hp && pi < 255)
                        new_path[pi++] = *hp++;
                    new_path[pi] = '\0';
                } else {
                    new_path[0] = '/';
                    new_path[1] = '\0';
                }
                return https_get(new_host, 443, new_path, out_body, out_len);
            }
            /* Non-HTTPS redirect — not supported */
            printf("Non-HTTPS redirect not supported\n");
            return -1;
        }
    }

    printf("HTTP %d, body %u bytes\n", status, (unsigned)body_len);

    if (status < 200 || status >= 400) {
        free(resp);
        return -1;
    }

    /* Move body to start of buffer */
    memmove(resp, resp + hdr_size, body_len);
    uint8_t *trimmed = realloc(resp, body_len > 0 ? body_len : 1);
    *out_body = trimmed ? trimmed : resp;
    *out_len = body_len;
    return (int)body_len;
}

/* ── Async HTTPS GET (runs in a preemptive thread) ─────────── */

static https_async_t *_async_req;   /* pointer for thread entry */

static void _https_thread_entry(void) {
    https_async_t *req = _async_req;
    req->result = https_get(req->host, req->port, req->path,
                            &req->body, &req->body_len);
    req->done = 1;
    task_exit();
}

int https_get_async(https_async_t *req) {
    req->done = 0;
    req->body = NULL;
    req->body_len = 0;
    req->result = 0;

    _async_req = req;
    int tid = task_create_thread("https", _https_thread_entry, 1);
    if (tid < 0)
        return -1;
    req->tid = tid;
    return 0;
}
