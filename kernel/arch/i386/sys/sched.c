#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <stdint.h>
#include <stdlib.h>

extern volatile uint32_t pit_ticks;

static volatile int scheduler_active = 0;

/*
 * Cooperative tasks (slots 0-3) share the boot stack and use task_set_current()
 * cooperatively. We save/restore the boot stack context as a single entity.
 * Preemptive threads (slots 4+) each have their own stack.
 */
static uint32_t coop_esp = 0;       /* Saved boot stack ESP */
static int coop_task_id = TASK_KERNEL; /* Which cooperative task was current */

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
    scheduler_active = 1;
}

registers_t* schedule(registers_t* regs) {
    if (!scheduler_active)
        return regs;

    int current = task_get_current();
    task_info_t *cur = task_get(current);

    /* Determine if current task is a preemptive thread (has own stack) */
    int cur_is_preemptive = (cur && cur->stack_base != NULL);

    /* Clean up zombie threads (free their stacks safely) */
    for (int i = 4; i < TASK_MAX; i++) {
        /* task_get returns NULL for inactive tasks, so access directly */
        task_info_t *t = task_get_raw(i);
        if (t && t->state == TASK_STATE_ZOMBIE && t->stack_base) {
            free(t->stack_base);
            t->stack_base = 0;
        }
    }

    /* Wake sleeping tasks whose sleep_until has passed */
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (t && t->state == TASK_STATE_SLEEPING) {
            if (pit_ticks >= t->sleep_until)
                t->state = TASK_STATE_READY;
        }
    }

    /* Find next READY preemptive thread (round-robin among threads only) */
    int search_start = cur_is_preemptive ? current : 3;
    int next_thread = -1;
    for (int i = 1; i <= TASK_MAX; i++) {
        int candidate = (search_start + i) % TASK_MAX;
        if (candidate < 4) continue;  /* Skip cooperative task slots */
        task_info_t *t = task_get(candidate);
        if (t && t->state == TASK_STATE_READY && t->stack_base) {
            next_thread = candidate;
            break;
        }
    }

    if (!cur_is_preemptive) {
        /* Currently running cooperative code on boot stack */
        if (next_thread >= 0) {
            /* Save cooperative context and switch to preemptive thread */
            coop_esp = (uint32_t)regs;
            coop_task_id = current;

            task_info_t *nxt = task_get(next_thread);
            nxt->state = TASK_STATE_RUNNING;
            task_set_current(next_thread);
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
            task_set_current(next_thread);
            return (registers_t*)nxt->esp;
        }

        /* No more preemptive threads — return to cooperative world */
        task_set_current(coop_task_id);
        return (registers_t*)coop_esp;
    }
}
