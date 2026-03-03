#include <kernel/shell_cmd.h>
#include <kernel/sh_parse.h>
#include <kernel/net.h>
#include <kernel/ip.h>
#include <kernel/dns.h>
#include <kernel/dhcp.h>
#include <kernel/httpd.h>
#include <kernel/firewall.h>
#include <kernel/arp.h>
#include <kernel/http.h>
#include <kernel/rtc.h>
#include <kernel/config.h>
#include <kernel/fs.h>
#include <kernel/tls.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void cmd_wget(int argc, char* argv[]) {
    http_response_t resp;

    const char *url = NULL;
    const char *outfile = NULL;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O") == 0 && i + 1 < argc) {
            outfile = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else {
            url = argv[i];
        }
    }

    if (!url) {
        printf("Usage: wget [-v] [-O FILE] URL\n");
        return;
    }

    http_set_verbose(verbose);
    if (!verbose)
        printf("Connecting to %s...\n", url);
    int rc = http_get(url, &resp);
    http_set_verbose(0);
    if (rc < 0) {
        printf("wget: failed (error %d)\n", rc);
        return;
    }

    printf("HTTP %d, %u bytes", resp.status_code, resp.body_len);
    if (resp.content_type[0])
        printf(" (%s)", resp.content_type);
    printf("\n");

    if (outfile) {
        /* Write to file */
        if (fs_create_file(outfile, 0) < 0 && resp.body_len > 0) {
            /* File might already exist, try writing anyway */
        }
        if (resp.body_len > 0) {
            uint32_t parent;
            char name[28];
            int ino = fs_resolve_path(outfile, &parent, name);
            if (ino >= 0) {
                int w = fs_write_at((uint32_t)ino, (const uint8_t *)resp.body, 0, resp.body_len);
                if (w > 0)
                    printf("Saved %d bytes to '%s'\n", w, outfile);
                else
                    printf("wget: write failed\n");
            } else {
                printf("wget: cannot create '%s'\n", outfile);
            }
        }
    } else {
        /* Print body to stdout */
        for (uint32_t i = 0; i < resp.body_len; i++)
            putchar(resp.body[i]);
        if (resp.body_len > 0 && resp.body[resp.body_len - 1] != '\n')
            putchar('\n');
    }

    http_response_free(&resp);
}

static void cmd_ifconfig(int argc, char* argv[]) {
    net_config_t* config = net_get_config();

    if (argc == 1) {
        /* Display current configuration */
        printf("eth0: flags=");
        if (config->link_up) {
            printf("UP");
        } else {
            printf("DOWN");
        }
        printf("\n");

        printf("    inet ");
        net_print_ip(config->ip);
        printf("  netmask ");
        net_print_ip(config->netmask);
        printf("\n");

        printf("    ether ");
        net_print_mac(config->mac);
        printf("\n");

        printf("    gateway ");
        net_print_ip(config->gateway);
        printf("\n");

    } else if (argc >= 2) {
        const char* iface = argv[1];

        if (strcmp(iface, "eth0") != 0) {
            printf("Unknown interface: %s\n", iface);
            return;
        }

        if (argc == 3 && strcmp(argv[2], "up") == 0) {
            config->link_up = 1;
            printf("Interface eth0 enabled\n");
        } else if (argc == 3 && strcmp(argv[2], "down") == 0) {
            config->link_up = 0;
            printf("Interface eth0 disabled\n");
        } else if (argc == 4) {
            /* Set IP and netmask: ifconfig eth0 10.0.2.15 255.255.255.0 */
            /* Parse IP address */
            const char* ip_str = argv[2];
            int a = 0, b = 0, c = 0, d = 0;
            const char* p = ip_str;

            while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid IP format\n"); return; }
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid IP format\n"); return; }
            while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid IP format\n"); return; }
            while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');

            config->ip[0] = a;
            config->ip[1] = b;
            config->ip[2] = c;
            config->ip[3] = d;

            /* Parse netmask */
            const char* mask_str = argv[3];
            a = b = c = d = 0;
            p = mask_str;

            while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid netmask format\n"); return; }
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid netmask format\n"); return; }
            while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
            if (*p++ != '.') { printf("Invalid netmask format\n"); return; }
            while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');

            config->netmask[0] = a;
            config->netmask[1] = b;
            config->netmask[2] = c;
            config->netmask[3] = d;

            printf("IP address set to ");
            net_print_ip(config->ip);
            printf("\n");
            printf("Netmask set to ");
            net_print_ip(config->netmask);
            printf("\n");
        } else {
            printf("Usage: ifconfig [interface] [up|down|IP NETMASK]\n");
        }
    }
}

