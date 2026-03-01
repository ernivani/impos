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
#define PTE_GUARD       0x200  /* AVL bit 9: OS-defined guard page marker */
#define PTE_COW         0x400  /* AVL bit 10: copy-on-write marker */

#define PAGE_SIZE       4096
#define PAGE_MASK       (~0xFFF)

/* User-space virtual base for per-process mappings (PDE[256] = 1GB) */
#define USER_SPACE_BASE 0x40000000

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

/* Create a per-process page directory (copy of kernel PD). Returns phys addr. */
uint32_t vmm_create_user_pagedir(void);

/* Map a 4KB page in a specific page directory.
 * Returns the page table phys addr (caller tracks for cleanup), or 0 on failure. */
uint32_t vmm_map_user_page(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags);

/* Free a per-process page directory frame */
void vmm_destroy_user_pagedir(uint32_t pd_phys);

/* Guard page support: mark a page as guard (removes PRESENT, sets PTE_GUARD).
 * Returns 1 on success, 0 on failure. */
int vmm_set_guard_page(uint32_t virt);

/* Check if a faulting address is a guard page. If so, removes guard flag,
 * restores PRESENT, and returns 1. Returns 0 if not a guard page. */
int vmm_check_guard_page(uint32_t virt);

/* Ensure a page table exists for the PDE covering virt in a user page directory.
 * Allocates a new PT if needed (COW copies kernel PT). Does NOT create a PTE.
 * Returns the PT physical address, or 0 on failure. */
uint32_t vmm_ensure_pt(uint32_t pd_phys, uint32_t virt);

/* Read the PTE value for virt from a user page directory.
 * Returns 0 if no page table exists or PTE is clear. */
uint32_t vmm_get_pte(uint32_t pd_phys, uint32_t virt);

/* Unmap a 4KB page in a user page directory. Clears PTE and invlpg.
 * Does NOT free the physical frame (caller's responsibility). */
void vmm_unmap_user_page(uint32_t pd_phys, uint32_t virt);

/* Flush the entire TLB (reload CR3). */
void vmm_flush_tlb(void);

#endif
