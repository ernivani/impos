#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/gfx.h>
#include <string.h>
#include <stdio.h>

/* Page directory and page tables — static, page-aligned in BSS.
 * 64 page tables identity-map 0..256MB with 4KB granularity.
 * Everything above 256MB uses 4MB PSE pages (covers ACPI, framebuffer, MMIO). */
#define IDENTITY_TABLES  64   /* 64 * 4MB = 256MB */

static uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));
uint32_t kernel_page_tables[IDENTITY_TABLES][1024] __attribute__((aligned(4096)));

void vmm_init(multiboot_info_t *mbi) {
    (void)mbi;

    memset(kernel_page_directory, 0, sizeof(kernel_page_directory));
    memset(kernel_page_tables, 0, sizeof(kernel_page_tables));

    /* Enable PSE (Page Size Extension) in CR4 for 4MB pages */
    uint32_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10;  /* CR4.PSE */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    /* Identity-map first 256MB with 4KB pages (fine-grained control) */
    for (int i = 0; i < IDENTITY_TABLES; i++) {
        uint32_t base = (uint32_t)i * 1024 * PAGE_SIZE;  /* i * 4MB */
        for (int j = 0; j < 1024; j++) {
            kernel_page_tables[i][j] = (base + j * PAGE_SIZE)
                                       | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        }
        kernel_page_directory[i] = (uint32_t)&kernel_page_tables[i]
                                   | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }

    /* Identity-map 256MB..4GB with 4MB PSE pages.
     * This covers ACPI tables (~0xBFFE0000), framebuffer (~0xFD000000),
     * and any other firmware/MMIO regions. */
    uint32_t fb_phys = (uint32_t)gfx_framebuffer();
    uint32_t fb_size = gfx_height() * gfx_pitch();
    uint32_t fb_pde_start = fb_phys / (4 * 1024 * 1024);
    uint32_t fb_pde_end = (fb_phys + fb_size + 4 * 1024 * 1024 - 1) / (4 * 1024 * 1024);

    for (int i = IDENTITY_TABLES; i < 1024; i++) {
        uint32_t phys = (uint32_t)i * 4 * 1024 * 1024;
        uint32_t flags = PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_4MB;

        /* Disable caching for framebuffer region */
        if (fb_phys != 0 && (uint32_t)i >= fb_pde_start && (uint32_t)i < fb_pde_end)
            flags |= PTE_NOCACHE | PTE_WRITETHROUGH;

        kernel_page_directory[i] = phys | flags;
    }

    /* Load page directory into CR3 and enable paging (CR0 bit 31) */
    uint32_t cr3 = (uint32_t)&kernel_page_directory;
    __asm__ volatile (
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or  $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        : : "r"(cr3) : "eax"
    );

    printf("[VMM] Paging enabled (4KB: 0-256MB, 4MB PSE: 256MB-4GB). CR3=0x%x\n", cr3);
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    /* Check if this PDE uses a 4MB page — can't do 4KB mapping there */
    if ((kernel_page_directory[pde_idx] & PTE_PRESENT) &&
        (kernel_page_directory[pde_idx] & PTE_4MB)) {
        printf("[VMM] WARN: PDE %u is a 4MB page, can't map 4KB (virt 0x%x)\n", pde_idx, virt);
        return;
    }

    /* Get or verify page table exists */
    if (!(kernel_page_directory[pde_idx] & PTE_PRESENT)) {
        printf("[VMM] WARN: No page table for PDE %u (virt 0x%x)\n", pde_idx, virt);
        return;
    }

    uint32_t *pt = (uint32_t *)(kernel_page_directory[pde_idx] & PAGE_MASK);
    pt[pte_idx] = (phys & PAGE_MASK) | (flags & 0xFFF);
    vmm_invlpg(virt);
}

void vmm_unmap_page(uint32_t virt) {
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    if (!(kernel_page_directory[pde_idx] & PTE_PRESENT))
        return;
    if (kernel_page_directory[pde_idx] & PTE_4MB)
        return;  /* Can't unmap within a 4MB page */

    uint32_t *pt = (uint32_t *)(kernel_page_directory[pde_idx] & PAGE_MASK);
    pt[pte_idx] = 0;
    vmm_invlpg(virt);
}

void vmm_invlpg(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint32_t vmm_get_kernel_pagedir(void) {
    return (uint32_t)&kernel_page_directory;
}

uint32_t vmm_create_user_pagedir(void) {
    uint32_t pd_phys = pmm_alloc_frame();
    if (!pd_phys) return 0;

    /* Copy all 1024 PDEs from the kernel page directory.
     * Since pd_phys is identity-mapped (< 256MB), we can write directly. */
    uint32_t *pd = (uint32_t *)pd_phys;
    memcpy(pd, kernel_page_directory, 4096);

    return pd_phys;
}

uint32_t vmm_map_user_page(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t *pd = (uint32_t *)pd_phys;
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    uint32_t pt_phys;

    if (!(pd[pde_idx] & PTE_PRESENT) || (pd[pde_idx] & PTE_4MB)) {
        /* No page table here (or it's a 4MB PSE page) — allocate a fresh one */
        pt_phys = pmm_alloc_frame();
        if (!pt_phys) return 0;
        memset((void *)pt_phys, 0, 4096);
        pd[pde_idx] = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        pt_phys = pd[pde_idx] & PAGE_MASK;
    }

    uint32_t *pt = (uint32_t *)pt_phys;
    pt[pte_idx] = (phys & PAGE_MASK) | (flags & 0xFFF);

    return pt_phys;
}

void vmm_destroy_user_pagedir(uint32_t pd_phys) {
    pmm_free_frame(pd_phys);
}
