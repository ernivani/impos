#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/vmm.h>
#include <kernel/vma.h>
#include <kernel/frame_ref.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/pmm.h>
#include <kernel/pipe.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

extern volatile uint32_t pit_ticks;

static volatile int scheduler_active = 0;

/*
 * Cooperative tasks (slots 0-3) share the boot stack and use task_set_current()
 * cooperatively. We save/restore the boot stack context as a single entity.
 * Preemptive threads (slots 4+) each have their own stack.
 */
static uint32_t coop_esp = 0;       /* Saved boot stack ESP */
static int coop_task_id = TASK_KERNEL; /* Which cooperative task was current */
static uint32_t current_cr3 = 0;    /* Currently loaded page directory */

/* Per-priority round-robin tracking: last scheduled task index */
static int last_run[PRIO_LEVELS] = { 3, 3, 3, 3 };

/* Time slice values indexed by priority */
static const uint8_t prio_slices[PRIO_LEVELS] = {
    SLICE_IDLE, SLICE_BACKGROUND, SLICE_NORMAL, SLICE_REALTIME
};

static inline void sched_switch_cr3(uint32_t new_cr3) {
    if (new_cr3 && new_cr3 != current_cr3) {
        current_cr3 = new_cr3;
        __asm__ volatile ("mov %0, %%cr3" : : "r"(current_cr3) : "memory");
    }
}

int sched_is_active(void) {
    return scheduler_active;
}

void sched_init(void) {
    /* Mark the boot task (TASK_KERNEL) as the currently running task */
    task_info_t *boot = task_get(TASK_KERNEL);
    if (boot)
        boot->state = TASK_STATE_RUNNING;

    /* Mark other fixed tasks as READY */
    task_info_t *idle = task_get(TASK_IDLE);
    if (idle) idle->state = TASK_STATE_READY;

    task_info_t *wm = task_get(TASK_WM);
    if (wm) wm->state = TASK_STATE_READY;

    task_info_t *sh = task_get(TASK_SHELL);
    if (sh) sh->state = TASK_STATE_READY;

    coop_task_id = TASK_KERNEL;
    current_cr3 = vmm_get_kernel_pagedir();
    scheduler_active = 1;
}

