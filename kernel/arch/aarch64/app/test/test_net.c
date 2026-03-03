#include <kernel/test.h>
#include <kernel/ip.h>
#include <kernel/net.h>
#include <kernel/endian.h>
#include <kernel/firewall.h>
#include <kernel/crypto.h>
#include <kernel/tls.h>
#include <kernel/socket.h>
#include <kernel/tcp.h>
#include <kernel/udp.h>
#include <kernel/dns.h>
#include <kernel/http.h>
#include <kernel/linux_syscall.h>
#include <kernel/pipe.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_network(void) {
    printf("== Network Tests ==\n");

    /* Byte order: htons / ntohs */
    TEST_ASSERT(htons(0x1234) == 0x3412, "htons swap");
    TEST_ASSERT(ntohs(0x3412) == 0x1234, "ntohs swap");
    TEST_ASSERT(ntohs(htons(0xABCD)) == 0xABCD, "htons/ntohs roundtrip");

    /* htonl / ntohl */
    TEST_ASSERT(htonl(0x12345678) == 0x78563412, "htonl swap");
    TEST_ASSERT(ntohl(htonl(0xDEADBEEF)) == 0xDEADBEEF, "htonl/ntohl roundtrip");

    /* IP checksum on a known header:
     * Version/IHL=0x45, TOS=0, Len=0x003C, ID=0x1C46, Flags=0x4000,
     * TTL=0x40, Proto=0x06, Checksum=0, Src=0xAC100A63, Dst=0xAC100A0C
     * Expected checksum: the function returns host-order value which,
     * stored directly to uint16_t, yields correct wire bytes. */
    uint8_t ip_hdr[20] = {
        0x45, 0x00, 0x00, 0x3C,  /* ver/ihl, tos, total_len */
        0x1C, 0x46, 0x40, 0x00,  /* id, flags/frag */
        0x40, 0x06, 0x00, 0x00,  /* ttl, proto(TCP), checksum=0 */
        0xAC, 0x10, 0x0A, 0x63,  /* src: 172.16.10.99 */
        0xAC, 0x10, 0x0A, 0x0C   /* dst: 172.16.10.12 */
    };

    uint16_t csum = ip_checksum(ip_hdr, 20);
    TEST_ASSERT(csum != 0, "ip_checksum non-zero for zeroed field");

    /* Store checksum and verify sum-to-zero property */
    *(uint16_t*)(ip_hdr + 10) = csum;
    uint16_t verify = ip_checksum(ip_hdr, 20);
    TEST_ASSERT(verify == 0, "ip_checksum sum-to-zero");

    /* Net config */
    net_config_t* cfg = net_get_config();
    TEST_ASSERT(cfg != NULL, "net_get_config non-null");
    TEST_ASSERT(cfg->link_up == 1, "link is up");

    /* MAC should not be all-zero (driver set it) */
    int mac_nonzero = 0;
    for (int i = 0; i < 6; i++) {
        if (cfg->mac[i] != 0) mac_nonzero = 1;
    }
    TEST_ASSERT(mac_nonzero, "MAC address set");
}

/* ---- Firewall Tests ---- */

