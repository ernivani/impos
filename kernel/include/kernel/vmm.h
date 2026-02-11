#ifndef _KERNEL_VMM_H
#define _KERNEL_VMM_H

#include <stdint.h>
#include <kernel/multiboot.h>

/* Page table entry flags */
#define PTE_PRESENT     0x001
#define PTE_WRITABLE    0x002
#define PTE_USER        0x004
#define PTE_WRITETHROUGH 0x008
#define PTE_NOCACHE     0x010
#define PTE_ACCESSED    0x020
#define PTE_DIRTY       0x040
#define PTE_4MB         0x080
#define PTE_GLOBAL      0x100

#define PAGE_SIZE       4096
#define PAGE_MASK       (~0xFFF)

/* Initialize VMM: build identity-mapped page tables and enable paging */
void vmm_init(multiboot_info_t *mbi);

/* Map a single 4KB page: virt -> phys with given flags */
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/* Unmap a single 4KB page */
void vmm_unmap_page(uint32_t virt);

/* Invalidate a single TLB entry */
void vmm_invlpg(uint32_t virt);

/* Get the kernel page directory physical address */
uint32_t vmm_get_kernel_pagedir(void);

#endif
