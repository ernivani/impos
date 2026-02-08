#include <kernel/arp.h>
#include <kernel/net.h>
#include <kernel/rtl8139.h>
#include <stdio.h>
#include <string.h>

#define ARP_CACHE_SIZE 16
#define ARP_TIMEOUT 300  /* 300 seconds = 5 minutes */

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t current_time = 0;

/* Ethernet frame type for ARP */
#define ETHERTYPE_ARP 0x0806

/* Convert 16-bit value to network byte order (big-endian) */
static uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

void arp_initialize(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
}

int arp_resolve(const uint8_t ip[4], uint8_t mac[6]) {
    /* Check cache first */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid &&
            memcmp(arp_cache[i].ip, ip, 4) == 0 &&
            (current_time - arp_cache[i].timestamp) < ARP_TIMEOUT) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    
    /* Not in cache, send ARP request */
    return arp_send_request(ip);
}

int arp_send_request(const uint8_t target_ip[4]) {
    if (!rtl8139_is_initialized()) {
        return -1;
    }
    
    net_config_t* config = net_get_config();
    uint8_t packet[60];  /* Minimum Ethernet frame size */
    memset(packet, 0, sizeof(packet));
    
    /* Ethernet header */
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(packet, broadcast_mac, 6);              /* Destination MAC */
    memcpy(packet + 6, config->mac, 6);            /* Source MAC */
    *(uint16_t*)(packet + 12) = htons(ETHERTYPE_ARP);  /* EtherType */
    
    /* ARP packet */
    arp_packet_t* arp = (arp_packet_t*)(packet + 14);
    arp->hw_type = htons(1);        /* Ethernet */
    arp->proto_type = htons(0x0800); /* IPv4 */
    arp->hw_addr_len = 6;
    arp->proto_addr_len = 4;
    arp->opcode = htons(ARP_REQUEST);
    memcpy(arp->sender_mac, config->mac, 6);
    memcpy(arp->sender_ip, config->ip, 4);
    memset(arp->target_mac, 0, 6);   /* Unknown */
    memcpy(arp->target_ip, target_ip, 4);
    
    return net_send_packet(packet, sizeof(packet));
}

void arp_handle_packet(const uint8_t* data, size_t len) {
    if (len < sizeof(arp_packet_t)) {
        return;
    }
    
    arp_packet_t* arp = (arp_packet_t*)data;
    net_config_t* config = net_get_config();
    
    /* Convert from network byte order */
    uint16_t opcode = htons(arp->opcode);
    
    if (opcode == ARP_REPLY) {
        printf("ARP reply from ");
        net_print_ip(arp->sender_ip);
        printf(" (MAC: ");
        net_print_mac(arp->sender_mac);
        printf(")\n");
    }
    
    /* Update ARP cache */
    int cache_index = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            cache_index = i;
            break;
        }
        if (memcmp(arp_cache[i].ip, arp->sender_ip, 4) == 0) {
            cache_index = i;
            break;
        }
    }
    
    if (cache_index == -1) {
        cache_index = 0;  /* Overwrite oldest entry */
    }
    
    memcpy(arp_cache[cache_index].ip, arp->sender_ip, 4);
    memcpy(arp_cache[cache_index].mac, arp->sender_mac, 6);
    arp_cache[cache_index].timestamp = current_time;
    arp_cache[cache_index].valid = 1;
    
    /* If this is a request for us, send reply */
    if (opcode == ARP_REQUEST && 
        memcmp(arp->target_ip, config->ip, 4) == 0) {
        
        uint8_t reply[60];
        memset(reply, 0, sizeof(reply));
        
        /* Ethernet header */
        memcpy(reply, arp->sender_mac, 6);        /* Destination MAC */
        memcpy(reply + 6, config->mac, 6);        /* Source MAC */
        *(uint16_t*)(reply + 12) = htons(ETHERTYPE_ARP);
        
        /* ARP reply */
        arp_packet_t* arp_reply = (arp_packet_t*)(reply + 14);
        arp_reply->hw_type = htons(1);
        arp_reply->proto_type = htons(0x0800);
        arp_reply->hw_addr_len = 6;
        arp_reply->proto_addr_len = 4;
        arp_reply->opcode = htons(ARP_REPLY);
        memcpy(arp_reply->sender_mac, config->mac, 6);
        memcpy(arp_reply->sender_ip, config->ip, 4);
        memcpy(arp_reply->target_mac, arp->sender_mac, 6);
        memcpy(arp_reply->target_ip, arp->sender_ip, 4);
        
        net_send_packet(reply, sizeof(reply));
    }
}
