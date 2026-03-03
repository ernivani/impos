#ifndef _KERNEL_BOOT_INFO_H
#define _KERNEL_BOOT_INFO_H

#include <stdint.h>

/*
 * Architecture-independent boot information.
 *
 * Populated from:
 *   i386:    multiboot_info_t (GRUB)
 *   aarch64: device tree blob (QEMU -kernel)
 *
 * All consumers (gfx, pmm, vmm, kernel_main) should eventually
 * read from this struct instead of multiboot_info_t directly.
 * Migration is incremental — existing code keeps working.
 */

/* Memory map entry (unified format) */
#define BOOT_MMAP_MAX 32

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;      /* 1 = available, 2 = reserved, 3 = ACPI reclaimable */
} boot_mmap_entry_t;

typedef struct {
    /* Memory */
    uint64_t mem_total;             /* Total RAM in bytes */
    boot_mmap_entry_t mmap[BOOT_MMAP_MAX];
    uint32_t mmap_count;

    /* Framebuffer (0 = not available) */
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;

    /* Initrd / boot modules */
    uintptr_t initrd_start;
    uint32_t  initrd_size;

    /* Command line */
    const char *cmdline;

    /* Architecture-specific opaque pointer.
       i386: multiboot_info_t*, aarch64: DTB pointer */
    void *arch_data;
} boot_info_t;

/* Global boot info, populated early in kernel_main */
extern boot_info_t g_boot_info;

#endif /* _KERNEL_BOOT_INFO_H */