static void cmd_ping(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: ping HOST\n");
        return;
    }

    const char* host = argv[1];

    /* Parse IP address */
    int a = 0, b = 0, c = 0, d = 0;
    const char* p = host;

    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');

    uint8_t dst_ip[4] = {a, b, c, d};

    printf("PING %d.%d.%d.%d\n", a, b, c, d);

    int ping_tid = task_register("ping", 1, -1);
    for (int i = 1; i <= 4; i++) {
        if (ping_tid >= 0 && task_check_killed(ping_tid)) break;
        icmp_send_echo_request(dst_ip, 1, i);

        /* Wait and process packets */
        for (int attempts = 0; attempts < 20; attempts++) {
            net_process_packets();
            for (volatile int j = 0; j < 500000; j++);
        }

        /* Delay between pings */
        for (volatile int j = 0; j < 1000000; j++);
    }
    if (ping_tid >= 0) task_unregister(ping_tid);

    printf("\n");
}

static void cmd_arp(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: arp IP\n");
        return;
    }

    const char* host = argv[1];

    /* Parse IP address */
    int a = 0, b = 0, c = 0, d = 0;
    const char* p = host;

    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p++ != '.') { printf("Invalid IP format\n"); return; }
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');

    uint8_t target_ip[4] = {a, b, c, d};

    printf("ARP request for %d.%d.%d.%d ... ", a, b, c, d);

    /* Send ARP request */
    arp_send_request(target_ip);

    /* Wait and process packets */
    for (int i = 0; i < 20; i++) {
        net_process_packets();
        for (volatile int j = 0; j < 500000; j++);
    }

    printf("\n");
}

static void cmd_nslookup(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: nslookup HOSTNAME\n");
        return;
    }

    uint8_t ip[4];
    if (dns_resolve(argv[1], ip) == 0) {
        printf("%s: %d.%d.%d.%d\n", argv[1], ip[0], ip[1], ip[2], ip[3]);
    } else {
        printf("nslookup: could not resolve %s\n", argv[1]);
    }
}

static void cmd_dhcp_cmd(int argc, char* argv[]) {
    (void)argc; (void)argv;
    dhcp_discover();
}

static void cmd_httpd(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: httpd start|stop\n");
        return;
    }
    if (strcmp(argv[1], "start") == 0) {
        httpd_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        httpd_stop();
    } else {
        printf("Usage: httpd start|stop\n");
    }
}

static void cmd_connect(int argc, char* argv[]) {
    (void)argc; (void)argv;
    net_config_t* cfg = net_get_config();
    if (!cfg->link_up) {
        printf("connect: no network interface available\n");
        return;
    }

    printf("Running DHCP discovery...\n");
    if (dhcp_discover() == 0) {
        /* Re-fetch config after DHCP updated it */
        cfg = net_get_config();
        printf("Network configured:\n");
        printf("  IP:      "); net_print_ip(cfg->ip); printf("\n");
        printf("  Netmask: "); net_print_ip(cfg->netmask); printf("\n");
        printf("  Gateway: "); net_print_ip(cfg->gateway); printf("\n");
    } else {
        printf("connect: DHCP discovery failed\n");
    }
}

static int parse_ip(const char *s, uint8_t ip[4]) {
    int a, b, c, d;
    const char *p = s;
    a = atoi(p); while (*p && *p != '.') p++; if (*p) p++;
    b = atoi(p); while (*p && *p != '.') p++; if (*p) p++;
    c = atoi(p); while (*p && *p != '.') p++; if (*p) p++;
    d = atoi(p);
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255)
        return -1;
    ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
    return 0;
}

