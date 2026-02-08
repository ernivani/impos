#ifndef _KERNEL_NET_H
#define _KERNEL_NET_H

#include <stdint.h>
#include <stddef.h>

/* Ethernet frame structure */
typedef struct {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint8_t payload[1500];
} __attribute__((packed)) eth_frame_t;

/* Network interface configuration */
typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    int link_up;
} net_config_t;

/* Initialize networking */
void net_initialize(void);

/* Get network configuration */
net_config_t* net_get_config(void);

/* Set IP address */
void net_set_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/* Send/receive packets */
int net_send_packet(const uint8_t* data, size_t len);
int net_receive_packet(uint8_t* buffer, size_t* len);

/* Network utilities */
void net_print_mac(const uint8_t mac[6]);
void net_print_ip(const uint8_t ip[4]);

/* Packet processing */
void net_process_packets(void);

#endif
