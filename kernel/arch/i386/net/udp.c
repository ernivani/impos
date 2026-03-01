#include <kernel/udp.h>
#include <kernel/ip.h>
#include <kernel/net.h>
#include <kernel/idt.h>
#include <kernel/endian.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    uint8_t data[UDP_MAX_PAYLOAD];
    size_t  len;
    uint8_t src_ip[4];
    uint16_t src_port;
} udp_packet_t;

typedef struct {
    uint16_t port;
    int active;
    udp_packet_t ring[UDP_RING_SIZE];
    int head, tail, count;
} udp_binding_t;

static udp_binding_t bindings[UDP_MAX_BINDINGS];

void udp_initialize(void) {
    memset(bindings, 0, sizeof(bindings));
}

int udp_bind(uint16_t port) {
    for (int i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].active && bindings[i].port == port)
            return -1; /* already bound */
    }
    for (int i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (!bindings[i].active) {
            bindings[i].port = port;
            bindings[i].active = 1;
            bindings[i].head = 0;
            bindings[i].tail = 0;
            bindings[i].count = 0;
            return 0;
        }
    }
    return -1; /* no free slots */
}

void udp_unbind(uint16_t port) {
    for (int i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].active && bindings[i].port == port) {
            bindings[i].active = 0;
            return;
        }
    }
}

static uint16_t udp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                              const uint8_t* udp_pkt, size_t udp_len) {
    uint32_t sum = 0;

    /* Build pseudo-header as raw bytes in network order, then sum as uint16_t
     * to stay consistent with how the packet data is summed */
    uint8_t pseudo[12];
    memcpy(pseudo, src_ip, 4);
    memcpy(pseudo + 4, dst_ip, 4);
    pseudo[8] = 0;
    pseudo[9] = 17; /* protocol UDP */
    pseudo[10] = (udp_len >> 8) & 0xFF;
    pseudo[11] = udp_len & 0xFF;

    const uint16_t* pw = (const uint16_t*)pseudo;
    for (int i = 0; i < 6; i++) sum += pw[i];

    /* UDP header + data */
    const uint16_t* words = (const uint16_t*)udp_pkt;
    size_t remaining = udp_len;
    while (remaining > 1) {
        sum += *words++;
        remaining -= 2;
    }
    if (remaining == 1)
        sum += *(const uint8_t*)words;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t result = ~sum;
    return result == 0 ? 0xFFFF : result;
}

int udp_send(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t src_port,
             const uint8_t* data, size_t len) {
    if (len > UDP_MAX_PAYLOAD) return -1;

    uint8_t packet[sizeof(udp_header_t) + UDP_MAX_PAYLOAD];
    udp_header_t* hdr = (udp_header_t*)packet;
    size_t total = sizeof(udp_header_t) + len;

    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons(total);
    hdr->checksum = 0;
    memcpy(packet + sizeof(udp_header_t), data, len);

    net_config_t* cfg = net_get_config();
    hdr->checksum = udp_checksum(cfg->ip, dst_ip, packet, total);

    return ip_send_packet(dst_ip, IP_PROTOCOL_UDP, packet, total);
}

int udp_recv(uint16_t port, uint8_t* buf, size_t* len,
             uint8_t src_ip[4], uint16_t* src_port, uint32_t timeout_ms) {
    udp_binding_t* b = NULL;
    for (int i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].active && bindings[i].port == port) {
            b = &bindings[i];
            break;
        }
    }
    if (!b) return -1;

    uint32_t start = pit_get_ticks();
    uint32_t timeout_ticks = timeout_ms * 120 / 1000;

    while (1) {
        /* Process incoming packets */
        net_process_packets();

        if (b->count > 0) {
            udp_packet_t* pkt = &b->ring[b->tail];
            size_t copy = pkt->len;
            if (copy > *len) copy = *len;
            memcpy(buf, pkt->data, copy);
            *len = copy;
            if (src_ip) memcpy(src_ip, pkt->src_ip, 4);
            if (src_port) *src_port = pkt->src_port;
            b->tail = (b->tail + 1) % UDP_RING_SIZE;
            b->count--;
            return 0;
        }

        if (pit_get_ticks() - start >= timeout_ticks)
            return -1; /* timeout */
    }
}

int udp_rx_available(uint16_t port) {
    for (int i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].active && bindings[i].port == port)
            return bindings[i].count;
    }
    return 0;
}

void udp_handle_packet(const uint8_t* data, size_t len, const uint8_t src_ip[4]) {
    if (len < sizeof(udp_header_t)) return;

    const udp_header_t* hdr = (const udp_header_t*)data;
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t sport = ntohs(hdr->src_port);
    size_t payload_len = ntohs(hdr->length) - sizeof(udp_header_t);

    if (payload_len > len - sizeof(udp_header_t))
        payload_len = len - sizeof(udp_header_t);

    for (int i = 0; i < UDP_MAX_BINDINGS; i++) {
        if (bindings[i].active && bindings[i].port == dst_port) {
            udp_binding_t* b = &bindings[i];
            if (b->count < UDP_RING_SIZE) {
                udp_packet_t* pkt = &b->ring[b->head];
                size_t copy = payload_len;
                if (copy > UDP_MAX_PAYLOAD) copy = UDP_MAX_PAYLOAD;
                memcpy(pkt->data, data + sizeof(udp_header_t), copy);
                pkt->len = copy;
                memcpy(pkt->src_ip, src_ip, 4);
                pkt->src_port = sport;
                b->head = (b->head + 1) % UDP_RING_SIZE;
                b->count++;
            }
            return;
        }
    }
}