static void cmd_firewall(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: firewall list|add|del|flush|default\n");
        return;
    }

    if (strcmp(argv[1], "list") == 0) {
        int n = firewall_rule_count();
        printf("Default policy: %s\n", firewall_get_default() == FW_ACTION_ALLOW ? "ALLOW" : "DENY");
        if (n == 0) {
            printf("No rules.\n");
            return;
        }
        printf("%-4s %-6s %-5s %-16s %-12s\n", "Idx", "Action", "Proto", "Src IP", "Dst Port");
        for (int i = 0; i < n; i++) {
            const fw_rule_t *r = firewall_get_rule(i);
            if (!r) continue;
            const char *act = r->action == FW_ACTION_ALLOW ? "ALLOW" : "DENY";
            const char *proto = "all";
            if (r->protocol == FW_PROTO_TCP)  proto = "tcp";
            if (r->protocol == FW_PROTO_UDP)  proto = "udp";
            if (r->protocol == FW_PROTO_ICMP) proto = "icmp";

            char src[32] = "any";
            uint8_t zero[4] = {0,0,0,0};
            if (memcmp(r->src_ip, zero, 4) != 0 || memcmp(r->src_mask, zero, 4) != 0) {
                snprintf(src, sizeof(src), "%d.%d.%d.%d",
                         r->src_ip[0], r->src_ip[1], r->src_ip[2], r->src_ip[3]);
            }

            char port[20] = "any";
            if (r->dst_port_max > 0) {
                if (r->dst_port_min == r->dst_port_max)
                    snprintf(port, sizeof(port), "%d", r->dst_port_min);
                else
                    snprintf(port, sizeof(port), "%d-%d", r->dst_port_min, r->dst_port_max);
            }

            printf("%-4d %-6s %-5s %-16s %-12s\n", i, act, proto, src, port);
        }
    } else if (strcmp(argv[1], "add") == 0) {
        if (argc < 4) {
            printf("Usage: firewall add allow|deny tcp|udp|icmp|all [src_ip[/mask]] [port[-port]]\n");
            return;
        }
        fw_rule_t rule;
        memset(&rule, 0, sizeof(rule));
        rule.enabled = 1;

        if (strcmp(argv[2], "allow") == 0)      rule.action = FW_ACTION_ALLOW;
        else if (strcmp(argv[2], "deny") == 0)   rule.action = FW_ACTION_DENY;
        else { printf("firewall: action must be allow or deny\n"); return; }

        if (strcmp(argv[3], "tcp") == 0)        rule.protocol = FW_PROTO_TCP;
        else if (strcmp(argv[3], "udp") == 0)    rule.protocol = FW_PROTO_UDP;
        else if (strcmp(argv[3], "icmp") == 0)   rule.protocol = FW_PROTO_ICMP;
        else if (strcmp(argv[3], "all") == 0)    rule.protocol = FW_PROTO_ALL;
        else { printf("firewall: protocol must be tcp, udp, icmp, or all\n"); return; }

        for (int i = 4; i < argc; i++) {
            /* Check if it's an IP with optional mask */
            char *slash = strchr(argv[i], '/');
            if (slash && strchr(argv[i], '.')) {
                *slash = '\0';
                if (parse_ip(argv[i], rule.src_ip) != 0) {
                    printf("firewall: bad IP '%s'\n", argv[i]);
                    return;
                }
                if (parse_ip(slash + 1, rule.src_mask) != 0) {
                    printf("firewall: bad mask '%s'\n", slash + 1);
                    return;
                }
            } else if (strchr(argv[i], '.')) {
                if (parse_ip(argv[i], rule.src_ip) != 0) {
                    printf("firewall: bad IP '%s'\n", argv[i]);
                    return;
                }
                memset(rule.src_mask, 255, 4);
            } else if (strcmp(argv[i], "all") == 0 || strcmp(argv[i], "any") == 0) {
                /* Leave src as 0.0.0.0/0.0.0.0 = any */
            } else {
                /* Port or port range */
                char *dash = strchr(argv[i], '-');
                if (dash) {
                    *dash = '\0';
                    rule.dst_port_min = (uint16_t)atoi(argv[i]);
                    rule.dst_port_max = (uint16_t)atoi(dash + 1);
                } else {
                    rule.dst_port_min = (uint16_t)atoi(argv[i]);
                    rule.dst_port_max = rule.dst_port_min;
                }
            }
        }

        if (firewall_add_rule(&rule) == 0)
            printf("Rule added (%d/%d)\n", firewall_rule_count(), FW_MAX_RULES);
        else
            printf("firewall: rule table full\n");

    } else if (strcmp(argv[1], "del") == 0) {
        if (argc < 3) { printf("Usage: firewall del INDEX\n"); return; }
        int idx = atoi(argv[2]);
        if (firewall_del_rule(idx) == 0)
            printf("Rule %d deleted\n", idx);
        else
            printf("firewall: invalid index\n");

    } else if (strcmp(argv[1], "flush") == 0) {
        firewall_flush();
        printf("All rules flushed\n");

    } else if (strcmp(argv[1], "default") == 0) {
        if (argc < 3) { printf("Usage: firewall default allow|deny\n"); return; }
        if (strcmp(argv[2], "allow") == 0)
            firewall_set_default(FW_ACTION_ALLOW);
        else if (strcmp(argv[2], "deny") == 0)
            firewall_set_default(FW_ACTION_DENY);
        else { printf("firewall: must be allow or deny\n"); return; }
        printf("Default policy: %s\n", argv[2]);

    } else {
        printf("Usage: firewall list|add|del|flush|default\n");
    }
}

