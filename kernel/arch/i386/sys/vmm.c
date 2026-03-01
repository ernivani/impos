#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/gfx.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/* Page directory and page tables — static, page-aligned in BSS.
 * 64 page tables identity-map 0..256MB with 4KB granularity.
 * Everything above 256MB uses 4MB PSE pages (covers ACPI, framebuffer, MMIO). */
#define IDENTITY_TABLES  64   /* 64 * 4MB = 256MB */

static uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));
uint32_t kernel_page_tables[IDENTITY_TABLES][1024] __attribute__((aligned(4096)));

/* Check if a PT physical address points into the shared kernel_page_tables.
 * Used to implement copy-on-write: if a user PD still references a kernel PT,
 * we must allocate a private copy before modifying it. */
static int is_kernel_pt(uint32_t pt_phys) {
    uint32_t start = (uint32_t)&kernel_page_tables[0];
    return (pt_phys >= start && pt_phys < start + sizeof(kernel_page_tables));
}

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

    DBG("[VMM] Paging enabled (4KB: 0-256MB, 4MB PSE: 256MB-4GB). CR3=0x%x", cr3);
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    /* Check if this PDE uses a 4MB page — can't do 4KB mapping there */
    if ((kernel_page_directory[pde_idx] & PTE_PRESENT) &&
        (kernel_page_directory[pde_idx] & PTE_4MB)) {
        DBG("[VMM] WARN: PDE %u is a 4MB page, can't map 4KB (virt 0x%x)", pde_idx, virt);
        return;
    }

    /* Get or verify page table exists */
    if (!(kernel_page_directory[pde_idx] & PTE_PRESENT)) {
        DBG("[VMM] WARN: No page table for PDE %u (virt 0x%x)", pde_idx, virt);
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
        /* COW: if this PDE still points to a shared kernel page table,
         * allocate a private copy before modifying it */
        if (is_kernel_pt(pt_phys)) {
            uint32_t new_pt = pmm_alloc_frame();
            if (!new_pt) return 0;
            memcpy((void *)new_pt, (void *)pt_phys, 4096);
            pd[pde_idx] = new_pt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
            pt_phys = new_pt;
        }
    }

    uint32_t *pt = (uint32_t *)pt_phys;
    pt[pte_idx] = (phys & PAGE_MASK) | (flags & 0xFFF);

    return pt_phys;
}

void vmm_destroy_user_pagedir(uint32_t pd_phys) {
    uint32_t *pd = (uint32_t *)pd_phys;
    for (int i = 0; i < 1024; i++) {
        if (!(pd[i] & PTE_PRESENT) || (pd[i] & PTE_4MB))
            continue;
        uint32_t pt = pd[i] & PAGE_MASK;
        if (!is_kernel_pt(pt))
            pmm_free_frame(pt);
    }
    pmm_free_frame(pd_phys);
}

int vmm_set_guard_page(uint32_t virt) {
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    if (!(kernel_page_directory[pde_idx] & PTE_PRESENT))
        return 0;
    if (kernel_page_directory[pde_idx] & PTE_4MB)
        return 0;  /* Can't guard within a 4MB page */

    uint32_t *pt = (uint32_t *)(kernel_page_directory[pde_idx] & PAGE_MASK);
    /* Remove PRESENT and set GUARD bit — access will trigger page fault */
    pt[pte_idx] = (pt[pte_idx] & ~PTE_PRESENT) | PTE_GUARD;
    vmm_invlpg(virt);
    return 1;
}

int vmm_check_guard_page(uint32_t virt) {
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    if (!(kernel_page_directory[pde_idx] & PTE_PRESENT))
        return 0;
    if (kernel_page_directory[pde_idx] & PTE_4MB)
        return 0;

    uint32_t *pt = (uint32_t *)(kernel_page_directory[pde_idx] & PAGE_MASK);
    if (pt[pte_idx] & PTE_GUARD) {
        /* One-shot: remove guard, restore present */
        pt[pte_idx] = (pt[pte_idx] & ~PTE_GUARD) | PTE_PRESENT;
        vmm_invlpg(virt);
        return 1;
    }
    return 0;
}

/* ── New helpers for demand paging / COW ─────────────────────── */

uint32_t vmm_ensure_pt(uint32_t pd_phys, uint32_t virt) {
    uint32_t *pd = (uint32_t *)pd_phys;
    uint32_t pde_idx = virt >> 22;

    if (!(pd[pde_idx] & PTE_PRESENT) || (pd[pde_idx] & PTE_4MB)) {
        /* No page table — allocate one */
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) return 0;
        memset((void *)pt_phys, 0, 4096);
        pd[pde_idx] = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        return pt_phys;
    }

    uint32_t pt_phys = pd[pde_idx] & PAGE_MASK;
    /* COW: if still pointing to a kernel page table, copy it */
    if (is_kernel_pt(pt_phys)) {
        uint32_t new_pt = pmm_alloc_frame();
        if (!new_pt) return 0;
        memcpy((void *)new_pt, (void *)pt_phys, 4096);
        pd[pde_idx] = new_pt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        return new_pt;
    }

    return pt_phys;
}

uint32_t vmm_get_pte(uint32_t pd_phys, uint32_t virt) {
    uint32_t *pd = (uint32_t *)pd_phys;
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    if (!(pd[pde_idx] & PTE_PRESENT) || (pd[pde_idx] & PTE_4MB))
        return 0;

    uint32_t *pt = (uint32_t *)(pd[pde_idx] & PAGE_MASK);
    return pt[pte_idx];
}

void vmm_unmap_user_page(uint32_t pd_phys, uint32_t virt) {
    uint32_t *pd = (uint32_t *)pd_phys;
    uint32_t pde_idx = virt >> 22;
    uint32_t pte_idx = (virt >> 12) & 0x3FF;

    if (!(pd[pde_idx] & PTE_PRESENT) || (pd[pde_idx] & PTE_4MB))
        return;

    uint32_t pt_phys = pd[pde_idx] & PAGE_MASK;
    /* Don't modify kernel page tables */
    if (is_kernel_pt(pt_phys))
        return;

    uint32_t *pt = (uint32_t *)pt_phys;
    pt[pte_idx] = 0;
    vmm_invlpg(virt);
}

void vmm_flush_tlb(void) {
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}
