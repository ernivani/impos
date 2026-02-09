#include <kernel/firewall.h>
#include <string.h>

static fw_rule_t rules[FW_MAX_RULES];
static int rule_count;
static int default_action;

void firewall_initialize(void) {
    rule_count = 0;
    default_action = FW_ACTION_ALLOW;
    memset(rules, 0, sizeof(rules));
}

static int ip_match(const uint8_t ip[4], const uint8_t rule_ip[4], const uint8_t mask[4]) {
    /* Mask of 0.0.0.0 means "any" */
    uint8_t zero[4] = {0, 0, 0, 0};
    if (memcmp(mask, zero, 4) == 0 && memcmp(rule_ip, zero, 4) == 0)
        return 1;
    for (int i = 0; i < 4; i++) {
        if ((ip[i] & mask[i]) != (rule_ip[i] & mask[i]))
            return 0;
    }
    return 1;
}

int firewall_check(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                    uint8_t protocol, uint16_t dst_port) {
    for (int i = 0; i < rule_count; i++) {
        const fw_rule_t *r = &rules[i];
        if (!r->enabled) continue;

        /* Check protocol */
        if (r->protocol != FW_PROTO_ALL && r->protocol != protocol)
            continue;

        /* Check source IP */
        if (!ip_match(src_ip, r->src_ip, r->src_mask))
            continue;

        /* Check destination IP */
        if (!ip_match(dst_ip, r->dst_ip, r->dst_mask))
            continue;

        /* Check destination port */
        if (r->dst_port_max > 0) {
            if (dst_port < r->dst_port_min || dst_port > r->dst_port_max)
                continue;
        }

        /* First match wins */
        return r->action;
    }
    return default_action;
}

int firewall_add_rule(const fw_rule_t *rule) {
    if (rule_count >= FW_MAX_RULES)
        return -1;
    memcpy(&rules[rule_count], rule, sizeof(fw_rule_t));
    rules[rule_count].enabled = 1;
    rule_count++;
    return 0;
}

int firewall_del_rule(int index) {
    if (index < 0 || index >= rule_count)
        return -1;
    for (int i = index; i < rule_count - 1; i++)
        rules[i] = rules[i + 1];
    rule_count--;
    memset(&rules[rule_count], 0, sizeof(fw_rule_t));
    return 0;
}

void firewall_flush(void) {
    rule_count = 0;
    memset(rules, 0, sizeof(rules));
}

void firewall_set_default(int action) {
    default_action = action;
}

int firewall_get_default(void) {
    return default_action;
}

int firewall_rule_count(void) {
    return rule_count;
}

const fw_rule_t* firewall_get_rule(int index) {
    if (index < 0 || index >= rule_count)
        return 0;
    return &rules[index];
}