static void test_firewall(void) {
    printf("== Firewall Tests ==\n");

    /* Save current state and reinit */
    firewall_flush();
    firewall_set_default(FW_ACTION_ALLOW);

    uint8_t src[4] = {10, 0, 2, 15};
    uint8_t dst[4] = {10, 0, 2, 1};

    /* Default allow */
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_ALLOW,
                "fw default allow");

    /* Add deny ICMP rule */
    fw_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.protocol = FW_PROTO_ICMP;
    rule.action = FW_ACTION_DENY;
    rule.enabled = 1;
    TEST_ASSERT(firewall_add_rule(&rule) == 0, "fw add rule");
    TEST_ASSERT(firewall_rule_count() == 1, "fw rule count 1");

    /* ICMP should be denied, TCP still allowed */
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_ICMP, 0) == FW_ACTION_DENY,
                "fw deny icmp");
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_ALLOW,
                "fw allow tcp with icmp rule");

    /* Add deny TCP port 80 */
    fw_rule_t rule2;
    memset(&rule2, 0, sizeof(rule2));
    rule2.protocol = FW_PROTO_TCP;
    rule2.action = FW_ACTION_DENY;
    rule2.dst_port_min = 80;
    rule2.dst_port_max = 80;
    rule2.enabled = 1;
    firewall_add_rule(&rule2);

    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 80) == FW_ACTION_DENY,
                "fw deny tcp:80");
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_TCP, 443) == FW_ACTION_ALLOW,
                "fw allow tcp:443");

    /* Test default deny */
    firewall_set_default(FW_ACTION_DENY);
    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_UDP, 53) == FW_ACTION_DENY,
                "fw default deny udp");

    /* Test source IP matching */
    fw_rule_t rule3;
    memset(&rule3, 0, sizeof(rule3));
    rule3.protocol = FW_PROTO_ALL;
    rule3.action = FW_ACTION_ALLOW;
    rule3.src_ip[0] = 10; rule3.src_ip[1] = 0; rule3.src_ip[2] = 2; rule3.src_ip[3] = 15;
    memset(rule3.src_mask, 255, 4);
    rule3.enabled = 1;
    firewall_add_rule(&rule3);

    TEST_ASSERT(firewall_check(src, dst, FW_PROTO_UDP, 53) == FW_ACTION_ALLOW,
                "fw allow by src ip");
    uint8_t other_src[4] = {192, 168, 1, 1};
    TEST_ASSERT(firewall_check(other_src, dst, FW_PROTO_UDP, 53) == FW_ACTION_DENY,
                "fw deny other src");

    /* Delete rule and flush */
    TEST_ASSERT(firewall_del_rule(0) == 0, "fw del rule 0");
    TEST_ASSERT(firewall_rule_count() == 2, "fw count after del");

    firewall_flush();
    TEST_ASSERT(firewall_rule_count() == 0, "fw count after flush");
    firewall_set_default(FW_ACTION_ALLOW);
}

