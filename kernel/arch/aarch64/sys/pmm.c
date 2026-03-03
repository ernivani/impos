/*
 * aarch64 Physical Memory Manager — Phase 2.
 *
 * Same bitmap allocator as i386, but memory map comes from hardcoded
 * QEMU virt addresses (Phase 8 adds DTB parsing).
 *
 * QEMU virt RAM starts at 0x40000000. We use a bitmap where each bit
 * represents one 4KB frame.
 */

#include <kernel/pmm.h>
#include <kernel/io.h>
#include <stdint.h>
#include <string.h>

#define PAGE_SIZE       4096
#define MAX_FRAMES      (256 * 1024)   /* 256K frames = 1GB addressable */
#define BITMAP_SIZE     (MAX_FRAMES / 8)

/* Bitmap: 1 = used, 0 = free */
static uint8_t frame_bitmap[BITMAP_SIZE];
static uint32_t total_frames;
static uint32_t used_frames;

/* Defined by linker script */
extern char _heap_start[];
extern char __bss_end[];

/* ── Bitmap helpers ──────────────────────────────────────────────── */

static inline void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}

static inline void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static inline int bitmap_test(uint32_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

/* ── Physical address ↔ frame index ─────────────────────────────── */

/* QEMU virt RAM base */
#define RAM_BASE 0x40000000UL

static inline uint32_t addr_to_frame(uint32_t addr) {
    return (addr - RAM_BASE) / PAGE_SIZE;
}

static inline uint32_t frame_to_addr(uint32_t frame) {
    return RAM_BASE + frame * PAGE_SIZE;
}

/* ═══════════════════════════════════════════════════════════════════
 * pmm_init — initialize from QEMU virt memory map
 *
 * On aarch64, we ignore the multiboot_info_t parameter (it's NULL).
 * Instead we use hardcoded QEMU virt values:
 *   RAM: 0x40000000 to 0x40000000 + mem_size
 *   Kernel: loaded at 0x40000000 to _heap_start
 * ═══════════════════════════════════════════════════════════════════ */

void pmm_init(multiboot_info_t *mbi) {
    (void)mbi;

    /* QEMU virt with -m 8G gives us 8GB starting at 0x40000000.
     * But we only track up to MAX_FRAMES * 4KB = 1GB for now.
     * This is plenty for Phase 2-3 (tasks, basic page tables). */
    uint32_t mem_size = MAX_FRAMES * PAGE_SIZE;  /* 1 GB */
    total_frames = MAX_FRAMES;
    used_frames = 0;

    /* Mark all frames as free */
    memset(frame_bitmap, 0, BITMAP_SIZE);

    /* Reserve frame 0 (never allocate the RAM base page) */
    bitmap_set(0);
    used_frames++;

    /* Reserve kernel image: 0x40000000 to _heap_start */
    uint32_t kernel_end = (uint32_t)(uintptr_t)_heap_start;
    uint32_t kernel_start = RAM_BASE;
    uint32_t kernel_frames = (kernel_end - kernel_start + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < kernel_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
        }
    }

    DBG("PMM: %u frames total, %u used by kernel, %u free (%u MB free)",
        total_frames, used_frames, total_frames - used_frames,
        (total_frames - used_frames) * 4 / 1024);

    (void)mem_size;
}

/* ═══════════════════════════════════════════════════════════════════
 * Allocate/Free
 * ═══════════════════════════════════════════════════════════════════ */

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
            return frame_to_addr(i);
        }
    }
    DBG("PMM: OUT OF MEMORY!");
    return 0;
}

uint32_t pmm_alloc_contiguous(uint32_t n_frames) {
    if (n_frames == 0) return 0;
    if (n_frames == 1) return pmm_alloc_frame();

    for (uint32_t i = 0; i <= total_frames - n_frames; i++) {
        uint32_t j;
        for (j = 0; j < n_frames; j++) {
            if (bitmap_test(i + j))
                break;
        }
        if (j == n_frames) {
            /* Found contiguous run */
            for (j = 0; j < n_frames; j++) {
                bitmap_set(i + j);
                used_frames++;
            }
            return frame_to_addr(i);
        }
    }
    return 0;
}

void pmm_free_frame(uint32_t phys_addr) {
    if (phys_addr < RAM_BASE) return;
    uint32_t frame = addr_to_frame(phys_addr);
    if (frame >= total_frames) return;
    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        used_frames--;
    }
}

void pmm_free_contiguous(uint32_t phys_addr, uint32_t n_frames) {
    for (uint32_t i = 0; i < n_frames; i++) {
        pmm_free_frame(phys_addr + i * PAGE_SIZE);
    }
}

void pmm_reserve_range(uint32_t phys_start, uint32_t phys_end) {
    if (phys_start < RAM_BASE) phys_start = RAM_BASE;
    uint32_t start_frame = addr_to_frame(phys_start);
    uint32_t end_frame = (phys_end - RAM_BASE + PAGE_SIZE - 1) / PAGE_SIZE;
    if (end_frame > total_frames) end_frame = total_frames;

    for (uint32_t i = start_frame; i < end_frame; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
        }
    }
}

uint32_t pmm_free_frame_count(void) {
    return total_frames - used_frames;
}
