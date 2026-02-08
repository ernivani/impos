#ifndef _KERNEL_IP_H
#define _KERNEL_IP_H

#include <stdint.h>
#include <stddef.h>

/* IP Header */
typedef struct {
    uint8_t version_ihl;       /* Version (4 bits) + IHL (4 bits) */
    uint8_t tos;               /* Type of Service */
    uint16_t total_length;     /* Total length */
    uint16_t identification;   /* Identification */
    uint16_t flags_fragment;   /* Flags (3 bits) + Fragment offset (13 bits) */
    uint8_t ttl;               /* Time to Live */
    uint8_t protocol;          /* Protocol (1=ICMP, 6=TCP, 17=UDP) */
    uint16_t checksum;         /* Header checksum */
    uint8_t src_ip[4];         /* Source IP address */
    uint8_t dst_ip[4];         /* Destination IP address */
} __attribute__((packed)) ip_header_t;

/* ICMP Header */
typedef struct {
    uint8_t type;              /* ICMP type */
    uint8_t code;              /* ICMP code */
    uint16_t checksum;         /* Checksum */
    uint16_t id;               /* Identifier */
    uint16_t sequence;         /* Sequence number */
} __attribute__((packed)) icmp_header_t;

/* Protocol numbers */
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP  6
#define IP_PROTOCOL_UDP  17

/* ICMP types */
#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

/* IP Functions */
void ip_initialize(void);
int ip_send_packet(const uint8_t dst_ip[4], uint8_t protocol, const uint8_t* payload, size_t payload_len);
void ip_handle_packet(const uint8_t* data, size_t len);

/* ICMP Functions */
void icmp_initialize(void);
int icmp_send_echo_request(const uint8_t dst_ip[4], uint16_t id, uint16_t seq);
void icmp_handle_packet(const uint8_t* data, size_t len, const uint8_t src_ip[4]);

/* Utility functions */
uint16_t ip_checksum(const void* data, size_t len);

#endif