void test_crypto(void) {
    printf("== Crypto Tests ==\n");

    /* SHA-256: NIST test vector - SHA256("abc") */
    {
        uint8_t digest[32];
        sha256((const uint8_t *)"abc", 3, digest);
        /* Expected: ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad */
        uint8_t expected[] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
            0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
            0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
        };
        TEST_ASSERT(memcmp(digest, expected, 32) == 0, "SHA256(abc)");
    }

    /* SHA-256: empty string */
    {
        uint8_t digest[32];
        sha256((const uint8_t *)"", 0, digest);
        uint8_t expected[] = {
            0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
            0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
            0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
            0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
        };
        TEST_ASSERT(memcmp(digest, expected, 32) == 0, "SHA256(empty)");
    }

    /* HMAC-SHA-256: RFC 4231 Test Case 2 */
    {
        uint8_t mac[32];
        hmac_sha256((const uint8_t *)"Jefe", 4,
                    (const uint8_t *)"what do ya want for nothing?", 28,
                    mac);
        uint8_t expected[] = {
            0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,
            0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
            0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
            0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
        };
        TEST_ASSERT(memcmp(mac, expected, 32) == 0, "HMAC-SHA256 RFC4231 TC2");
    }

    /* AES-128: FIPS 197 Appendix B test vector */
    {
        uint8_t key[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
            0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
        };
        uint8_t plaintext[16] = {
            0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
            0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34
        };
        uint8_t expected_ct[16] = {
            0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
            0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32
        };

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);

        uint8_t ct[16];
        aes128_encrypt_block(&ctx, plaintext, ct);
        TEST_ASSERT(memcmp(ct, expected_ct, 16) == 0, "AES-128 encrypt");

        uint8_t pt[16];
        aes128_decrypt_block(&ctx, ct, pt);
        TEST_ASSERT(memcmp(pt, plaintext, 16) == 0, "AES-128 decrypt");
    }

    /* AES-128-CBC encrypt/decrypt roundtrip */
    {
        uint8_t key[16] = {0};
        uint8_t iv[16] = {0};
        uint8_t data[32];
        memset(data, 0x42, 32);

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);

        uint8_t cipher[32], plain[32];
        aes128_cbc_encrypt(&ctx, iv, data, 32, cipher);
        aes128_cbc_decrypt(&ctx, iv, cipher, 32, plain);
        TEST_ASSERT(memcmp(plain, data, 32) == 0, "AES-CBC roundtrip");
    }

    /* AES-128-GCM: NIST test case 3 (64-byte plaintext, no AAD)
     * From: NIST SP 800-38D, test case 3 */
    {
        uint8_t key[] = {0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,
                         0x6d,0x6a,0x8f,0x94,0x67,0x30,0x83,0x08};
        uint8_t nonce[] = {0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,
                           0xde,0xca,0xf8,0x88};
        uint8_t pt[] = {0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,
                        0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
                        0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,
                        0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
                        0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,
                        0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
                        0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,
                        0xba,0x63,0x7b,0x39,0x1a,0xaf,0xd2,0x55};
        uint8_t expected_tag[] = {0x4d,0x5c,0x2a,0xf3,0x27,0xcd,0x64,0xa6,
                                  0x2c,0xf3,0x5a,0xbd,0x2b,0xa6,0xfa,0xb4};

        aes128_ctx_t ctx;
        aes128_init(&ctx, key);
        uint8_t ct[64], tag[16], dec[64];

        int rc_enc = aes128_gcm_encrypt(&ctx, nonce, 12, NULL, 0,
                                         pt, 64, ct, tag);
        TEST_ASSERT(rc_enc == 0, "AES-GCM encrypt succeeds");
        TEST_ASSERT(memcmp(tag, expected_tag, 16) == 0, "AES-GCM tag matches NIST");

        int rc_dec = aes128_gcm_decrypt(&ctx, nonce, 12, NULL, 0,
                                         ct, 64, dec, tag);
        TEST_ASSERT(rc_dec == 0, "AES-GCM decrypt succeeds");
        TEST_ASSERT(memcmp(dec, pt, 64) == 0, "AES-GCM roundtrip matches");

        /* Tamper test: flip a ciphertext byte, should fail auth */
        ct[0] ^= 0x01;
        int rc_tamper = aes128_gcm_decrypt(&ctx, nonce, 12, NULL, 0,
                                            ct, 64, dec, tag);
        TEST_ASSERT(rc_tamper == -2, "AES-GCM rejects tampered ciphertext");
    }

    /* Bignum: 3^10 mod 7 = 4 */
    {
        bignum_t base, exp, mod, result;
        bn_zero(&base); base.d[0] = 3; base.top = 1;
        bn_zero(&exp);  exp.d[0] = 10; exp.top = 1;
        bn_zero(&mod);  mod.d[0] = 7;  mod.top = 1;
        bn_modexp(&result, &base, &exp, &mod);
        TEST_ASSERT(result.d[0] == 4, "bignum 3^10 mod 7 = 4");
    }

    /* Bignum: 2^16 mod 100 = 36 */
    {
        bignum_t base, exp, mod, result;
        bn_zero(&base); base.d[0] = 2;  base.top = 1;
        bn_zero(&exp);  exp.d[0] = 16;  exp.top = 1;
        bn_zero(&mod);  mod.d[0] = 100; mod.top = 1;
        bn_modexp(&result, &base, &exp, &mod);
        TEST_ASSERT(result.d[0] == 36, "bignum 2^16 mod 100 = 36");
    }

    /* Bignum mulmod: carry overflow test (2048-bit) */
    {
        bignum_t a, two, m, result;
        bn_zero(&a); bn_zero(&two); bn_zero(&m);
        /* m = 2^2047 + 1 (has MSB set, true 2048-bit number) */
        m.d[63] = 0x80000000;
        m.d[0] = 1;
        m.top = 64;
        /* a = 2^2047 = m - 1 */
        a.d[63] = 0x80000000;
        a.top = 64;
        /* Compute a*2 mod m = 2^2048 mod (2^2047+1) = 2^2047 - 1 */
        two.d[0] = 2; two.top = 1;
        bn_mulmod(&result, &a, &two, &m);
        bignum_t expected;
        bn_zero(&expected);
        for (int i = 0; i < 63; i++) expected.d[i] = 0xFFFFFFFF;
        expected.d[63] = 0x7FFFFFFF;
        expected.top = 64;
        TEST_ASSERT(bn_cmp(&result, &expected) == 0, "mulmod 2048-bit carry");
    }

    /* Bignum modexp: (m-1)^2 mod m = 1 (2048-bit) */
    {
        bignum_t base, exp, m, result;
        bn_zero(&base); bn_zero(&exp); bn_zero(&m);
        /* m = 2^2047 + 3 */
        m.d[63] = 0x80000000;
        m.d[0] = 3;
        m.top = 64;
        /* base = m - 1 = 2^2047 + 2 */
        base.d[63] = 0x80000000;
        base.d[0] = 2;
        base.top = 64;
        /* exp = 2 */
        exp.d[0] = 2; exp.top = 1;
        bn_modexp(&result, &base, &exp, &m);
        /* (m-1)^2 mod m = (-1)^2 mod m = 1 */
        TEST_ASSERT(result.d[0] == 1 && result.top == 1, "modexp (m-1)^2 mod m = 1");
    }

    /* P-256: 2*G via scalar_mul must match point_double(G) */
    {
        ec_point_t g, dbl, mul;
        ec_get_generator(&g);
        ec_point_double(&dbl, &g);

        uint8_t k[32];
        memset(k, 0, 32);
        k[31] = 2; /* k = 2, big-endian */
        ec_scalar_mul(&mul, k, 32, &g);

        TEST_ASSERT(memcmp(&dbl.x, &mul.x, sizeof(ec_fe_t)) == 0, "P256 2*G x matches double");
        TEST_ASSERT(memcmp(&dbl.y, &mul.y, sizeof(ec_fe_t)) == 0, "P256 2*G y matches double");
    }

    /* P-256 NIST known-answer: k=1 => k*G == G */
    {
        ec_point_t g, res;
        ec_get_generator(&g);
        uint8_t k[32];
        memset(k, 0, 32);
        k[31] = 1; /* k = 1 */
        ec_scalar_mul(&res, k, 32, &g);
        TEST_ASSERT(memcmp(&res.x, &g.x, sizeof(ec_fe_t)) == 0, "P256 1*G == G (x)");
        TEST_ASSERT(memcmp(&res.y, &g.y, sizeof(ec_fe_t)) == 0, "P256 1*G == G (y)");
    }

    /* ECDH roundtrip: privA*pubB == privB*pubA */
    {
        uint8_t privA[32], privB[32];
        ec_point_t pubA, pubB;
        ec_generate_keypair(privA, &pubA);
        ec_generate_keypair(privB, &pubB);

        ec_fe_t sharedAB, sharedBA;
        ec_compute_shared(&sharedAB, privA, &pubB);
        ec_compute_shared(&sharedBA, privB, &pubA);

        uint8_t sAB[32], sBA[32];
        ec_fe_to_bytes(&sharedAB, sAB);
        ec_fe_to_bytes(&sharedBA, sBA);
        TEST_ASSERT(memcmp(sAB, sBA, 32) == 0, "ECDH shared secret agreement");
    }

    /* PRNG: should produce non-zero, non-identical output */
    {
        uint8_t buf1[16], buf2[16];
        prng_init();
        prng_random(buf1, 16);
        prng_random(buf2, 16);
        int all_zero = 1;
        for (int i = 0; i < 16; i++)
            if (buf1[i] != 0) all_zero = 0;
        TEST_ASSERT(!all_zero, "PRNG non-zero output");
        TEST_ASSERT(memcmp(buf1, buf2, 16) != 0, "PRNG different outputs");
    }

    printf("  Crypto tests done.\n");
}

