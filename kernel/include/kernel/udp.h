#ifndef _KERNEL_UDP_H
#define _KERNEL_UDP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

#define UDP_MAX_BINDINGS 8
#define UDP_RING_SIZE    4
#define UDP_MAX_PAYLOAD  1400

void udp_initialize(void);
int udp_bind(uint16_t port);
void udp_unbind(uint16_t port);
int udp_send(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t src_port,
             const uint8_t* data, size_t len);
int udp_recv(uint16_t port, uint8_t* buf, size_t* len,
             uint8_t src_ip[4], uint16_t* src_port, uint32_t timeout_ms);
void udp_handle_packet(const uint8_t* data, size_t len, const uint8_t src_ip[4]);
int  udp_rx_available(uint16_t port);

#endif
