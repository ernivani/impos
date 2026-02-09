#ifndef _KERNEL_FIREWALL_H
#define _KERNEL_FIREWALL_H

#include <stdint.h>

#define FW_MAX_RULES 16

#define FW_ACTION_DENY  0
#define FW_ACTION_ALLOW 1

#define FW_PROTO_ALL  0
#define FW_PROTO_ICMP 1
#define FW_PROTO_TCP  6
#define FW_PROTO_UDP  17

typedef struct {
    uint8_t  src_ip[4];
    uint8_t  src_mask[4];
    uint8_t  dst_ip[4];
    uint8_t  dst_mask[4];
    uint16_t dst_port_min;
    uint16_t dst_port_max;
    uint8_t  protocol;
    uint8_t  action;
    int      enabled;
} fw_rule_t;

void firewall_initialize(void);
int  firewall_check(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                     uint8_t protocol, uint16_t dst_port);
int  firewall_add_rule(const fw_rule_t *rule);
int  firewall_del_rule(int index);
void firewall_flush(void);
void firewall_set_default(int action);
int  firewall_get_default(void);
int  firewall_rule_count(void);
const fw_rule_t* firewall_get_rule(int index);

#endif
