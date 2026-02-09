#include <kernel/dhcp.h>
#include <kernel/udp.h>
#include <kernel/net.h>
#include <kernel/idt.h>
#include <kernel/endian.h>
#include <kernel/ip.h>
#include <string.h>
#include <stdio.h>

/* DHCP message structure (simplified) */
typedef struct {
    uint8_t  op;         /* 1=request, 2=reply */
    uint8_t  htype;      /* 1=Ethernet */
    uint8_t  hlen;       /* 6 for MAC */
    uint8_t  hops;
    uint32_t xid;        /* transaction ID */
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];  /* client IP */
    uint8_t  yiaddr[4];  /* your (client) IP */
    uint8_t  siaddr[4];  /* server IP */
    uint8_t  giaddr[4];  /* gateway IP */
    uint8_t  chaddr[16]; /* client hardware address */
    uint8_t  sname[64];  /* server host name */
    uint8_t  file[128];  /* boot file name */
    uint8_t  magic[4];   /* DHCP magic cookie: 99.130.83.99 */
    uint8_t  options[312]; /* DHCP options */
} __attribute__((packed)) dhcp_packet_t;

#define DHCP_OP_REQUEST    1
#define DHCP_OP_REPLY      2
#define DHCP_MAGIC_0       99
#define DHCP_MAGIC_1       130
#define DHCP_MAGIC_2       83
#define DHCP_MAGIC_3       99

/* DHCP option codes */
#define DHCP_OPT_MSG_TYPE  53
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_REQ_IP    50
#define DHCP_OPT_SUBNET    1
#define DHCP_OPT_ROUTER    3
#define DHCP_OPT_DNS       6
#define DHCP_OPT_END       255

/* DHCP message types */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

void dhcp_initialize(void) {
    /* nothing */
}

/* Find a DHCP option in the options field */
static uint8_t* dhcp_find_option(uint8_t* options, size_t len, uint8_t code) {
    size_t pos = 0;
    while (pos < len) {
        if (options[pos] == DHCP_OPT_END) break;
        if (options[pos] == 0) { pos++; continue; } /* padding */
        if (pos + 1 >= len) break;
        uint8_t opt_code = options[pos];
        uint8_t opt_len = options[pos + 1];
        if (opt_code == code) return &options[pos];
        pos += 2 + opt_len;
    }
    return NULL;
}

static void build_dhcp_base(dhcp_packet_t* pkt, uint32_t xid) {
    memset(pkt, 0, sizeof(dhcp_packet_t));
    pkt->op = DHCP_OP_REQUEST;
    pkt->htype = 1;
    pkt->hlen = 6;
    pkt->xid = xid;
    pkt->flags = htons(0x8000); /* broadcast flag */

    net_config_t* cfg = net_get_config();
    memcpy(pkt->chaddr, cfg->mac, 6);

    pkt->magic[0] = DHCP_MAGIC_0;
    pkt->magic[1] = DHCP_MAGIC_1;
    pkt->magic[2] = DHCP_MAGIC_2;
    pkt->magic[3] = DHCP_MAGIC_3;
}