registers_t* schedule(registers_t* regs) {
    if (!scheduler_active)
        return regs;

    int current = task_get_current();
    task_info_t *cur = task_get_raw(current);  /* use raw: task may be zombie/inactive */

    /* Determine if current task is a preemptive thread (has own stack) */
    int cur_is_preemptive = (cur && (cur->stack_base != NULL || cur->is_user));

    /* Clean up zombie threads (free their stacks, page dirs, and pipes safely).
     * Only reap if parent has collected via waitpid (slot cleared to UNUSED)
     * or parent doesn't exist / is also dead (backward compat). */
    for (int i = 4; i < TASK_MAX; i++) {
        if (i == current) continue;  /* never free the stack we're running on */
        task_info_t *t = task_get_raw(i);
        if (t && t->state == TASK_STATE_ZOMBIE) {
            /* Keep zombie around if parent is alive and hasn't collected yet */
            int ptid = t->parent_tid;
            if (ptid >= 0 && ptid < TASK_MAX) {
                task_info_t *parent = task_get(ptid);
                if (parent && parent->active) continue;  /* parent alive, wait for waitpid */
            }
            pipe_cleanup_task(i);
            fd_table_free(i);
            if (t->is_user) {
                if (t->kernel_stack) {
                    pmm_free_frame(t->kernel_stack);
                    t->kernel_stack = 0;
                }
                if (t->user_stack) {
                    pmm_free_frame(t->user_stack);
                    t->user_stack = 0;
                }

                /* VMA-based cleanup: walk all VMAs, free pages via refcount */
                if (t->vma) {
                    for (int v = 0; v < VMA_MAX_PER_TASK; v++) {
                        vma_t *vma = &t->vma->vmas[v];
                        if (!vma->active) continue;
                        if (t->page_dir && t->page_dir != vmm_get_kernel_pagedir()) {
                            for (uint32_t va = vma->vm_start; va < vma->vm_end; va += PAGE_SIZE) {
                                uint32_t pte = vmm_get_pte(t->page_dir, va);
                                if (!(pte & PTE_PRESENT)) continue;
                                uint32_t frame = pte & PAGE_MASK;
                                vmm_unmap_user_page(t->page_dir, va);
                                if (frame_ref_dec(frame) == 0)
                                    pmm_free_frame(frame);
                            }
                        }
                    }
                    vma_destroy(t->vma);
                    t->vma = NULL;
                } else {
                    /* Legacy path: elf_frames[] */
                    for (int f = 0; f < t->num_elf_frames; f++) {
                        if (t->elf_frames[f])
                            pmm_free_frame(t->elf_frames[f]);
                    }
                    t->num_elf_frames = 0;
                }

                if (t->user_page_table) {
                    pmm_free_frame(t->user_page_table);
                    t->user_page_table = 0;
                }
                if (t->page_dir && t->page_dir != vmm_get_kernel_pagedir()) {
                    vmm_destroy_user_pagedir(t->page_dir);
                    t->page_dir = 0;
                }
            } else if (t->stack_base) {
                free(t->stack_base);
                t->stack_base = 0;
            }
        }
    }

    /* Wake sleeping tasks whose sleep_until has passed */
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (t && t->state == TASK_STATE_SLEEPING) {
            if ((int32_t)(pit_ticks - t->sleep_until) >= 0)
                t->state = TASK_STATE_READY;
        }
    }

    /* Check if current task still has time slice remaining */
    int force_switch = 0;
    if (cur_is_preemptive && cur && cur->state == TASK_STATE_RUNNING) {
        if (cur->slice_remaining > 0)
            cur->slice_remaining--;
        if (cur->slice_remaining == 0)
            force_switch = 1;
    }

    /* Find next READY preemptive thread — priority-ordered, round-robin within level */
    int next_thread = -1;

    /* If current task still has time slice and no higher-priority task is ready, keep running */
    if (cur_is_preemptive && cur && cur->state == TASK_STATE_RUNNING && !force_switch) {
        /* Check for higher-priority READY tasks */
        int higher_ready = 0;
        for (int p = PRIO_REALTIME; p > cur->priority; p--) {
            for (int i = 4; i < TASK_MAX; i++) {
                task_info_t *t = task_get(i);
                if (t && t->state == TASK_STATE_READY && t->priority == p
                    && (t->stack_base || t->is_user)) {
                    higher_ready = 1;
                    break;
                }
            }
            if (higher_ready) break;
        }
        if (!higher_ready) {
            /* No higher-priority task ready, keep running current */
            next_thread = -2;  /* sentinel: don't switch */
        }
    }

    if (next_thread != -2) {
        /* Scan from highest to lowest priority */
        for (int p = PRIO_REALTIME; p >= PRIO_IDLE && next_thread < 0; p--) {
            int start = last_run[p];
            for (int i = 1; i <= TASK_MAX; i++) {
                int candidate = (start + i) % TASK_MAX;
                if (candidate < 4) continue;  /* Skip cooperative task slots */
                task_info_t *t = task_get(candidate);
                if (t && t->state == TASK_STATE_READY && t->priority == p
                    && (t->stack_base || t->is_user)) {
                    next_thread = candidate;
                    last_run[p] = candidate;
                    break;
                }
            }
        }
    }

    /* Reset sentinel to "no thread found" */
    if (next_thread == -2)
        next_thread = -1;

    if (!cur_is_preemptive) {
        /* Currently running cooperative code on boot stack */
        if (next_thread >= 0) {
            /* Save cooperative context and switch to preemptive thread */
            coop_esp = (uint32_t)regs;
            coop_task_id = current;

            task_info_t *nxt = task_get(next_thread);
            nxt->state = TASK_STATE_RUNNING;
            nxt->slice_remaining = nxt->time_slice;
            task_set_current(next_thread);
            if (nxt->is_user)
                tss_set_esp0(nxt->kernel_esp);
            if (nxt->tib)
                gdt_set_fs_base(nxt->tib);
            if (nxt->is_elf && nxt->tls_base)
                gdt_set_gs_base(nxt->tls_base);
            sched_switch_cr3(nxt->page_dir);
            return (registers_t*)nxt->esp;
        }
        /* No preemptive threads ready — cooperative code continues unchanged */
        return regs;
    } else {
        /* Currently running a preemptive thread */
        cur->esp = (uint32_t)regs;
        if (cur->state == TASK_STATE_RUNNING)
            cur->state = TASK_STATE_READY;

        if (next_thread >= 0) {
            /* Switch to another preemptive thread */
            task_info_t *nxt = task_get(next_thread);
            nxt->state = TASK_STATE_RUNNING;
            nxt->slice_remaining = nxt->time_slice;
            task_set_current(next_thread);
            if (nxt->is_user)
                tss_set_esp0(nxt->kernel_esp);
            if (nxt->tib)
                gdt_set_fs_base(nxt->tib);
            if (nxt->is_elf && nxt->tls_base)
                gdt_set_gs_base(nxt->tls_base);
            sched_switch_cr3(nxt->page_dir);
            return (registers_t*)nxt->esp;
        }

        /* No more preemptive threads — return to cooperative world */
        task_set_current(coop_task_id);
        sched_switch_cr3(vmm_get_kernel_pagedir());
        return (registers_t*)coop_esp;
    }
}

void sched_set_priority(int tid, uint8_t priority) {
    if (priority >= PRIO_LEVELS)
        priority = PRIO_NORMAL;
    task_info_t *t = task_get(tid);
    if (!t) return;

    uint32_t flags = irq_save();
    t->priority = priority;
    t->time_slice = prio_slices[priority];
    /* Don't reset slice_remaining — let current quantum finish */
    irq_restore(flags);
}

int sched_get_priority(int tid) {
    task_info_t *t = task_get(tid);
    if (!t) return -1;
    return t->priority;
}
