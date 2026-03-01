#include <kernel/task.h>
#include <kernel/io.h>

/*
 * sys_waitpid — wait for child process state changes.
 *
 * pid > 0:  wait for specific child with that PID
 * pid == -1: wait for any child
 * pid == 0:  wait for any child in same process group (Step D)
 *
 * Returns child PID on success, 0 if WNOHANG and no zombie, -1 on error.
 */
int sys_waitpid(int pid, int *wstatus, int options) {
    int tid = task_get_current();
    task_info_t *self = task_get(tid);
    if (!self) return -1;

    uint32_t flags = irq_save();

    /* First pass: look for zombie children matching the criteria */
    int found_child = 0;  /* any matching child exists (zombie or not) */

    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *child = task_get_raw(i);
        if (!child) continue;
        if (child->parent_tid != tid) continue;

        /* Check PID filter */
        if (pid > 0 && child->pid != pid) continue;
        /* pid == 0: process group matching (future Step D) */
        /* pid == -1: any child */

        found_child = 1;

        if (child->state == TASK_STATE_ZOMBIE) {
            /* Found a zombie child — collect it */
            int child_pid = child->pid;
            int code = child->exit_code;

            if (wstatus) {
                /* Encode exit status in POSIX format: (code << 8) | 0 */
                *wstatus = (code & 0xFF) << 8;
            }

            /* Fully reap the zombie: clear the task slot */
            child->state = TASK_STATE_UNUSED;
            child->parent_tid = -1;
            child->pid = 0;

            irq_restore(flags);
            return child_pid;
        }
    }

    if (!found_child) {
        /* No children matching the criteria at all */
        irq_restore(flags);
        return -1;  /* ECHILD */
    }

    /* Children exist but none are zombies */
    if (options & WNOHANG) {
        irq_restore(flags);
        return 0;  /* No zombie yet */
    }

    /* Block until a child exits */
    self->wait_tid = (pid > 0) ? task_find_by_pid(pid) : 0;
    self->state = TASK_STATE_BLOCKED;

    irq_restore(flags);

    /* Yield — will be woken by child's task_exit_code() */
    task_yield();

    /* After waking up, scan for zombie children again */
    flags = irq_save();
    self->wait_tid = -1;

    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *child = task_get_raw(i);
        if (!child) continue;
        if (child->parent_tid != tid) continue;
        if (pid > 0 && child->pid != pid) continue;

        if (child->state == TASK_STATE_ZOMBIE) {
            int child_pid = child->pid;
            int code = child->exit_code;

            if (wstatus)
                *wstatus = (code & 0xFF) << 8;

            child->state = TASK_STATE_UNUSED;
            child->parent_tid = -1;
            child->pid = 0;

            irq_restore(flags);
            return child_pid;
        }
    }

    irq_restore(flags);
    return -1;  /* Shouldn't normally reach here */
}
