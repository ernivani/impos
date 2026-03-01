#ifndef _KERNEL_VMA_H
#define _KERNEL_VMA_H

#include <stdint.h>

#define VMA_MAX_PER_TASK  64

/* VMA protection flags */
#define VMA_READ      0x01
#define VMA_WRITE     0x02
#define VMA_EXEC      0x04
#define VMA_SHARED    0x08
#define VMA_ANON      0x10
#define VMA_GROWSDOWN 0x20

/* VMA types */
#define VMA_TYPE_NONE  0
#define VMA_TYPE_ANON  1   /* Anonymous mmap */
#define VMA_TYPE_ELF   2   /* ELF PT_LOAD segment */
#define VMA_TYPE_STACK 3   /* User stack */
#define VMA_TYPE_BRK   4   /* Heap (brk) */

typedef struct {
    uint32_t vm_start;     /* Page-aligned start address (inclusive) */
    uint32_t vm_end;       /* Page-aligned end address (exclusive) */
    uint32_t vm_flags;     /* VMA_READ | VMA_WRITE | VMA_EXEC | ... */
    uint8_t  vm_type;      /* VMA_TYPE_* */
    int      active;       /* 1 if this slot is in use */
} vma_t;

typedef struct {
    vma_t    vmas[VMA_MAX_PER_TASK];
    int      count;          /* Number of active VMAs */
    uint32_t mmap_next;      /* Next free VA for mmap allocation */
    uint32_t brk_start;      /* Initial program break */
    uint32_t brk_current;    /* Current program break */
    uint32_t total_mapped;   /* Total mapped bytes (informational) */
} vma_table_t;

/* Initialize a new VMA table. Returns heap-allocated table, or NULL on failure. */
vma_table_t *vma_init(void);

/* Find the VMA containing addr. Returns NULL if no VMA covers that address. */
vma_t *vma_find(vma_table_t *vt, uint32_t addr);

/* Insert a new VMA. Returns 0 on success, -1 if table is full. */
int vma_insert(vma_table_t *vt, uint32_t start, uint32_t end,
               uint32_t flags, uint8_t type);

/* Remove all VMA coverage in [start, end). May split or shrink existing VMAs.
 * Returns the number of pages removed, or -1 on error. */
int vma_remove(vma_table_t *vt, uint32_t start, uint32_t end);

/* Split a VMA at addr. The VMA containing addr is split into [vm_start, addr)
 * and [addr, vm_end). Returns 0 on success, -1 on error. */
int vma_split(vma_table_t *vt, uint32_t addr);

/* Find a free gap of at least 'len' bytes starting from mmap_next upward.
 * Returns the start address, or 0 on failure. */
uint32_t vma_find_free(vma_table_t *vt, uint32_t len);

/* Deep-copy a VMA table (for fork). Returns new table, or NULL on failure. */
vma_table_t *vma_clone(vma_table_t *src);

/* Free a VMA table and all its resources. */
void vma_destroy(vma_table_t *vt);

/* Get the VMA type name (for /proc/PID/maps). */
const char *vma_type_name(uint8_t type);

#endif
