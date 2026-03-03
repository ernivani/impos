/*
 * aarch64 Syscall Handler — Phase 4.
 *
 * Convention:
 *   SVC #0 triggers a synchronous exception (EC=0x15 in ESR_EL1).
 *   x8  = syscall number
 *   x0-x5 = arguments
 *   x0  = return value
 *
 * For ELF tasks (is_elf=1), routes to linux_syscall_handler().
 */

#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/linux_syscall.h>
#include <kernel/io.h>
#include <stdint.h>
#include <string.h>

#define TARGET_HZ 120

/* ── ioctl dispatch (stub — full implementation in Phase 5+) ────── */

static int ioctl_dispatch(int fd, uint32_t cmd, void *arg) {
    (void)fd; (void)cmd; (void)arg;
    return -1;
}

/* ── syscall handler ────────────────────────────────────────────── */

registers_t* syscall_handler(registers_t* regs) {
    /* Route ELF tasks to Linux syscall handler */
    int tid = task_get_current();
    task_info_t *cur = task_get(tid);
    if (cur && cur->is_elf)
        return linux_syscall_handler(regs);

    uint64_t num = regs->x[8];

    switch (num) {
        case SYS_EXIT: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
                pipe_cleanup_task(tid);
                t->exit_code = (int)regs->x[0];
                t->state = TASK_STATE_ZOMBIE;
                t->active = 0;
                task_reparent_children(tid);
                /* Wake parent if blocked in waitpid */
                int ptid = t->parent_tid;
                if (ptid >= 0 && ptid < TASK_MAX) {
                    task_info_t *parent = task_get(ptid);
                    if (parent && parent->state == TASK_STATE_BLOCKED && parent->wait_tid != -1) {
                        if (parent->wait_tid == 0 || parent->wait_tid == tid)
                            parent->state = TASK_STATE_READY;
                    }
                }
            }
            return schedule(regs);
        }

        case SYS_YIELD:
            return schedule(regs);

        case SYS_SLEEP: {
            uint32_t ms = (uint32_t)regs->x[0];
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
                t->sleep_until = pit_get_ticks() + (ms * TARGET_HZ / 1000) + 1;
                t->state = TASK_STATE_SLEEPING;
            }
            return schedule(regs);
        }

        case SYS_GETPID:
            regs->x[0] = (uint64_t)task_get_pid(task_get_current());
            return regs;

        case SYS_READ: {
            int tid = task_get_current();
            int fd = (int)regs->x[0];
            char *buf = (char *)(uintptr_t)regs->x[1];
            int count = (int)regs->x[2];
            int rc = pipe_read(fd, buf, count, tid);
            if (rc == -2) {
                task_info_t *t = task_get(tid);
                if (t) t->state = TASK_STATE_BLOCKED;
                regs = schedule(regs);
                tid = task_get_current();
                rc = pipe_read(fd, buf, count, tid);
                if (rc == -2) rc = 0;
            }
            regs->x[0] = (uint64_t)(int64_t)rc;
            return regs;
        }

        case SYS_WRITE: {
            int tid = task_get_current();
            int fd = (int)regs->x[0];
            const char *buf = (const char *)(uintptr_t)regs->x[1];
            int count = (int)regs->x[2];

            /* For TTY fds, write to serial directly */
            if (fd >= 0 && cur && cur->fds && fd < cur->fd_count &&
                cur->fds[fd].type == FD_TTY) {
                for (int i = 0; i < count; i++)
                    serial_putc(buf[i]);
                regs->x[0] = (uint64_t)count;
                return regs;
            }

            int rc = pipe_write(fd, buf, count, tid);
            if (rc == -2) {
                task_info_t *t = task_get(tid);
                if (t) t->state = TASK_STATE_BLOCKED;
                regs = schedule(regs);
                tid = task_get_current();
                rc = pipe_write(fd, buf, count, tid);
                if (rc == -2) rc = -1;
            }
            regs->x[0] = (uint64_t)(int64_t)rc;
            return regs;
        }

        case SYS_OPEN: {
            /* Stub — needs filesystem (Phase 5+) */
            regs->x[0] = (uint64_t)-1;
            return regs;
        }

        case SYS_CLOSE: {
            int tid = task_get_current();
            int fd = (int)regs->x[0];
            pipe_close(fd, tid);
            regs->x[0] = 0;
            return regs;
        }

        case SYS_PIPE: {
            int tid = task_get_current();
            int *fds = (int *)(uintptr_t)regs->x[0];
            int rc = pipe_create(&fds[0], &fds[1], tid);
            regs->x[0] = (uint64_t)(int64_t)rc;
            return regs;
        }

        case SYS_KILL: {
            int pid = (int)regs->x[0];
            int signum = (int)regs->x[1];
            if (signum <= 0 || signum >= NSIG) signum = SIGTERM;
            int rc = sig_send_pid(pid, signum);
            regs->x[0] = (uint64_t)(int64_t)rc;
            return regs;
        }

        case SYS_SIGACTION: {
            int tid = task_get_current();
            int signum = (int)regs->x[0];
            sig_handler_t handler = (sig_handler_t)(uintptr_t)regs->x[1];
            sig_handler_t old = sig_set_handler(tid, signum, handler);
            regs->x[0] = (uint64_t)(uintptr_t)old;
            return regs;
        }

        case SYS_SIGRETURN: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (!t || !t->is_user) return regs;

            /* Read sig_context_t from user stack.
             * User SP points to: [signum(8)] [sig_context_t(272)] */
            uint64_t user_sp = regs->sp;
            /* TODO Phase 6: convert virtual→physical via page tables */
            uint64_t *phys_sp = (uint64_t *)user_sp;
            phys_sp++;  /* skip signum */
            sig_context_t *ctx = (sig_context_t *)phys_sp;

            /* Restore all registers */
            for (int i = 0; i < 31; i++)
                regs->x[i] = ctx->x[i];
            regs->sp   = ctx->sp;
            regs->elr  = ctx->elr;
            regs->spsr = ctx->spsr;

            t->sig.in_handler = 0;
            return regs;
        }

        case SYS_SHM_CREATE:
        case SYS_SHM_ATTACH:
        case SYS_SHM_DETACH:
            /* Stub — needs shared memory (Phase 5+) */
            regs->x[0] = (uint64_t)-1;
            return regs;

        case SYS_IOCTL: {
            int fd = (int)regs->x[0];
            uint32_t cmd = (uint32_t)regs->x[1];
            void *arg = (void *)(uintptr_t)regs->x[2];
            regs->x[0] = (uint64_t)(int64_t)ioctl_dispatch(fd, cmd, arg);
            return regs;
        }

        case SYS_MMAP: {
            uint64_t len = regs->x[1];
            if (len == 0) {
                regs->x[0] = (uint64_t)-1;
                return regs;
            }
            uint32_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
            if (num_pages == 1) {
                uintptr_t frame = pmm_alloc_frame();
                if (!frame) {
                    regs->x[0] = (uint64_t)-1;
                    return regs;
                }
                memset((void *)frame, 0, PAGE_SIZE);
                regs->x[0] = (uint64_t)frame;
            } else {
                regs->x[0] = (uint64_t)-1;  /* Multi-page: Phase 6 */
            }
            return regs;
        }

        case SYS_WAITPID: {
            int pid = (int)regs->x[0];
            int *wstatus = (int *)(uintptr_t)regs->x[1];
            int options = (int)regs->x[2];
            int rc = sys_waitpid(pid, wstatus, options);
            if (rc == 0 && !(options & WNOHANG))
                return schedule(regs);
            regs->x[0] = (uint64_t)(int64_t)rc;
            return regs;
        }

        case SYS_NICE: {
            int tid = task_get_current();
            uint8_t prio = (uint8_t)regs->x[0];
            sched_set_priority(tid, prio);
            regs->x[0] = 0;
            return regs;
        }

        default:
            return regs;
    }
}
