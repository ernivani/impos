#ifndef _KERNEL_SHM_H
#define _KERNEL_SHM_H

#include <stdint.h>

#define SHM_MAX_REGIONS 16
#define SHM_MAX_PAGES   16                     /* 16 pages = 64KB max per region */
#define SHM_MAX_SIZE    (SHM_MAX_PAGES * 4096) /* 64KB */
#define SHM_NAME_LEN    32
#define SHM_BASE        0x40010000             /* after user stack at 0x40000000 */

typedef struct {
    int      active;
    char     name[SHM_NAME_LEN];
    uint32_t phys_pages[SHM_MAX_PAGES]; /* physical frames */
    int      num_pages;
    int      ref_count;
} shm_region_t;

int      shm_create(const char *name, uint32_t size);
int      shm_find_by_name(const char *name);
uint32_t shm_attach(int region_id, int tid);
int      shm_detach(int region_id, int tid);
void     shm_cleanup_task(int tid);
shm_region_t *shm_get_regions(void);

#endif
