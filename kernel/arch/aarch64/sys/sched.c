/*
 * aarch64 Scheduler — Phase 3.
 *
 * Called from arch_handle_exception() on timer IRQ. Returns a (possibly
 * different) registers_t* which the exception_stubs.S restores via eret.
 *
 * The context switch mechanism:
 *   Timer IRQ fires → exception_stubs.S saves all regs to stack →
 *   arch_handle_exception() → schedule(regs) returns new_regs →
 *   exception_stubs.S restores from new_regs → eret into new thread.
 *
 * No TSS or CR3 switching needed on aarch64:
 *   - SP_EL1 is used automatically for EL0→EL1 transitions
 *   - TTBR0_EL1 replaces CR3 for page directory switching
 */

#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/vmm.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/pmm.h>
#include <kernel/pipe.h>
#include <stdint.h>
#include <stdlib.h>

extern volatile uint32_t pit_ticks;

static volatile int scheduler_active = 0;

/* Cooperative tasks (0-3) share the boot stack */
static uintptr_t coop_esp = 0;
static int coop_task_id = TASK_KERNEL;
static uintptr_t current_ttbr = 0;

/* Per-priority round-robin tracking */
static int last_run[PRIO_LEVELS] = { 3, 3, 3, 3 };

static const uint8_t prio_slices[PRIO_LEVELS] = {
    SLICE_IDLE, SLICE_BACKGROUND, SLICE_NORMAL, SLICE_REALTIME
};

static inline void sched_switch_ttbr(uintptr_t new_ttbr) {
    if (new_ttbr && new_ttbr != current_ttbr) {
        current_ttbr = new_ttbr;
        __asm__ volatile(
            "msr ttbr0_el1, %0\n"
            "isb\n"
            "tlbi vmalle1is\n"
            "dsb ish\n"
            "isb\n"
            :: "r"((uint64_t)current_ttbr) : "memory"
        );
    }
}

int sched_is_active(void) {
    return scheduler_active;
}

void sched_init(void) {
    task_info_t *boot = task_get(TASK_KERNEL);
    if (boot) boot->state = TASK_STATE_RUNNING;

    task_info_t *idle = task_get(TASK_IDLE);
    if (idle) idle->state = TASK_STATE_READY;

    task_info_t *wm = task_get(TASK_WM);
    if (wm) wm->state = TASK_STATE_READY;

    task_info_t *sh = task_get(TASK_SHELL);
    if (sh) sh->state = TASK_STATE_READY;

    coop_task_id = TASK_KERNEL;
    current_ttbr = vmm_get_kernel_pagedir();
    scheduler_active = 1;
}

registers_t* schedule(registers_t* regs) {
    if (!scheduler_active)
        return regs;

    int current = task_get_current();
    task_info_t *cur = task_get_raw(current);

    int cur_is_preemptive = (cur && (cur->stack_base != NULL || cur->is_user));

    /* Clean up zombie threads */
    for (int i = 4; i < TASK_MAX; i++) {
        if (i == current) continue;
        task_info_t *t = task_get_raw(i);
        if (t && t->state == TASK_STATE_ZOMBIE) {
            int ptid = t->parent_tid;
            if (ptid >= 0 && ptid < TASK_MAX) {
                task_info_t *parent = task_get(ptid);
                if (parent && parent->active) continue;
            }
            pipe_cleanup_task(i);
            fd_table_free(i);
            if (t->stack_base) {
                free(t->stack_base);
                t->stack_base = 0;
            }
        }
    }

    /* Wake sleeping tasks */
    uint32_t ticks = pit_get_ticks();
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (t && t->state == TASK_STATE_SLEEPING) {
            if ((int32_t)(ticks - t->sleep_until) >= 0)
                t->state = TASK_STATE_READY;
        }
    }

    /* Check time slice */
    int force_switch = 0;
    if (cur_is_preemptive && cur && cur->state == TASK_STATE_RUNNING) {
        if (cur->slice_remaining > 0)
            cur->slice_remaining--;
        if (cur->slice_remaining == 0)
            force_switch = 1;
    }

    /* Find next READY preemptive thread */
    int next_thread = -1;

    if (cur_is_preemptive && cur && cur->state == TASK_STATE_RUNNING && !force_switch) {
        int higher_ready = 0;
        for (int p = PRIO_REALTIME; p > (int)cur->priority; p--) {
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
        if (!higher_ready)
            next_thread = -2;  /* keep current */
    }

    if (next_thread != -2) {
        for (int p = PRIO_REALTIME; p >= PRIO_IDLE && next_thread < 0; p--) {
            int start = last_run[p];
            for (int i = 1; i <= TASK_MAX; i++) {
                int candidate = (start + i) % TASK_MAX;
                if (candidate < 4) continue;
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

    if (next_thread == -2)
        next_thread = -1;

    if (!cur_is_preemptive) {
        /* Currently on boot stack (cooperative tasks 0-3) */
        if (next_thread >= 0) {
            coop_esp = (uintptr_t)regs;
            coop_task_id = current;

            task_info_t *nxt = task_get(next_thread);
            nxt->state = TASK_STATE_RUNNING;
            nxt->slice_remaining = nxt->time_slice;
            task_set_current(next_thread);
            sched_switch_ttbr(nxt->page_dir);
            return (registers_t*)nxt->esp;
        }
        return regs;
    } else {
        /* Currently on a preemptive thread's stack */
        cur->esp = (uintptr_t)regs;
        if (cur->state == TASK_STATE_RUNNING)
            cur->state = TASK_STATE_READY;

        if (next_thread >= 0) {
            task_info_t *nxt = task_get(next_thread);
            nxt->state = TASK_STATE_RUNNING;
            nxt->slice_remaining = nxt->time_slice;
            task_set_current(next_thread);
            sched_switch_ttbr(nxt->page_dir);
            return (registers_t*)nxt->esp;
        }

        /* No preemptive threads — return to cooperative world */
        task_set_current(coop_task_id);
        sched_switch_ttbr(vmm_get_kernel_pagedir());
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
    irq_restore(flags);
}

int sched_get_priority(int tid) {
    task_info_t *t = task_get(tid);
    if (!t) return -1;
    return t->priority;
}
