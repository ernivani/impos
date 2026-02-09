#ifndef _KERNEL_DHCP_H
#define _KERNEL_DHCP_H

#include <stdint.h>

void dhcp_initialize(void);
int  dhcp_discover(void);

#endif
