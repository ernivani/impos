/*
 * aarch64 Virtual Memory Manager — Phase 2.
 *
 * ARM64 paging with 4KB granule, 3-level tables for 39-bit VA (512 GB):
 *   L1 (PGD): 512 entries × 1GB each
 *   L2 (PMD): 512 entries × 2MB each
 *   L3 (PTE): 512 entries × 4KB each
 *
 * Phase 2 goal: identity-map first 1GB (RAM + devices) and enable MMU.
 * Uses 2MB block descriptors at L2 for simplicity (no L3 needed for
 * identity mapping).
 *
 * Memory attribute setup (MAIR_EL1):
 *   Index 0: Device-nGnRnE (for MMIO: UART, GIC, etc.)
 *   Index 1: Normal Write-Back (for RAM)
 */

#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/io.h>
#include <stdint.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════
 * ARM64 Page Table Descriptor Bits
 * ═══════════════════════════════════════════════════════════════════ */

/* Descriptor types */
#define DESC_INVALID    0x0
#define DESC_BLOCK      0x1    /* L1/L2 block (maps 1GB/2MB) */
#define DESC_TABLE      0x3    /* Points to next-level table */
#define DESC_PAGE       0x3    /* L3 page (maps 4KB) */

/* Lower attributes (for block/page descriptors) */
#define ATTR_IDX(n)     ((uint64_t)(n) << 2)  /* MAIR index */
#define ATTR_NS         (1ULL << 5)            /* Non-secure */
#define ATTR_AP_RW_EL1  (0ULL << 6)            /* EL1 R/W, EL0 none */
#define ATTR_AP_RW_ALL  (1ULL << 6)            /* EL1 R/W, EL0 R/W */
#define ATTR_AP_RO_EL1  (2ULL << 6)            /* EL1 R/O, EL0 none */
#define ATTR_AP_RO_ALL  (3ULL << 6)            /* EL1 R/O, EL0 R/O */
#define ATTR_SH_ISH     (3ULL << 8)            /* Inner Shareable */
#define ATTR_AF         (1ULL << 10)           /* Access Flag */
#define ATTR_nG         (1ULL << 11)           /* Not Global */

/* Upper attributes */
#define ATTR_PXN        (1ULL << 53)           /* Privileged Execute Never */
#define ATTR_UXN        (1ULL << 54)           /* Unprivileged Execute Never */

/* MAIR register values */
#define MAIR_DEVICE_nGnRnE  0x00   /* Index 0 */
#define MAIR_NORMAL_WB      0xFF   /* Index 1: Write-Back, R+W Allocate */

/* Convenience for identity mapping */
#define BLOCK_DEVICE  (DESC_BLOCK | ATTR_IDX(0) | ATTR_AF | ATTR_SH_ISH | \
                       ATTR_AP_RW_EL1 | ATTR_UXN | ATTR_PXN)
#define BLOCK_NORMAL  (DESC_BLOCK | ATTR_IDX(1) | ATTR_AF | ATTR_SH_ISH | \
                       ATTR_AP_RW_EL1 | ATTR_UXN)

/* ═══════════════════════════════════════════════════════════════════
 * Page Tables (statically allocated, 4KB aligned)
 * ═══════════════════════════════════════════════════════════════════ */

/* L1 table: 512 entries covering 512 GB */
static uint64_t l1_table[512] __attribute__((aligned(4096)));

/* L2 tables: we need 2 — one for 0x00000000-0x3FFFFFFF (devices)
 * and one for 0x40000000-0x7FFFFFFF (RAM) */
static uint64_t l2_table_dev[512] __attribute__((aligned(4096)));
static uint64_t l2_table_ram[512] __attribute__((aligned(4096)));

/* Kernel page directory physical address (for compatibility with i386 API) */
static uint32_t kernel_pagedir;

/* ═══════════════════════════════════════════════════════════════════
 * vmm_init — build page tables and enable MMU
 * ═══════════════════════════════════════════════════════════════════ */

