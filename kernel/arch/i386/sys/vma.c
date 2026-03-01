/*
 * vma.c — Virtual Memory Area tracking for per-process address spaces
 *
 * Each process has a vma_table_t with up to 64 VMAs describing its mapped
 * regions (ELF segments, stack, heap, anonymous mmap). This replaces the
 * fixed elf_frames[64] array with proper range tracking.
 *
 * All operations are O(n) linear scans on a 64-entry array — fast enough
 * for our task count and VMA density.
 */

#include <kernel/vma.h>
#include <kernel/vmm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Allocation ────────────────────────────────────────────────── */

vma_table_t *vma_init(void) {
    vma_table_t *vt = (vma_table_t *)malloc(sizeof(vma_table_t));
    if (!vt) return NULL;
    memset(vt, 0, sizeof(vma_table_t));
    vt->mmap_next = 0x20000000;  /* Default: 512MB, above typical ELF loads */
    return vt;
}

void vma_destroy(vma_table_t *vt) {
    if (vt) free(vt);
}

vma_table_t *vma_clone(vma_table_t *src) {
    if (!src) return NULL;
    vma_table_t *dst = (vma_table_t *)malloc(sizeof(vma_table_t));
    if (!dst) return NULL;
    memcpy(dst, src, sizeof(vma_table_t));
    return dst;
}

/* ── Lookup ────────────────────────────────────────────────────── */

vma_t *vma_find(vma_table_t *vt, uint32_t addr) {
    if (!vt) return NULL;
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (vt->vmas[i].active &&
            addr >= vt->vmas[i].vm_start &&
            addr < vt->vmas[i].vm_end) {
            return &vt->vmas[i];
        }
    }
    return NULL;
}

/* ── Insert ────────────────────────────────────────────────────── */

static vma_t *find_free_slot(vma_table_t *vt) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (!vt->vmas[i].active)
            return &vt->vmas[i];
    }
    return NULL;
}

int vma_insert(vma_table_t *vt, uint32_t start, uint32_t end,
               uint32_t flags, uint8_t type) {
    if (!vt || start >= end) return -1;

    /* Page-align */
    start &= PAGE_MASK;
    end = (end + PAGE_SIZE - 1) & PAGE_MASK;

    vma_t *slot = find_free_slot(vt);
    if (!slot) return -1;

    slot->vm_start = start;
    slot->vm_end = end;
    slot->vm_flags = flags;
    slot->vm_type = type;
    slot->active = 1;
    vt->count++;
    vt->total_mapped += (end - start);

    return 0;
}

/* ── Remove ────────────────────────────────────────────────────── */

int vma_remove(vma_table_t *vt, uint32_t start, uint32_t end) {
    if (!vt || start >= end) return -1;

    start &= PAGE_MASK;
    end = (end + PAGE_SIZE - 1) & PAGE_MASK;

    int pages_removed = 0;

    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        vma_t *v = &vt->vmas[i];
        if (!v->active) continue;

        /* No overlap */
        if (v->vm_end <= start || v->vm_start >= end)
            continue;

        if (start <= v->vm_start && end >= v->vm_end) {
            /* Entire VMA is within removal range — delete it */
            pages_removed += (v->vm_end - v->vm_start) / PAGE_SIZE;
            vt->total_mapped -= (v->vm_end - v->vm_start);
            v->active = 0;
            vt->count--;
        } else if (start > v->vm_start && end < v->vm_end) {
            /* Removal punches a hole — split into two VMAs */
            uint32_t old_end = v->vm_end;
            uint32_t old_flags = v->vm_flags;
            uint8_t old_type = v->vm_type;

            /* Shrink existing VMA to [vm_start, start) */
            vt->total_mapped -= (v->vm_end - v->vm_start);
            v->vm_end = start;
            vt->total_mapped += (v->vm_end - v->vm_start);

            /* Create new VMA for [end, old_end) */
            vma_t *slot = find_free_slot(vt);
            if (slot) {
                slot->vm_start = end;
                slot->vm_end = old_end;
                slot->vm_flags = old_flags;
                slot->vm_type = old_type;
                slot->active = 1;
                vt->count++;
                vt->total_mapped += (old_end - end);
            }

            pages_removed += (end - start) / PAGE_SIZE;
        } else if (start <= v->vm_start) {
            /* Trim from the left: [vm_start, end) removed */
            pages_removed += (end - v->vm_start) / PAGE_SIZE;
            vt->total_mapped -= (end - v->vm_start);
            v->vm_start = end;
        } else {
            /* Trim from the right: [start, vm_end) removed */
            pages_removed += (v->vm_end - start) / PAGE_SIZE;
            vt->total_mapped -= (v->vm_end - start);
            v->vm_end = start;
        }
    }

    return pages_removed;
}

