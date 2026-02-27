#ifndef _KERNEL_VIRTIO_INPUT_H
#define _KERNEL_VIRTIO_INPUT_H

#include <stdint.h>

/* Initialize VirtIO tablet device (returns 1 if found, 0 otherwise) */
int  virtio_input_init(void);

/* Poll for pending input events (call from main loop) */
void virtio_input_poll(void);

/* Check if VirtIO input is active */
int  virtio_input_active(void);

#endif