void test_tls(void) {
    printf("== TLS Test ==\n");

    net_config_t *cfg = net_get_config();
    if (!cfg || !cfg->link_up || (cfg->ip[0] == 0 && cfg->ip[1] == 0)) {
        printf("  SKIP: network not configured (run dhcp first)\n");
        return;
    }

    printf("  Attempting HTTPS GET to example.com...\n");

    /* Run in background thread so UI stays responsive */
    static https_async_t req;
    strcpy(req.host, "example.com");
    req.port = 443;
    strcpy(req.path, "/");

    if (https_get_async(&req) < 0) {
        printf("  Failed to start HTTPS thread\n");
        return;
    }

    printf("  TLS running in background (thread %d)...\n", req.tid);
    while (!req.done) {
        keyboard_run_idle();   /* keep WM/UI alive */
        task_yield();
    }

    if (req.result > 0 && req.body) {
        TEST_ASSERT(req.body_len > 0, "tls: got response body");
        /* Check for HTML content */
        int has_html = 0;
        for (size_t i = 0; i + 5 < req.body_len; i++) {
            if (memcmp(req.body + i, "<html", 5) == 0 ||
                memcmp(req.body + i, "<HTML", 5) == 0) {
                has_html = 1;
                break;
            }
        }
        TEST_ASSERT(has_html, "tls: response contains HTML");
        printf("  Received %u bytes of HTML\n", (unsigned)req.body_len);
        free(req.body);
    } else {
        printf("  HTTPS GET failed (ret=%d) - server may not support our cipher\n", req.result);
    }
}

