#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pipe.h>
#include <stdint.h>

extern volatile uint32_t pit_ticks;
#define TARGET_HZ 120

registers_t* syscall_handler(registers_t* regs) {
    switch (regs->eax) {
        case SYS_EXIT: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
                pipe_cleanup_task(tid);
                t->state = TASK_STATE_ZOMBIE;
                t->active = 0;
            }
            return schedule(regs);
        }

        case SYS_YIELD:
            return schedule(regs);

        case SYS_SLEEP: {
            uint32_t ms = regs->ebx;
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
                t->sleep_until = pit_ticks + (ms * TARGET_HZ / 1000) + 1;
                t->state = TASK_STATE_SLEEPING;
            }
            return schedule(regs);
        }

        case SYS_GETPID:
            regs->eax = task_get_pid(task_get_current());
            return regs;

        case SYS_READ: {
            int tid = task_get_current();
            int fd = (int)regs->ebx;
            char *buf = (char *)regs->ecx;
            int count = (int)regs->edx;
            int rc = pipe_read(fd, buf, count, tid);
            if (rc == -2) {
                /* Block and reschedule; on wake, retry */
                task_info_t *t = task_get(tid);
                if (t) t->state = TASK_STATE_BLOCKED;
                regs = schedule(regs);
                /* After unblock, retry the read */
                tid = task_get_current();
                rc = pipe_read(fd, buf, count, tid);
                if (rc == -2) rc = 0;  /* shouldn't happen, but safe fallback */
            }
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case SYS_WRITE: {
            int tid = task_get_current();
            int fd = (int)regs->ebx;
            const char *buf = (const char *)regs->ecx;
            int count = (int)regs->edx;
            int rc = pipe_write(fd, buf, count, tid);
            if (rc == -2) {
                task_info_t *t = task_get(tid);
                if (t) t->state = TASK_STATE_BLOCKED;
                regs = schedule(regs);
                tid = task_get_current();
                rc = pipe_write(fd, buf, count, tid);
                if (rc == -2) rc = -1;
            }
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case SYS_OPEN:
            /* Reserved for future file FDs */
            regs->eax = (uint32_t)-1;
            return regs;

        case SYS_CLOSE: {
            int tid = task_get_current();
            int fd = (int)regs->ebx;
            pipe_close(fd, tid);
            regs->eax = 0;
            return regs;
        }

        case SYS_PIPE: {
            int tid = task_get_current();
            int *fds = (int *)regs->ebx;
            int rc = pipe_create(&fds[0], &fds[1], tid);
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case SYS_KILL: {
            int pid = (int)regs->ebx;
            /* ECX = signal number (unused for now, just kill) */
            int rc = task_kill_by_pid(pid);
            regs->eax = (uint32_t)rc;
            return regs;
        }

        default:
            return regs;
    }
}
