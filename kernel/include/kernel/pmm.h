#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <stdint.h>
#include <kernel/multiboot.h>

/* Initialize the physical memory manager from multiboot mmap */
void pmm_init(multiboot_info_t *mbi);

/* Allocate a single 4KB-aligned physical frame. Returns 0 on failure. */
uint32_t pmm_alloc_frame(void);

/* Free a previously allocated physical frame */
void pmm_free_frame(uint32_t phys_addr);

/* Reserve a range of physical addresses (mark frames as used) */
void pmm_reserve_range(uint32_t phys_start, uint32_t phys_end);

/* Return the number of free frames */
uint32_t pmm_free_frame_count(void);

#endif