/* ---- HTTP Tests (requires network) ---- */

static void test_http(void) {
    printf("== HTTP Tests ==\n");

    /* URL parser unit tests (no network needed) */
    {
        char host[128], path[256];
        uint16_t port;
        int is_https;

        TEST_ASSERT(http_parse_url("http://example.com",
            host, sizeof(host), &port, path, sizeof(path), &is_https) == 0,
            "http: parse http URL");
        TEST_ASSERT(strcmp(host, "example.com") == 0, "http: parsed host");
        TEST_ASSERT(port == 80, "http: default port 80");
        TEST_ASSERT(strcmp(path, "/") == 0, "http: default path /");
        TEST_ASSERT(is_https == 0, "http: not https");

        TEST_ASSERT(http_parse_url("https://secure.example.com:8443/api/v1",
            host, sizeof(host), &port, path, sizeof(path), &is_https) == 0,
            "http: parse https URL with port+path");
        TEST_ASSERT(strcmp(host, "secure.example.com") == 0, "http: parsed https host");
        TEST_ASSERT(port == 8443, "http: custom port 8443");
        TEST_ASSERT(strcmp(path, "/api/v1") == 0, "http: parsed path");
        TEST_ASSERT(is_https == 1, "http: is https");

        TEST_ASSERT(http_parse_url("https://example.com",
            host, sizeof(host), &port, path, sizeof(path), &is_https) == 0,
            "http: parse https default port");
        TEST_ASSERT(port == 443, "http: default https port 443");

        /* Edge cases */
        TEST_ASSERT(http_parse_url(NULL, host, sizeof(host),
            &port, path, sizeof(path), &is_https) < 0,
            "http: null URL returns error");
        TEST_ASSERT(http_parse_url("",
            host, sizeof(host), &port, path, sizeof(path), &is_https) < 0,
            "http: empty URL returns error");
    }

    /* Network tests (skip if link not up) */
    net_config_t *cfg = net_get_config();
    if (!cfg || !cfg->link_up) {
        printf("  SKIP: network not available\n");
        return;
    }

    /* DNS resolve test */
    {
        uint8_t ip[4];
        int dns_rc = dns_resolve("example.com", ip);
        TEST_ASSERT(dns_rc == 0, "http: DNS resolve example.com");
        if (dns_rc < 0) {
            printf("  SKIP: DNS failed, skipping HTTP/HTTPS GET tests\n");
            return;
        }
        printf("  Resolved example.com to %d.%d.%d.%d\n",
               ip[0], ip[1], ip[2], ip[3]);
    }

    /* HTTP GET test */
    {
        http_response_t resp;
        int rc = http_get("http://example.com", &resp);
        TEST_ASSERT(rc == 0, "http: GET http://example.com succeeds");
        if (rc == 0) {
            TEST_ASSERT(resp.status_code == 200, "http: status 200");
            TEST_ASSERT(resp.body_len > 0, "http: body not empty");
            /* Check for HTML content */
            int has_html = resp.body &&
                (strstr(resp.body, "<html") || strstr(resp.body, "<HTML"));
            TEST_ASSERT(has_html, "http: response contains HTML");
            printf("  HTTP GET: %d bytes, status %d\n",
                   resp.body_len, resp.status_code);
            http_response_free(&resp);
        } else {
            printf("  HTTP GET failed: error %d\n", rc);
        }
    }

    /* HTTPS GET test */
    {
        http_set_verbose(1);
        http_response_t resp;
        int rc = http_get("https://example.com", &resp);
        http_set_verbose(0);
        TEST_ASSERT(rc == 0, "http: GET https://example.com succeeds");
        if (rc == 0) {
            TEST_ASSERT(resp.status_code == 200, "http: HTTPS status 200");
            TEST_ASSERT(resp.body_len > 0, "http: HTTPS body not empty");
            printf("  HTTPS GET: %d bytes, status %d\n",
                   resp.body_len, resp.status_code);
            http_response_free(&resp);
        } else {
            printf("  HTTPS GET failed: error %d (check serial for TLS diag)\n", rc);
        }
    }

    /* HTTPS GET google.com (requires GCM — Google rejects CBC ciphers) */
    {
        http_response_t resp;
        int rc = http_get("https://google.com", &resp);
        TEST_ASSERT(rc == 0, "http: GET https://google.com succeeds");
        if (rc == 0) {
            /* Google typically redirects (301) to www.google.com, or returns 200.
             * Our redirect-following should give us a final 200. */
            TEST_ASSERT(resp.status_code == 200 || resp.status_code == 301,
                        "http: google.com status 200 or 301");
            TEST_ASSERT(resp.body_len > 0, "http: google.com body not empty");
            printf("  HTTPS google.com: %d bytes, status %d\n",
                   resp.body_len, resp.status_code);
            http_response_free(&resp);
        } else {
            printf("  HTTPS google.com failed: error %d\n", rc);
        }
    }

    /* HTTPS GET impin.fr — uses ECDSA cert, ECDHE-ECDSA-AES128-GCM-SHA256 */
    {
        http_response_t resp;
        int rc = http_get("https://impin.fr", &resp);
        TEST_ASSERT(rc == 0, "http: GET https://impin.fr succeeds");
        if (rc == 0) {
            TEST_ASSERT(resp.body_len > 0, "http: impin.fr body");
            printf("  HTTPS impin.fr: %d bytes, status %d\n",
                   resp.body_len, resp.status_code);
            http_response_free(&resp);
        } else {
            printf("  HTTPS impin.fr failed: error %d\n", rc);
        }
    }
}

