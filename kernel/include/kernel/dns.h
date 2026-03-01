#ifndef _KERNEL_DNS_H
#define _KERNEL_DNS_H

#include <stdint.h>

void dns_initialize(void);
int dns_resolve(const char* hostname, uint8_t ip_out[4]);
void dns_cache_flush(void);

#endif
