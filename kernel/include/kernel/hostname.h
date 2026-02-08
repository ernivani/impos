#ifndef _KERNEL_HOSTNAME_H
#define _KERNEL_HOSTNAME_H

#define MAX_HOSTNAME 64

/* Initialize hostname system */
void hostname_initialize(void);

/* Get/set hostname */
const char* hostname_get(void);
int hostname_set(const char* name);

/* Load/save from/to /etc/hostname */
int hostname_load(void);
int hostname_save(void);

#endif
