#include <kernel/net.h>
#include <kernel/rtl8139.h>
#include <kernel/pcnet.h>
#include <kernel/arp.h>
#include <kernel/ip.h>
#include <kernel/udp.h>
#include <kernel/tcp.h>
#include <kernel/socket.h>
#include <kernel/dns.h>
#include <kernel/dhcp.h>
#include <kernel/httpd.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>

static net_config_t net_config;
static int net_initialized = 0;
static int active_driver = 0;  /* 0=none, 1=rtl8139, 2=pcnet */
static uint32_t net_tx_packets = 0, net_tx_bytes = 0;
static uint32_t net_rx_packets = 0, net_rx_bytes = 0;

void net_initialize(void) {
    if (net_initialized) {
        return;
    }

    memset(&net_config, 0, sizeof(net_config_t));
    
    /* Default configuration */
    net_config.mac[0] = 0x52;
    net_config.mac[1] = 0x54;
    net_config.mac[2] = 0x00;
    net_config.mac[3] = 0x12;
    net_config.mac[4] = 0x34;
    net_config.mac[5] = 0x56;
    
    /* Default IP: 10.0.2.15 (QEMU default) */
    net_config.ip[0] = 10;
    net_config.ip[1] = 0;
    net_config.ip[2] = 2;
    net_config.ip[3] = 15;
    
    /* Netmask: 255.255.255.0 */
    net_config.netmask[0] = 255;
    net_config.netmask[1] = 255;
    net_config.netmask[2] = 255;
    net_config.netmask[3] = 0;
    
    /* Gateway: 10.0.2.2 (QEMU default) */
    net_config.gateway[0] = 10;
    net_config.gateway[1] = 0;
    net_config.gateway[2] = 2;
    net_config.gateway[3] = 2;
    
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
    
    /* Try to initialize NIC drivers: RTL8139 first, then PCnet */
    DBG("net: trying RTL8139...");
    if (rtl8139_initialize() == 0) {
        rtl8139_get_mac(net_config.mac);
        net_config.link_up = 1;
        active_driver = 1;
        DBG("net: RTL8139 OK, MAC=%x:%x:%x:%x:%x:%x",
            net_config.mac[0], net_config.mac[1], net_config.mac[2],
            net_config.mac[3], net_config.mac[4], net_config.mac[5]);
        DBG("Network: RTL8139 initialized");
    } else {
        DBG("net: RTL8139 not found, trying PCnet...");
        if (pcnet_initialize() == 0) {
            pcnet_get_mac(net_config.mac);
            net_config.link_up = 1;
            active_driver = 2;
            DBG("net: PCnet OK, MAC=%x:%x:%x:%x:%x:%x",
                net_config.mac[0], net_config.mac[1], net_config.mac[2],
                net_config.mac[3], net_config.mac[4], net_config.mac[5]);
            DBG("Network: PCnet-FAST III initialized");
        } else {
            DBG("net: no NIC found!");
            DBG("No network card detected");
        }
    }

    net_initialized = 1;
    DBG("net: init done, driver=%d link_up=%d", active_driver, net_config.link_up);
}

net_config_t* net_get_config(void) {
    return &net_config;
}

void net_set_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    net_config.ip[0] = a;
    net_config.ip[1] = b;
    net_config.ip[2] = c;
    net_config.ip[3] = d;
}

int net_send_packet(const uint8_t* data, size_t len) {
    DBG("net: send_packet len=%u driver=%d", (unsigned)len, active_driver);
    int ret = -1;
    if (active_driver == 1) ret = rtl8139_send_packet(data, len);
    else if (active_driver == 2) ret = pcnet_send_packet(data, len);
    if (ret == 0) { net_tx_packets++; net_tx_bytes += (uint32_t)len; }
    DBG("net: send_packet ret=%d", ret);
    return ret;
}

int net_receive_packet(uint8_t* buffer, size_t* len) {
    if (active_driver == 1) return rtl8139_receive_packet(buffer, len);
    if (active_driver == 2) return pcnet_receive_packet(buffer, len);
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
    if (active_driver == 0) {
        return;
    }

    uint8_t buffer[1500];
    size_t len = sizeof(buffer);

    while (net_receive_packet(buffer, &len) == 0) {
        net_rx_packets++;
        net_rx_bytes += (uint32_t)len;

        /* Check Ethernet frame type */
        if (len < 14) {
            len = sizeof(buffer);
            continue;
        }

        uint16_t ethertype = (buffer[12] << 8) | buffer[13];
        DBG("net: rx pkt len=%u ethertype=0x%x", (unsigned)len, ethertype);

        if (ethertype == 0x0806) {  /* ARP */
            arp_handle_packet(buffer + 14, len - 14);
        } else if (ethertype == 0x0800) {  /* IPv4 */
            ip_handle_packet(buffer + 14, len - 14);
        }

        len = sizeof(buffer);
    }
}

void net_get_stats(uint32_t *tx_pkts, uint32_t *tx_bytes, uint32_t *rx_pkts, uint32_t *rx_bytes) {
    if (tx_pkts) *tx_pkts = net_tx_packets;
    if (tx_bytes) *tx_bytes = net_tx_bytes;
    if (rx_pkts) *rx_pkts = net_rx_packets;
    if (rx_bytes) *rx_bytes = net_rx_bytes;
}
