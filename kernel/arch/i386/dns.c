#include <kernel/dns.h>
#include <kernel/udp.h>
#include <kernel/endian.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>

/* DNS header */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

#define DNS_PORT    53
#define DNS_FLAG_RD 0x0100  /* Recursion Desired */
#define DNS_FLAG_QR 0x8000  /* Query/Response */
#define DNS_TYPE_A  1
#define DNS_CLASS_IN 1

/* QEMU SLIRP DNS server */
static uint8_t dns_server[4] = {10, 0, 2, 3};

void dns_initialize(void) {
    /* nothing */
}

/* Encode hostname into DNS wire format (e.g. "www.google.com" -> 3www6google3com0) */
static int dns_encode_name(const char* name, uint8_t* buf, size_t bufsize) {
    size_t pos = 0;
    const char* p = name;

    while (*p) {
        const char* dot = strchr(p, '.');
        size_t label_len = dot ? (size_t)(dot - p) : strlen(p);
        if (label_len == 0 || label_len > 63) return -1;
        if (pos + label_len + 1 >= bufsize) return -1;

        buf[pos++] = (uint8_t)label_len;
        memcpy(buf + pos, p, label_len);
        pos += label_len;
        p += label_len;
        if (*p == '.') p++;
    }
    if (pos >= bufsize) return -1;
    buf[pos++] = 0; /* root label */
    return pos;
}

int dns_resolve(const char* hostname, uint8_t ip_out[4]) {
    uint8_t query[512];
    memset(query, 0, sizeof(query));

    /* Build DNS header */
    dns_header_t* hdr = (dns_header_t*)query;
    hdr->id = htons(0x1234);
    hdr->flags = htons(DNS_FLAG_RD);
    hdr->qdcount = htons(1);

    /* Encode question */
    size_t offset = sizeof(dns_header_t);
    int name_len = dns_encode_name(hostname, query + offset, sizeof(query) - offset);
    if (name_len < 0) return -1;
    offset += name_len;

    /* QTYPE = A (1) */
    *(uint16_t*)(query + offset) = htons(DNS_TYPE_A);
    offset += 2;
    /* QCLASS = IN (1) */
    *(uint16_t*)(query + offset) = htons(DNS_CLASS_IN);
    offset += 2;

    /* Bind ephemeral port for response */
    uint16_t local_port = 10053;
    udp_bind(local_port);

    /* Send query */
    udp_send(dns_server, DNS_PORT, local_port, query, offset);

    /* Wait for response */
    uint8_t resp[512];
    size_t resp_len = sizeof(resp);
    uint8_t src_ip[4];
    uint16_t src_port;

    int ret = udp_recv(local_port, resp, &resp_len, src_ip, &src_port, 3000);
    udp_unbind(local_port);

    if (ret != 0) {
        return -1; /* timeout */
    }

    /* Parse response */
    if (resp_len < sizeof(dns_header_t)) return -1;
    dns_header_t* rhdr = (dns_header_t*)resp;

    if (ntohs(rhdr->id) != 0x1234) return -1;
    if (!(ntohs(rhdr->flags) & DNS_FLAG_QR)) return -1;
    uint16_t ancount = ntohs(rhdr->ancount);
    if (ancount == 0) return -1;

    /* Skip question section */
    size_t pos = sizeof(dns_header_t);
    /* Skip QNAME */
    while (pos < resp_len && resp[pos] != 0) {
        if (resp[pos] & 0xC0) { pos += 2; break; }
        pos += resp[pos] + 1;
    }
    if (pos < resp_len && resp[pos] == 0) pos++;
    pos += 4; /* QTYPE + QCLASS */

    /* Parse answers */
    for (uint16_t i = 0; i < ancount && pos < resp_len; i++) {
        /* Skip NAME (may be compressed) */
        if (pos >= resp_len) break;
        if (resp[pos] & 0xC0) {
            pos += 2;
        } else {
            while (pos < resp_len && resp[pos] != 0)
                pos += resp[pos] + 1;
            if (pos < resp_len) pos++;
        }

        if (pos + 10 > resp_len) break;
        uint16_t rtype = ntohs(*(uint16_t*)(resp + pos));
        pos += 2;
        /* uint16_t rclass = ntohs(*(uint16_t*)(resp + pos)); */
        pos += 2;
        /* uint32_t ttl = ntohl(*(uint32_t*)(resp + pos)); */
        pos += 4;
        uint16_t rdlength = ntohs(*(uint16_t*)(resp + pos));
        pos += 2;

        if (rtype == DNS_TYPE_A && rdlength == 4) {
            memcpy(ip_out, resp + pos, 4);
            return 0;
        }
        pos += rdlength;
    }

    return -1;
}
