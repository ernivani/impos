/*
 * Network driver dispatch — aarch64
 *
 * Uses VirtIO-net instead of RTL8139/PCnet.
 */
#include <kernel/net.h>
#include <kernel/arp.h>
#include <kernel/ip.h>
#include <kernel/udp.h>
#include <kernel/tcp.h>
#include <kernel/socket.h>
#include <kernel/dns.h>
#include <kernel/dhcp.h>
#include <kernel/httpd.h>
#include <kernel/io.h>
#include <kernel/virtio_mmio.h>
#include <stdio.h>
#include <string.h>

/* VirtIO net driver API (from virtio_net.c) */
extern int virtio_net_initialize(void);
extern int virtio_net_send_packet(const uint8_t *data, uint32_t len);
extern int virtio_net_receive_packet(uint8_t *buf, uint32_t buf_size, uint32_t *out_len);
extern void virtio_net_get_mac(uint8_t *mac);

static net_config_t net_config;
static int net_initialized = 0;
static int active_driver = 0;  /* 0=none, 3=virtio-net */
static uint32_t net_tx_packets = 0, net_tx_bytes = 0;
static uint32_t net_rx_packets = 0, net_rx_bytes = 0;

void net_initialize(void) {
    if (net_initialized)
        return;

    memset(&net_config, 0, sizeof(net_config_t));

    /* Default MAC */
    net_config.mac[0] = 0x52;
    net_config.mac[1] = 0x54;
    net_config.mac[2] = 0x00;
    net_config.mac[3] = 0x12;
    net_config.mac[4] = 0x34;
    net_config.mac[5] = 0x56;

    /* Default IP: 10.0.2.15 (QEMU default) */
    net_config.ip[0] = 10;  net_config.ip[1] = 0;
    net_config.ip[2] = 2;   net_config.ip[3] = 15;

    /* Netmask: 255.255.255.0 */
    net_config.netmask[0] = 255; net_config.netmask[1] = 255;
    net_config.netmask[2] = 255; net_config.netmask[3] = 0;

    /* Gateway: 10.0.2.2 */
    net_config.gateway[0] = 10; net_config.gateway[1] = 0;
    net_config.gateway[2] = 2;  net_config.gateway[3] = 2;

    net_config.link_up = 0;

    /* Initialize protocol layers */
    arp_initialize();
    ip_initialize();
    icmp_initialize();
    udp_initialize();
    tcp_initialize();
    socket_initialize();
    dns_initialize();
    dhcp_initialize();
    httpd_initialize();

    /* Try VirtIO-net */
    DBG("net: trying VirtIO-net...");
    if (virtio_net_initialize() == 0) {
        virtio_net_get_mac(net_config.mac);
        net_config.link_up = 1;
        active_driver = 3;
        DBG("net: VirtIO-net OK, MAC=%x:%x:%x:%x:%x:%x",
            net_config.mac[0], net_config.mac[1], net_config.mac[2],
            net_config.mac[3], net_config.mac[4], net_config.mac[5]);
    } else {
        DBG("net: no NIC found!");
    }

    net_initialized = 1;
    DBG("net: init done, driver=%d link_up=%d", active_driver, net_config.link_up);
}

net_config_t* net_get_config(void) {
    return &net_config;
}

void net_set_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    net_config.ip[0] = a; net_config.ip[1] = b;
    net_config.ip[2] = c; net_config.ip[3] = d;
}

int net_send_packet(const uint8_t* data, size_t len) {
    int ret = -1;
    if (active_driver == 3)
        ret = virtio_net_send_packet(data, (uint32_t)len);
    if (ret == 0) { net_tx_packets++; net_tx_bytes += (uint32_t)len; }
    return ret;
}

int net_receive_packet(uint8_t* buffer, size_t* len) {
    if (active_driver == 3) {
        uint32_t out_len = 0;
        int ret = virtio_net_receive_packet(buffer, (uint32_t)*len, &out_len);
        if (ret == 0) {
            *len = out_len;
            net_rx_packets++;
            net_rx_bytes += out_len;
        }
        return ret;
    }
    return -1;
}

void net_print_mac(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) {
        if (i > 0) putchar(':');
        int high = mac[i] >> 4;
        int low = mac[i] & 0xF;
        putchar(high < 10 ? '0' + high : 'a' + high - 10);
        putchar(low < 10 ? '0' + low : 'a' + low - 10);
    }
}

void net_print_ip(const uint8_t ip[4]) {
    printf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

void net_process_packets(void) {
    if (!net_config.link_up) return;

    static uint8_t pkt_buf[1600];
    size_t len = sizeof(pkt_buf);

    while (net_receive_packet(pkt_buf, &len) == 0 && len > 0) {
        if (len >= 14) {
            uint16_t ethertype = ((uint16_t)pkt_buf[12] << 8) | pkt_buf[13];
            if (ethertype == 0x0806)
                arp_handle_packet(pkt_buf, len);
            else if (ethertype == 0x0800)
                ip_handle_packet(pkt_buf, len);
        }
        len = sizeof(pkt_buf);
    }
}

int net_is_link_up(void) {
    return net_config.link_up;
}

void net_get_stats(uint32_t *tx_p, uint32_t *tx_b, uint32_t *rx_p, uint32_t *rx_b) {
    if (tx_p) *tx_p = net_tx_packets;
    if (tx_b) *tx_b = net_tx_bytes;
    if (rx_p) *rx_p = net_rx_packets;
    if (rx_b) *rx_b = net_rx_bytes;
}