/* ── Split ─────────────────────────────────────────────────────── */

int vma_split(vma_table_t *vt, uint32_t addr) {
    if (!vt) return -1;

    addr &= PAGE_MASK;

    vma_t *v = vma_find(vt, addr);
    if (!v) return -1;

    /* Already at a boundary — nothing to split */
    if (addr == v->vm_start || addr == v->vm_end)
        return 0;

    /* Need a free slot for the upper half */
    vma_t *slot = find_free_slot(vt);
    if (!slot) return -1;

    /* Upper half: [addr, old_end) */
    slot->vm_start = addr;
    slot->vm_end = v->vm_end;
    slot->vm_flags = v->vm_flags;
    slot->vm_type = v->vm_type;
    slot->active = 1;
    vt->count++;

    /* Lower half: [vm_start, addr) */
    v->vm_end = addr;

    return 0;
}

/* ── Find free gap ─────────────────────────────────────────────── */

/* Check if [start, start+len) overlaps any active VMA */
static int range_overlaps(vma_table_t *vt, uint32_t start, uint32_t len) {
    uint32_t end = start + len;
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (!vt->vmas[i].active) continue;
        if (start < vt->vmas[i].vm_end && end > vt->vmas[i].vm_start)
            return 1;
    }
    return 0;
}

uint32_t vma_find_free(vma_table_t *vt, uint32_t len) {
    if (!vt || len == 0) return 0;

    len = (len + PAGE_SIZE - 1) & PAGE_MASK;

    /* Start scanning from mmap_next */
    uint32_t candidate = vt->mmap_next;

    /* Stay within the identity-mapped region (< 256MB = 0x10000000),
     * but also below the user stack at USER_SPACE_BASE (0x40000000) */
    uint32_t limit = 0x40000000;

    for (int attempts = 0; attempts < 1024; attempts++) {
        if (candidate + len > limit || candidate + len < candidate)
            return 0;  /* Overflow or past limit */

        if (!range_overlaps(vt, candidate, len)) {
            /* Found a gap — advance mmap_next past this allocation */
            vt->mmap_next = candidate + len;
            return candidate;
        }

        /* Advance past whatever VMA we hit */
        uint32_t best_end = candidate + PAGE_SIZE;
        for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
            if (!vt->vmas[i].active) continue;
            if (candidate >= vt->vmas[i].vm_start &&
                candidate < vt->vmas[i].vm_end) {
                if (vt->vmas[i].vm_end > best_end)
                    best_end = vt->vmas[i].vm_end;
            }
        }
        candidate = best_end;
    }

    return 0;  /* Exhausted attempts */
}

/* ── Utilities ─────────────────────────────────────────────────── */

const char *vma_type_name(uint8_t type) {
    switch (type) {
        case VMA_TYPE_ANON:  return "anon";
        case VMA_TYPE_ELF:   return "elf";
        case VMA_TYPE_STACK: return "stack";
        case VMA_TYPE_BRK:   return "brk";
        default:             return "???";
    }
}
