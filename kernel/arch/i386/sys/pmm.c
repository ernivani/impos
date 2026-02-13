#include <kernel/pmm.h>
#include <kernel/multiboot.h>
#include <string.h>
#include <stdio.h>

/* Bitmap: 1 bit per 4KB frame. 65536 frames = 256MB max. */
#define PMM_MAX_FRAMES  65536
#define PMM_BITMAP_SIZE (PMM_MAX_FRAMES / 32)  /* 2048 uint32_t = 8KB */
#define FRAME_SIZE      4096

static uint32_t bitmap[PMM_BITMAP_SIZE];
static uint32_t total_frames;

/* Linker symbols */
extern char _heap_start[];

static inline void frame_set(uint32_t frame) {
    bitmap[frame / 32] |= (1U << (frame % 32));
}

static inline void frame_clear(uint32_t frame) {
    bitmap[frame / 32] &= ~(1U << (frame % 32));
}

static inline int frame_test(uint32_t frame) {
    return (bitmap[frame / 32] >> (frame % 32)) & 1;
}

void pmm_init(multiboot_info_t *mbi) {
    /* Start with all frames marked as used */
    memset(bitmap, 0xFF, sizeof(bitmap));
    total_frames = PMM_MAX_FRAMES;

    /* Parse multiboot memory map to find available regions */
    if (!(mbi->flags & (1 << 6))) {
        printf("[PMM] No memory map from bootloader!\n");
        return;
    }

    uint32_t mmap_addr = mbi->mmap_addr;
    uint32_t mmap_end  = mmap_addr + mbi->mmap_length;

    while (mmap_addr < mmap_end) {
        multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)mmap_addr;

        /* type 1 = available RAM */
        if (entry->type == 1) {
            uint64_t base = entry->addr;
            uint64_t len  = entry->len;
            uint64_t end  = base + len;

            /* Clamp to our 256MB limit */
            if (base >= (uint64_t)PMM_MAX_FRAMES * FRAME_SIZE)
                goto next;
            if (end > (uint64_t)PMM_MAX_FRAMES * FRAME_SIZE)
                end = (uint64_t)PMM_MAX_FRAMES * FRAME_SIZE;

            /* Align base up, end down to frame boundaries */
            uint32_t frame_start = (uint32_t)((base + FRAME_SIZE - 1) / FRAME_SIZE);
            uint32_t frame_end   = (uint32_t)(end / FRAME_SIZE);

            for (uint32_t f = frame_start; f < frame_end; f++)
                frame_clear(f);
        }

next:
        mmap_addr += entry->size + sizeof(entry->size);
    }

    /* Re-reserve the first 1MB (BIOS, VGA, bootloader data) = 256 frames */
    for (uint32_t f = 0; f < 256; f++)
        frame_set(f);

    /* Re-reserve kernel image + heap area.
     * Kernel starts at 1MB (frame 256). _heap_start marks end of BSS.
     * Reserve up to _heap_start + 16MB for heap growth room. */
    uint32_t kernel_end_addr = (uint32_t)_heap_start + (16 * 1024 * 1024);
    uint32_t kernel_end_frame = kernel_end_addr / FRAME_SIZE;
    if (kernel_end_frame > PMM_MAX_FRAMES)
        kernel_end_frame = PMM_MAX_FRAMES;
    for (uint32_t f = 256; f < kernel_end_frame; f++)
        frame_set(f);

    printf("[PMM] Initialized: %u free frames (%u MB free)\n",
           pmm_free_frame_count(),
           pmm_free_frame_count() * 4 / 1024);
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        if (bitmap[i] == 0xFFFFFFFF)
            continue;  /* All 32 frames used */
        for (uint32_t bit = 0; bit < 32; bit++) {
            if (!(bitmap[i] & (1U << bit))) {
                uint32_t frame = i * 32 + bit;
                if (frame >= total_frames)
                    return 0;
                frame_set(frame);
                return frame * FRAME_SIZE;
            }
        }
    }
    return 0;  /* Out of memory */
}

void pmm_free_frame(uint32_t phys_addr) {
    uint32_t frame = phys_addr / FRAME_SIZE;
    if (frame < total_frames)
        frame_clear(frame);
}

void pmm_reserve_range(uint32_t phys_start, uint32_t phys_end) {
    uint32_t frame_start = phys_start / FRAME_SIZE;
    uint32_t frame_end = (phys_end + FRAME_SIZE - 1) / FRAME_SIZE;
    if (frame_end > PMM_MAX_FRAMES) frame_end = PMM_MAX_FRAMES;
    for (uint32_t f = frame_start; f < frame_end; f++)
        frame_set(f);
}

uint32_t pmm_free_frame_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        if (bitmap[i] == 0)
            count += 32;
        else if (bitmap[i] != 0xFFFFFFFF) {
            uint32_t val = bitmap[i];
            /* Count zero bits */
            val = ~val;
            while (val) {
                count++;
                val &= val - 1;
            }
        }
    }
    return count;
}
