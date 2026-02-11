#include <kernel/shm.h>
#include <kernel/task.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/io.h>
#include <string.h>

static shm_region_t regions[SHM_MAX_REGIONS];

int shm_create(const char *name, uint32_t size) {
    if (!name || size == 0 || size > SHM_MAX_SIZE) return -1;

    uint32_t flags = irq_save();

    /* Check for duplicate name */
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (regions[i].active && strcmp(regions[i].name, name) == 0) {
            irq_restore(flags);
            return i;  /* already exists, return existing id */
        }
    }

    /* Find free slot */
    int id = -1;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (!regions[i].active) {
            id = i;
            break;
        }
    }
    if (id < 0) {
        irq_restore(flags);
        return -1;
    }

    int num_pages = (size + 4095) / 4096;

    /* Allocate physical pages */
    for (int i = 0; i < num_pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame) {
            /* Free already allocated pages */
            for (int j = 0; j < i; j++)
                pmm_free_frame(regions[id].phys_pages[j]);
            irq_restore(flags);
            return -1;
        }
        memset((void *)frame, 0, 4096);
        regions[id].phys_pages[i] = frame;
    }

    regions[id].active = 1;
    strncpy(regions[id].name, name, SHM_NAME_LEN - 1);
    regions[id].name[SHM_NAME_LEN - 1] = '\0';
    regions[id].num_pages = num_pages;
    regions[id].ref_count = 0;

    irq_restore(flags);
    return id;
}

int shm_find_by_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (regions[i].active && strcmp(regions[i].name, name) == 0)
            return i;
    }
    return -1;
}

uint32_t shm_attach(int region_id, int tid) {
    if (region_id < 0 || region_id >= SHM_MAX_REGIONS) return 0;
    if (!regions[region_id].active) return 0;

    task_info_t *t = task_get(tid);
    if (!t) return 0;

    /* Already attached? Return the address */
    if (t->shm_attached & (1 << region_id)) {
        return SHM_BASE + (uint32_t)region_id * SHM_MAX_SIZE;
    }

    /* Need a per-process page directory to map into */
    uint32_t pd = t->page_dir;
    if (!pd || pd == vmm_get_kernel_pagedir()) {
        /* Ring 0 tasks without per-process PD: return physical address directly */
        return regions[region_id].phys_pages[0];
    }

    uint32_t flags = irq_save();

    /* Map each physical page into the task's page directory */
    uint32_t base_va = SHM_BASE + (uint32_t)region_id * SHM_MAX_SIZE;
    for (int i = 0; i < regions[region_id].num_pages; i++) {
        uint32_t va = base_va + (uint32_t)i * 4096;
        vmm_map_user_page(pd, va, regions[region_id].phys_pages[i],
                          PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    t->shm_attached |= (1 << region_id);
    regions[region_id].ref_count++;

    irq_restore(flags);
    return base_va;
}

int shm_detach(int region_id, int tid) {
    if (region_id < 0 || region_id >= SHM_MAX_REGIONS) return -1;
    if (!regions[region_id].active) return -1;

    task_info_t *t = task_get_raw(tid);
    if (!t) return -1;
    if (!(t->shm_attached & (1 << region_id))) return -1;

    uint32_t flags = irq_save();

    /* Unmap from per-process page directory */
    uint32_t pd = t->page_dir;
    if (pd && pd != vmm_get_kernel_pagedir()) {
        uint32_t *pd_ptr = (uint32_t *)pd;
        uint32_t base_va = SHM_BASE + (uint32_t)region_id * SHM_MAX_SIZE;
        for (int i = 0; i < regions[region_id].num_pages; i++) {
            uint32_t va = base_va + (uint32_t)i * 4096;
            uint32_t pde_idx = va >> 22;
            uint32_t pte_idx = (va >> 12) & 0x3FF;
            if (pd_ptr[pde_idx] & PTE_PRESENT) {
                uint32_t *pt = (uint32_t *)(pd_ptr[pde_idx] & PAGE_MASK);
                pt[pte_idx] = 0;
            }
        }
    }

    t->shm_attached &= ~(1 << region_id);
    regions[region_id].ref_count--;

    /* If no one is attached, free the region */
    if (regions[region_id].ref_count <= 0) {
        for (int i = 0; i < regions[region_id].num_pages; i++) {
            if (regions[region_id].phys_pages[i]) {
                pmm_free_frame(regions[region_id].phys_pages[i]);
                regions[region_id].phys_pages[i] = 0;
            }
        }
        regions[region_id].active = 0;
    }

    irq_restore(flags);
    return 0;
}

void shm_cleanup_task(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t) return;

    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (t->shm_attached & (1 << i))
            shm_detach(i, tid);
    }
}

shm_region_t *shm_get_regions(void) {
    return regions;
}
