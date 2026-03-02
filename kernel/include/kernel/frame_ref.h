#ifndef _KERNEL_FRAME_REF_H
#define _KERNEL_FRAME_REF_H

#include <stdint.h>

/* Physical frame reference counting for COW fork.
 * One byte per frame (65536 frames = 64KB). Saturates at 255. */

void frame_ref_init(void);

/* Increment reference count for a physical frame address. */
void frame_ref_inc(uint32_t phys);

/* Decrement reference count. Returns new count. Caller frees frame at 0. */
int  frame_ref_dec(uint32_t phys);

/* Get current reference count. */
int  frame_ref_get(uint32_t phys);

/* Set reference count to 1 (used by pmm_alloc_frame). */
void frame_ref_set1(uint32_t phys);

#endif
