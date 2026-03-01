/*
 * frame_ref.c — Physical frame reference counting for COW fork
 *
 * One byte per frame (65536 frames = 256MB / 4KB). Saturates at 255 to
 * prevent overflow — a saturated frame is never freed (minor leak is safer
 * than use-after-free).
 */

#include <kernel/frame_ref.h>
#include <kernel/vmm.h>
#include <string.h>

#define PMM_MAX_FRAMES  65536

static uint8_t refcounts[PMM_MAX_FRAMES];

void frame_ref_init(void) {
    /* All frames start at refcount 0. Frames allocated by PMM get set to 1
     * via frame_ref_set1() called from pmm_alloc_frame(). */
    memset(refcounts, 0, sizeof(refcounts));
}

static inline uint32_t frame_idx(uint32_t phys) {
    return phys / PAGE_SIZE;
}

void frame_ref_inc(uint32_t phys) {
    uint32_t idx = frame_idx(phys);
    if (idx >= PMM_MAX_FRAMES) return;
    if (refcounts[idx] < 255)
        refcounts[idx]++;
}

int frame_ref_dec(uint32_t phys) {
    uint32_t idx = frame_idx(phys);
    if (idx >= PMM_MAX_FRAMES) return 0;
    if (refcounts[idx] == 255) return 255;  /* saturated: don't decrement */
    if (refcounts[idx] > 0)
        refcounts[idx]--;
    return refcounts[idx];
}

int frame_ref_get(uint32_t phys) {
    uint32_t idx = frame_idx(phys);
    if (idx >= PMM_MAX_FRAMES) return 0;
    return refcounts[idx];
}

void frame_ref_set1(uint32_t phys) {
    uint32_t idx = frame_idx(phys);
    if (idx >= PMM_MAX_FRAMES) return;
    refcounts[idx] = 1;
}
