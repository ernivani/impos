#include <kernel/ip.h>
#include <kernel/arp.h>
#include <kernel/net.h>
#include <kernel/endian.h>
#include <kernel/udp.h>
#include <kernel/tcp.h>
#include <kernel/firewall.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>

#define ETHERTYPE_IP 0x0800

static uint16_t ip_id_counter = 0;

uint16_t ip_checksum(const void* data, size_t len) {
    const uint16_t* words = (const uint16_t*)data;
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *words++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(const uint8_t*)words;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

void ip_initialize(void) {
    ip_id_counter = 1;
}

int ip_send_packet(const uint8_t dst_ip[4], uint8_t protocol, const uint8_t* payload, size_t payload_len) {
    net_config_t* config = net_get_config();
    DBG("ip: send to %d.%d.%d.%d proto=%d len=%u link_up=%d",
        dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3],
        protocol, (unsigned)payload_len, config->link_up);
    if (!config->link_up) {
        DBG("ip: link down, aborting send");
        return -1;
    }
    
    /* Resolve destination MAC via ARP */
    uint8_t dst_mac[6];
    uint8_t bcast_ip[4] = {255, 255, 255, 255};
    if (memcmp(dst_ip, bcast_ip, 4) == 0) {
        /* Broadcast IP always uses broadcast MAC */
        memset(dst_mac, 0xFF, 6);
    } else {
        /* Determine ARP target: use gateway for non-local destinations */
        const uint8_t *arp_target = dst_ip;
        int local = 1;
        for (int i = 0; i < 4; i++) {
            if ((dst_ip[i] & config->netmask[i]) != (config->ip[i] & config->netmask[i])) {
                local = 0;
                break;
            }
        }
        if (!local) {
            arp_target = config->gateway;
            DBG("ip: non-local dest, ARP resolving gateway %d.%d.%d.%d",
                arp_target[0], arp_target[1], arp_target[2], arp_target[3]);
        }
        if (arp_resolve(arp_target, dst_mac) != 0) {
            DBG("ip: ARP failed, using broadcast MAC");
            memset(dst_mac, 0xFF, 6);
        }
    }
    
    /* Build packet: Ethernet + IP + Payload */
    size_t total_len = 14 + sizeof(ip_header_t) + payload_len;
    uint8_t packet[1500];
    
    if (total_len > sizeof(packet)) {
        return -1;
    }
    
    /* Ethernet header */
    memcpy(packet, dst_mac, 6);
    memcpy(packet + 6, config->mac, 6);
    *(uint16_t*)(packet + 12) = htons(ETHERTYPE_IP);
    
    /* IP header */
    ip_header_t* ip_hdr = (ip_header_t*)(packet + 14);
    memset(ip_hdr, 0, sizeof(ip_header_t));
    ip_hdr->version_ihl = 0x45;  /* Version 4, IHL 5 (20 bytes) */
    ip_hdr->tos = 0;
    ip_hdr->total_length = htons(sizeof(ip_header_t) + payload_len);
    ip_hdr->identification = htons(ip_id_counter++);
    ip_hdr->flags_fragment = 0;
    ip_hdr->ttl = 64;
    ip_hdr->protocol = protocol;
    memcpy(ip_hdr->src_ip, config->ip, 4);
    memcpy(ip_hdr->dst_ip, dst_ip, 4);
    ip_hdr->checksum = 0;
    ip_hdr->checksum = ip_checksum(ip_hdr, sizeof(ip_header_t));
    
    /* Payload */
    memcpy(packet + 14 + sizeof(ip_header_t), payload, payload_len);
    
    return net_send_packet(packet, total_len);
}