static void test_phase6_networking(void) {
    printf("== Phase 6 Networking Tests ==\n");

    /* ── FD_SOCKET constant ────────────────────────────────────────── */
    TEST_ASSERT(FD_SOCKET == 8, "FD_SOCKET == 8");

    /* ── Linux address family constants ────────────────────────────── */
    TEST_ASSERT(AF_INET == 2, "AF_INET == 2");
    TEST_ASSERT(AF_UNIX == 1, "AF_UNIX == 1");

    /* ── Socket type constants ─────────────────────────────────────── */
    TEST_ASSERT(SOCK_STREAM == 1, "SOCK_STREAM == 1");
    TEST_ASSERT(SOCK_DGRAM == 2, "SOCK_DGRAM == 2");

    /* ── sockaddr_in size (must be 16 bytes for Linux compat) ──────── */
    TEST_ASSERT(sizeof(struct linux_sockaddr_in) == 16,
                "sizeof(linux_sockaddr_in) == 16");

    /* ── socketcall sub-function constants ─────────────────────────── */
    TEST_ASSERT(SYS_SOCKET == 1, "SYS_SOCKET == 1");
    TEST_ASSERT(SYS_BIND == 2, "SYS_BIND == 2");
    TEST_ASSERT(SYS_CONNECT == 3, "SYS_CONNECT == 3");
    TEST_ASSERT(SYS_LISTEN == 4, "SYS_LISTEN == 4");
    TEST_ASSERT(SYS_ACCEPT == 5, "SYS_ACCEPT == 5");
    TEST_ASSERT(SYS_GETSOCKNAME == 6, "SYS_GETSOCKNAME == 6");
    TEST_ASSERT(SYS_GETPEERNAME == 7, "SYS_GETPEERNAME == 7");
    TEST_ASSERT(SYS_SEND == 9, "SYS_SEND == 9");
    TEST_ASSERT(SYS_RECV == 10, "SYS_RECV == 10");
    TEST_ASSERT(SYS_SENDTO == 11, "SYS_SENDTO == 11");
    TEST_ASSERT(SYS_RECVFROM == 12, "SYS_RECVFROM == 12");
    TEST_ASSERT(SYS_SHUTDOWN == 13, "SYS_SHUTDOWN == 13");
    TEST_ASSERT(SYS_SETSOCKOPT == 14, "SYS_SETSOCKOPT == 14");
    TEST_ASSERT(SYS_GETSOCKOPT == 15, "SYS_GETSOCKOPT == 15");

    /* ── Linux syscall number ──────────────────────────────────────── */
    TEST_ASSERT(LINUX_SYS_socketcall == 102, "LINUX_SYS_socketcall == 102");

    /* ── New errno values ──────────────────────────────────────────── */
    TEST_ASSERT(LINUX_ENOTSOCK == 88, "LINUX_ENOTSOCK == 88");
    TEST_ASSERT(LINUX_EPROTONOSUPPORT == 93, "LINUX_EPROTONOSUPPORT == 93");
    TEST_ASSERT(LINUX_EAFNOSUPPORT == 97, "LINUX_EAFNOSUPPORT == 97");
    TEST_ASSERT(LINUX_EADDRINUSE == 98, "LINUX_EADDRINUSE == 98");
    TEST_ASSERT(LINUX_ENETUNREACH == 101, "LINUX_ENETUNREACH == 101");
    TEST_ASSERT(LINUX_ECONNRESET == 104, "LINUX_ECONNRESET == 104");
    TEST_ASSERT(LINUX_ENOTCONN == 107, "LINUX_ENOTCONN == 107");
    TEST_ASSERT(LINUX_ETIMEDOUT == 110, "LINUX_ETIMEDOUT == 110");
    TEST_ASSERT(LINUX_ECONNREFUSED == 111, "LINUX_ECONNREFUSED == 111");
    TEST_ASSERT(LINUX_EINPROGRESS == 115, "LINUX_EINPROGRESS == 115");

    /* ── TCP backlog defines ───────────────────────────────────────── */
    TEST_ASSERT(TCP_BACKLOG_MAX == 4, "TCP_BACKLOG_MAX == 4");

    /* ── Socket create/close round-trip ────────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(s >= 0, "socket_create(STREAM) succeeds");
        int s2 = socket_create(SOCK_DGRAM);
        TEST_ASSERT(s2 >= 0, "socket_create(DGRAM) succeeds");
        TEST_ASSERT(s != s2, "two sockets have different fds");
        socket_close(s);
        socket_close(s2);
    }

    /* ── socket_set_nonblock / socket_get_nonblock ─────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(s >= 0, "nonblock test: socket created");
        TEST_ASSERT(socket_get_nonblock(s) == 0, "default nonblock is 0");
        socket_set_nonblock(s, 1);
        TEST_ASSERT(socket_get_nonblock(s) == 1, "nonblock set to 1");
        socket_set_nonblock(s, 0);
        TEST_ASSERT(socket_get_nonblock(s) == 0, "nonblock set back to 0");
        socket_close(s);
    }

    /* ── socket_poll_query on fresh socket ─────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(s >= 0, "poll_query test: socket created");
        int ev = socket_poll_query(s);
        TEST_ASSERT(ev == 0, "poll_query on unconnected socket is 0");
        socket_close(s);
    }

    /* ── socket_get_type ───────────────────────────────────────────── */
    {
        int s1 = socket_create(SOCK_STREAM);
        int s2 = socket_create(SOCK_DGRAM);
        TEST_ASSERT(socket_get_type(s1) == SOCK_STREAM, "get_type STREAM");
        TEST_ASSERT(socket_get_type(s2) == SOCK_DGRAM, "get_type DGRAM");
        socket_close(s1);
        socket_close(s2);
    }

    /* ── tcp_has_backlog on fresh TCB ──────────────────────────────── */
    TEST_ASSERT(tcp_has_backlog(0) == 0, "tcp_has_backlog(0) on init is 0");

    /* ── tcp_rx_available on fresh TCB ─────────────────────────────── */
    TEST_ASSERT(tcp_rx_available(0) == 0, "tcp_rx_available(0) on init is 0");

    /* ── tcp_recv_nb on unconnected TCB ────────────────────────────── */
    {
        uint8_t buf[16];
        int rc = tcp_recv_nb(0, buf, sizeof(buf));
        TEST_ASSERT(rc < 0, "tcp_recv_nb on unconnected returns error");
    }

    /* ── udp_rx_available ──────────────────────────────────────────── */
    TEST_ASSERT(udp_rx_available(9999) == 0, "udp_rx_available(unused) is 0");

    /* ── socket_recv_nb on fresh socket ────────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        uint8_t buf[16];
        int rc = socket_recv_nb(s, buf, sizeof(buf));
        TEST_ASSERT(rc < 0, "recv_nb on unconnected socket returns error");
        socket_close(s);
    }

    /* ── dns_cache_flush callable ──────────────────────────────────── */
    dns_cache_flush();
    TEST_ASSERT(1, "dns_cache_flush() callable without crash");

    /* ── socket_is_listening ───────────────────────────────────────── */
    {
        int s = socket_create(SOCK_STREAM);
        TEST_ASSERT(socket_is_listening(s) == 0, "fresh socket not listening");
        socket_close(s);
    }

    /* ── Invalid socket operations ─────────────────────────────────── */
    TEST_ASSERT(socket_create(99) == -1, "socket_create bad type fails");
    TEST_ASSERT(socket_set_nonblock(-1, 1) == -1, "set_nonblock bad fd fails");
    TEST_ASSERT(socket_get_nonblock(-1) == 0, "get_nonblock bad fd returns 0");
    TEST_ASSERT(socket_poll_query(-1) == 0, "poll_query bad fd returns 0");
}

void test_net_all(void) {
    test_network();
    test_firewall();
    test_crypto();
    test_http();
    test_phase6_networking();
}