int dhcp_discover(void) {
    uint32_t xid = pit_get_ticks() ^ 0xDEADBEEF;

    /* Need to temporarily set our IP to 0.0.0.0 for DHCP */
    net_config_t* cfg = net_get_config();
    uint8_t old_ip[4];
    memcpy(old_ip, cfg->ip, 4);

    /* Bind UDP port 68 */
    udp_bind(68);

    /* --- DISCOVER --- */
    dhcp_packet_t discover;
    build_dhcp_base(&discover, xid);
    discover.options[0] = DHCP_OPT_MSG_TYPE;
    discover.options[1] = 1;
    discover.options[2] = DHCP_DISCOVER;
    discover.options[3] = DHCP_OPT_END;

    uint8_t broadcast[4] = {255, 255, 255, 255};
    printf("DHCP: Sending DISCOVER...\n");

    /* Temporarily set IP to 0.0.0.0 to allow broadcast */
    net_set_ip(0, 0, 0, 0);
    udp_send(broadcast, 67, 68, (uint8_t*)&discover, sizeof(dhcp_packet_t));

    /* Wait for OFFER */
    uint8_t resp_buf[sizeof(dhcp_packet_t)];
    size_t resp_len = sizeof(resp_buf);
    uint8_t src_ip[4];
    uint16_t src_port;

    if (udp_recv(68, resp_buf, &resp_len, src_ip, &src_port, 5000) != 0) {
        printf("DHCP: No OFFER received (timeout)\n");
        net_set_ip(old_ip[0], old_ip[1], old_ip[2], old_ip[3]);
        udp_unbind(68);
        return -1;
    }

    dhcp_packet_t* offer = (dhcp_packet_t*)resp_buf;
    if (offer->op != DHCP_OP_REPLY || offer->xid != xid) {
        printf("DHCP: Invalid OFFER\n");
        net_set_ip(old_ip[0], old_ip[1], old_ip[2], old_ip[3]);
        udp_unbind(68);
        return -1;
    }

    uint8_t offered_ip[4];
    memcpy(offered_ip, offer->yiaddr, 4);
    printf("DHCP: Got OFFER: %d.%d.%d.%d\n",
           offered_ip[0], offered_ip[1], offered_ip[2], offered_ip[3]);

    /* Get server ID */
    uint8_t server_id[4] = {0};
    uint8_t* opt = dhcp_find_option(offer->options, sizeof(offer->options), DHCP_OPT_SERVER_ID);
    if (opt && opt[1] >= 4) memcpy(server_id, opt + 2, 4);

    /* --- REQUEST --- */
    dhcp_packet_t request;
    build_dhcp_base(&request, xid);
    size_t opt_pos = 0;
    request.options[opt_pos++] = DHCP_OPT_MSG_TYPE;
    request.options[opt_pos++] = 1;
    request.options[opt_pos++] = DHCP_REQUEST;
    request.options[opt_pos++] = DHCP_OPT_REQ_IP;
    request.options[opt_pos++] = 4;
    memcpy(&request.options[opt_pos], offered_ip, 4);
    opt_pos += 4;
    request.options[opt_pos++] = DHCP_OPT_SERVER_ID;
    request.options[opt_pos++] = 4;
    memcpy(&request.options[opt_pos], server_id, 4);
    opt_pos += 4;
    request.options[opt_pos++] = DHCP_OPT_END;

    printf("DHCP: Sending REQUEST...\n");
    udp_send(broadcast, 67, 68, (uint8_t*)&request, sizeof(dhcp_packet_t));

    /* Wait for ACK */
    resp_len = sizeof(resp_buf);
    if (udp_recv(68, resp_buf, &resp_len, src_ip, &src_port, 5000) != 0) {
        printf("DHCP: No ACK received (timeout)\n");
        net_set_ip(old_ip[0], old_ip[1], old_ip[2], old_ip[3]);
        udp_unbind(68);
        return -1;
    }

    dhcp_packet_t* ack = (dhcp_packet_t*)resp_buf;
    if (ack->op != DHCP_OP_REPLY || ack->xid != xid) {
        printf("DHCP: Invalid ACK\n");
        net_set_ip(old_ip[0], old_ip[1], old_ip[2], old_ip[3]);
        udp_unbind(68);
        return -1;
    }

    /* Apply configuration */
    net_set_ip(ack->yiaddr[0], ack->yiaddr[1], ack->yiaddr[2], ack->yiaddr[3]);

    /* Parse options for subnet, gateway, DNS */
    opt = dhcp_find_option(ack->options, sizeof(ack->options), DHCP_OPT_SUBNET);
    if (opt && opt[1] >= 4) {
        memcpy(cfg->netmask, opt + 2, 4);
    }

    opt = dhcp_find_option(ack->options, sizeof(ack->options), DHCP_OPT_ROUTER);
    if (opt && opt[1] >= 4) {
        memcpy(cfg->gateway, opt + 2, 4);
    }

    printf("DHCP: Assigned %d.%d.%d.%d\n",
           cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]);

    udp_unbind(68);
    return 0;
}