void ip_handle_packet(const uint8_t* data, size_t len) {
    if (len < sizeof(ip_header_t)) {
        DBG("ip: recv too short len=%u", (unsigned)len);
        return;
    }

    const ip_header_t* ip_hdr = (const ip_header_t*)data;
    net_config_t* config = net_get_config();

    DBG("ip: recv from %d.%d.%d.%d -> %d.%d.%d.%d proto=%d len=%u",
        ip_hdr->src_ip[0], ip_hdr->src_ip[1], ip_hdr->src_ip[2], ip_hdr->src_ip[3],
        ip_hdr->dst_ip[0], ip_hdr->dst_ip[1], ip_hdr->dst_ip[2], ip_hdr->dst_ip[3],
        ip_hdr->protocol, (unsigned)len);

    /* Check if packet is for us (or broadcast for DHCP) */
    uint8_t bcast[4] = {255, 255, 255, 255};
    uint8_t zero[4]  = {0, 0, 0, 0};
    if (memcmp(ip_hdr->dst_ip, config->ip, 4) != 0 &&
        memcmp(ip_hdr->dst_ip, bcast, 4) != 0 &&
        memcmp(config->ip, zero, 4) != 0) {
        DBG("ip: not for us, dropping");
        return;
    }

    /* Verify checksum: sum of entire header (including checksum) should be 0 */
    if (ip_checksum(ip_hdr, sizeof(ip_header_t)) != 0) {
        DBG("ip: bad checksum, dropping");
        return;  /* Bad checksum */
    }
    
    /* Extract payload */
    uint8_t ihl = (ip_hdr->version_ihl & 0x0F) * 4;
    const uint8_t* payload = data + ihl;
    size_t payload_len = ntohs(ip_hdr->total_length) - ihl;

    /* Firewall check: extract dst port for TCP/UDP */
    uint16_t dst_port = 0;
    if ((ip_hdr->protocol == IP_PROTOCOL_TCP || ip_hdr->protocol == IP_PROTOCOL_UDP)
        && payload_len >= 4) {
        dst_port = ntohs(*(const uint16_t*)(payload + 2));
    }
    if (firewall_check(ip_hdr->src_ip, ip_hdr->dst_ip,
                        ip_hdr->protocol, dst_port) == FW_ACTION_DENY) {
        return;  /* Packet dropped by firewall */
    }

    /* Handle by protocol */
    if (ip_hdr->protocol == IP_PROTOCOL_ICMP) {
        icmp_handle_packet(payload, payload_len, ip_hdr->src_ip);
    } else if (ip_hdr->protocol == IP_PROTOCOL_UDP) {
        udp_handle_packet(payload, payload_len, ip_hdr->src_ip);
    } else if (ip_hdr->protocol == IP_PROTOCOL_TCP) {
        tcp_handle_packet(payload, payload_len, ip_hdr->src_ip);
    }
}

void icmp_initialize(void) {
    /* Nothing to initialize */
}

int icmp_send_echo_request(const uint8_t dst_ip[4], uint16_t id, uint16_t seq) {
    DBG("icmp: echo request to %d.%d.%d.%d id=%d seq=%d",
        dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], id, seq);
    uint8_t payload[64];
    icmp_header_t* icmp = (icmp_header_t*)payload;
    
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(id);
    icmp->sequence = htons(seq);
    
    /* Add some data */
    for (int i = sizeof(icmp_header_t); i < 64; i++) {
        payload[i] = 0x20 + (i % 64);
    }
    
    /* Calculate checksum */
    icmp->checksum = ip_checksum(payload, 64);
    
    return ip_send_packet(dst_ip, IP_PROTOCOL_ICMP, payload, 64);
}

void icmp_handle_packet(const uint8_t* data, size_t len, const uint8_t src_ip[4]) {
    if (len < sizeof(icmp_header_t)) {
        return;
    }
    
    const icmp_header_t* icmp = (const icmp_header_t*)data;
    
    if (icmp->type == ICMP_ECHO_REPLY) {
        printf("Reply from ");
        net_print_ip(src_ip);
        printf(": seq=%d\n", ntohs(icmp->sequence));
    } else if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Send echo reply */
        uint8_t reply[1500];
        memcpy(reply, data, len);
        icmp_header_t* reply_icmp = (icmp_header_t*)reply;
        reply_icmp->type = ICMP_ECHO_REPLY;
        reply_icmp->checksum = 0;
        reply_icmp->checksum = ip_checksum(reply, len);
        
        ip_send_packet(src_ip, IP_PROTOCOL_ICMP, reply, len);
    }
}