void vmm_init(multiboot_info_t *mbi) {
    (void)mbi;

    memset(l1_table, 0, sizeof(l1_table));
    memset(l2_table_dev, 0, sizeof(l2_table_dev));
    memset(l2_table_ram, 0, sizeof(l2_table_ram));

    /*
     * L2 table for 0x00000000-0x3FFFFFFF (device region):
     *   0x00000000-0x0FFFFFFF: unmapped (nothing here on QEMU virt)
     *   But we need 0x08000000 (GIC) and 0x09000000 (UART) mapped.
     *   Map first 512 MB as device memory in 2MB blocks for simplicity.
     */
    for (int i = 0; i < 512; i++) {
        uint64_t addr = (uint64_t)i * 0x200000;  /* 2MB blocks */
        l2_table_dev[i] = addr | BLOCK_DEVICE;
    }

    /*
     * L2 table for 0x40000000-0x7FFFFFFF (RAM, 1GB):
     *   Map all as Normal Write-Back memory.
     */
    for (int i = 0; i < 512; i++) {
        uint64_t addr = 0x40000000ULL + (uint64_t)i * 0x200000;
        l2_table_ram[i] = addr | BLOCK_NORMAL;
    }

    /* L1 table: point entry 0 → l2_table_dev, entry 1 → l2_table_ram */
    l1_table[0] = (uint64_t)(uintptr_t)l2_table_dev | DESC_TABLE;
    l1_table[1] = (uint64_t)(uintptr_t)l2_table_ram | DESC_TABLE;

    /* Save kernel pagedir address */
    kernel_pagedir = (uint32_t)(uintptr_t)l1_table;

    /* ── Configure system registers ────────────────────────────── */

    /* MAIR_EL1: attribute 0 = device, attribute 1 = normal WB */
    uint64_t mair = ((uint64_t)MAIR_NORMAL_WB << 8) | MAIR_DEVICE_nGnRnE;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));

    /* TCR_EL1: configure translation
     *   T0SZ = 25 → 39-bit VA space (512 GB)
     *   TG0  = 0b00 → 4KB granule
     *   SH0  = 0b11 → Inner Shareable
     *   ORGN0 = 0b01 → Write-Back, Write-Allocate
     *   IRGN0 = 0b01 → Write-Back, Write-Allocate
     */
    uint64_t tcr = (25ULL << 0)   /* T0SZ = 25 (39-bit VA) */
                 | (0ULL << 14)   /* TG0 = 4KB */
                 | (3ULL << 12)   /* SH0 = Inner Shareable */
                 | (1ULL << 10)   /* ORGN0 = WB WA */
                 | (1ULL << 8);   /* IRGN0 = WB WA */
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));

    /* TTBR0_EL1: point to L1 table */
    uint64_t ttbr0 = (uint64_t)(uintptr_t)l1_table;
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0));

    /* Ensure all writes are visible before enabling MMU */
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* SCTLR_EL1: enable MMU + data cache + instruction cache */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1 << 0);    /* M: MMU enable */
    sctlr |= (1 << 2);    /* C: Data cache enable */
    sctlr |= (1 << 12);   /* I: Instruction cache enable */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb" ::: "memory");

    DBG("MMU enabled: L1=0x%x, VA=39-bit, 4KB granule",
        (unsigned)(uintptr_t)l1_table);
    DBG("  [0x00000000-0x3FFFFFFF] Device memory (GIC, UART)");
    DBG("  [0x40000000-0x7FFFFFFF] Normal memory (1GB RAM)");
}

/* ═══════════════════════════════════════════════════════════════════
 * API stubs — full implementation comes in Phase 6
 * ═══════════════════════════════════════════════════════════════════ */

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    /* Phase 6: 4KB page mapping via L3 tables */
    (void)virt; (void)phys; (void)flags;
}

void vmm_unmap_page(uint32_t virt) {
    (void)virt;
}

void vmm_invlpg(uint32_t virt) {
    /* TLBI by VA, Inner Shareable */
    uint64_t va = ((uint64_t)virt >> 12) & 0xFFFFFFFFF;
    __asm__ volatile("tlbi vae1is, %0" :: "r"(va));
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

uint32_t vmm_get_kernel_pagedir(void) {
    return kernel_pagedir;
}

uint32_t vmm_create_user_pagedir(void) {
    /* Phase 6: per-process page directory */
    return 0;
}

uint32_t vmm_map_user_page(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags) {
    (void)pd_phys; (void)virt; (void)phys; (void)flags;
    return 0;
}

void vmm_destroy_user_pagedir(uint32_t pd_phys) {
    (void)pd_phys;
}

int vmm_set_guard_page(uint32_t virt) {
    (void)virt;
    return 0;
}

int vmm_check_guard_page(uint32_t virt) {
    (void)virt;
    return 0;
}

uint32_t vmm_ensure_pt(uint32_t pd_phys, uint32_t virt) {
    (void)pd_phys; (void)virt;
    return 0;
}

uint32_t vmm_get_pte(uint32_t pd_phys, uint32_t virt) {
    (void)pd_phys; (void)virt;
    return 0;
}

void vmm_unmap_user_page(uint32_t pd_phys, uint32_t virt) {
    (void)pd_phys; (void)virt;
}

void vmm_flush_tlb(void) {
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}
