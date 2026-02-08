#ifndef _KERNEL_ARP_H
#define _KERNEL_ARP_H

#include <stdint.h>
#include <stddef.h>

/* ARP packet structure */
typedef struct {
    uint16_t hw_type;          /* Hardware type (Ethernet = 1) */
    uint16_t proto_type;       /* Protocol type (IPv4 = 0x0800) */
    uint8_t hw_addr_len;       /* Hardware address length (6 for MAC) */
    uint8_t proto_addr_len;    /* Protocol address length (4 for IPv4) */
    uint16_t opcode;           /* Operation (1=request, 2=reply) */
    uint8_t sender_mac[6];     /* Sender MAC address */
    uint8_t sender_ip[4];      /* Sender IP address */
    uint8_t target_mac[6];     /* Target MAC address */
    uint8_t target_ip[4];      /* Target IP address */
} __attribute__((packed)) arp_packet_t;

/* ARP opcodes */
#define ARP_REQUEST 1
#define ARP_REPLY   2

/* ARP cache entry */
typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    uint32_t timestamp;
    int valid;
} arp_cache_entry_t;

/* ARP Functions */
void arp_initialize(void);
int arp_resolve(const uint8_t ip[4], uint8_t mac[6]);
void arp_handle_packet(const uint8_t* data, size_t len);
int arp_send_request(const uint8_t target_ip[4]);

#endif