static void cmd_ntpdate(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("Syncing time via NTP (pool.ntp.org)...\n");
    if (rtc_ntp_sync() == 0) {
        datetime_t dt;
        config_get_datetime(&dt);
        printf("Time synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
               dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        printf("NTP sync failed (check network connection)\n");
    }
}

static const command_t net_commands[] = {
    {
        "ifconfig", cmd_ifconfig,
        "Configure network interface parameters",
        "ifconfig: ifconfig [interface] [options]\n"
        "    Display or configure network interface parameters.\n"
        "    Without arguments, shows current network configuration.\n"
        "    Options:\n"
        "      up                  Enable the network interface\n"
        "      down                Disable the network interface\n"
        "      IP NETMASK          Set IP address and netmask\n",
        "NAME\n"
        "    ifconfig - configure network interface\n\n"
        "SYNOPSIS\n"
        "    ifconfig [interface] [options]\n\n"
        "DESCRIPTION\n"
        "    Configure network interface parameters or display\n"
        "    current network configuration.\n\n"
        "EXAMPLES\n"
        "    ifconfig\n"
        "        Show current network configuration\n\n"
        "    ifconfig eth0 10.0.2.15 255.255.255.0\n"
        "        Set IP address and netmask\n\n"
        "    ifconfig eth0 up\n"
        "        Enable network interface\n",
        CMD_FLAG_ROOT
    },
    {
        "ping", cmd_ping,
        "Send ICMP ECHO_REQUEST to network hosts",
        "ping: ping HOST\n"
        "    Send ICMP ECHO_REQUEST packets to HOST.\n",
        "NAME\n"
        "    ping - send ICMP ECHO_REQUEST to network hosts\n\n"
        "SYNOPSIS\n"
        "    ping HOST\n\n"
        "DESCRIPTION\n"
        "    Send ICMP ECHO_REQUEST packets to HOST and wait\n"
        "    for ECHO_RESPONSE. This is useful for testing\n"
        "    network connectivity.\n\n"
        "EXAMPLES\n"
        "    ping 10.0.2.2\n"
        "        Ping the default gateway\n",
        0
    },
    {
        "arp", cmd_arp,
        "Test ARP request/reply",
        "arp: arp IP\n"
        "    Send ARP request and wait for reply.\n",
        "NAME\n"
        "    arp - test ARP protocol\n\n"
        "SYNOPSIS\n"
        "    arp IP\n\n"
        "DESCRIPTION\n"
        "    Sends an ARP request for the given IP address\n"
        "    and displays the MAC address in the reply.\n"
        "    This tests if network RX actually works.\n",
        0
    },
    {
        "nslookup", cmd_nslookup,
        "Query DNS to resolve a hostname",
        "nslookup: nslookup HOSTNAME\n"
        "    Resolve HOSTNAME to an IP address using DNS.\n",
        "NAME\n"
        "    nslookup - query Internet name servers\n\n"
        "SYNOPSIS\n"
        "    nslookup HOSTNAME\n\n"
        "DESCRIPTION\n"
        "    Sends a DNS type-A query to the configured DNS server\n"
        "    (default 10.0.2.3 for QEMU SLIRP) and prints the\n"
        "    resolved IPv4 address.\n",
        0
    },
    {
        "dhcp", cmd_dhcp_cmd,
        "Obtain an IP address via DHCP",
        "dhcp: dhcp\n"
        "    Run DHCP discovery to obtain an IP address.\n",
        "NAME\n"
        "    dhcp - Dynamic Host Configuration Protocol client\n\n"
        "SYNOPSIS\n"
        "    dhcp\n\n"
        "DESCRIPTION\n"
        "    Sends DHCP Discover/Offer/Request/Acknowledge sequence\n"
        "    to obtain a network configuration from the DHCP server.\n",
        CMD_FLAG_ROOT
    },
    {
        "httpd", cmd_httpd,
        "Start or stop the HTTP server",
        "httpd: httpd start|stop\n"
        "    Start or stop the built-in HTTP server on port 80.\n",
        "NAME\n"
        "    httpd - minimal HTTP/1.0 server\n\n"
        "SYNOPSIS\n"
        "    httpd start|stop\n\n"
        "DESCRIPTION\n"
        "    Starts a minimal HTTP server on port 80. It serves\n"
        "    static HTML for / and files from the filesystem.\n"
        "    Use 'httpd stop' to shut it down.\n",
        CMD_FLAG_ROOT
    },
    {
        "connect", cmd_connect,
        "Auto-configure network via DHCP",
        "connect: connect\n"
        "    Bring up the network by running DHCP discovery.\n"
        "    Displays assigned IP, netmask, and gateway on success.\n",
        "NAME\n"
        "    connect - auto-configure network via DHCP\n\n"
        "SYNOPSIS\n"
        "    connect\n\n"
        "DESCRIPTION\n"
        "    Checks that a NIC is present and the link is up,\n"
        "    then runs DHCP discovery to obtain an IP address,\n"
        "    netmask, and gateway from the network. Prints the\n"
        "    assigned configuration on success.\n",
        CMD_FLAG_ROOT
    },
    {
        "firewall", cmd_firewall,
        "Manage packet filtering rules",
        "firewall: firewall list|add|del|flush|default\n"
        "    Manage the packet filtering firewall.\n",
        "NAME\n"
        "    firewall - manage packet filtering rules\n\n"
        "SYNOPSIS\n"
        "    firewall list\n"
        "    firewall add allow|deny tcp|udp|icmp|all [SRC_IP[/MASK]] [PORT[-PORT]]\n"
        "    firewall del INDEX\n"
        "    firewall flush\n"
        "    firewall default allow|deny\n\n"
        "DESCRIPTION\n"
        "    A minimal stateless packet filter. Rules are evaluated\n"
        "    top-to-bottom; first match wins. Default policy applies\n"
        "    if no rule matches (default: allow).\n\n"
        "    list     Show all rules and default policy.\n"
        "    add      Add a rule. Protocol: tcp, udp, icmp, or all.\n"
        "             Optional SRC_IP with /MASK (e.g. 10.0.2.0/255.255.255.0).\n"
        "             Optional port or port range (e.g. 80 or 1024-65535).\n"
        "    del N    Delete rule at index N.\n"
        "    flush    Remove all rules.\n"
        "    default  Set default policy to allow or deny.\n",
        CMD_FLAG_ROOT
    },
    {
        "wget", cmd_wget,
        "Download a file via HTTP/HTTPS",
        "wget: wget [-v] [-O FILE] URL\n"
        "    Download a file from the given HTTP or HTTPS URL.\n"
        "    Follows redirects (up to 5 hops).\n"
        "    -v:      verbose output (DNS, TLS, headers).\n"
        "    -O FILE: save response body to FILE.\n"
        "    Without -O, prints the response to stdout.\n",
        NULL,
        0
    },
    {
        "ntpdate", cmd_ntpdate,
        "Synchronize system clock via NTP",
        "ntpdate: ntpdate\n"
        "    Sync system clock from pool.ntp.org via NTP.\n",
        "NAME\n"
        "    ntpdate - set date and time via NTP\n\n"
        "SYNOPSIS\n"
        "    ntpdate\n\n"
        "DESCRIPTION\n"
        "    Contacts pool.ntp.org via UDP port 123 to obtain\n"
        "    the current time and updates the system clock.\n"
        "    Requires an active network connection.\n",
        CMD_FLAG_ROOT
    },
};

const command_t *cmd_net_commands(int *count) {
    *count = sizeof(net_commands) / sizeof(net_commands[0]);
    return net_commands;
}
