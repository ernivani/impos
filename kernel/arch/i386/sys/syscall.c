#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/linux_syscall.h>
#include <kernel/ioctl.h>
#include <kernel/drm.h>
#include <kernel/fs.h>
#include <stdint.h>
#include <string.h>

extern volatile uint32_t pit_ticks;
#define TARGET_HZ 120

/* ── ioctl dispatch ─────────────────────────────────────────────── */

int ioctl_dispatch(int fd, uint32_t cmd, void *arg) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd < 0 || fd >= MAX_FDS)
        return -1;

    fd_entry_t *fde = &t->fds[fd];

    switch (fde->type) {
        case FD_DRM:
            return drm_ioctl(cmd, arg);

        case FD_DEV:
        case FD_TTY:
            /* Future: TTY ioctls (TIOCGWINSZ etc.) */
            return -1;

        default:
            return -1;  /* ioctl not supported on this fd type */
    }
}

/* ── syscall handler ────────────────────────────────────────────── */

registers_t* syscall_handler(registers_t* regs) {
    /* Route ELF tasks to Linux syscall handler */
    int tid = task_get_current();
    task_info_t *cur = task_get(tid);
    if (cur && cur->is_elf)
        return linux_syscall_handler(regs);

    switch (regs->eax) {
        case SYS_EXIT: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
                pipe_cleanup_task(tid);
                shm_cleanup_task(tid);
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

        case SYS_OPEN: {
            /* Open a file/device by path. EBX=path, ECX=flags */
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (!t) {
                regs->eax = (uint32_t)-1;
                return regs;
            }
            const char *path = (const char *)regs->ebx;
            int fd = fd_alloc(tid);
            if (fd < 0) {
                regs->eax = (uint32_t)-1;
                return regs;
            }
            /* Resolve path to inode */
            uint32_t parent;
            char name[28];
            int ino = fs_resolve_path(path, &parent, name);
            if (ino < 0) {
                regs->eax = (uint32_t)-1;
                return regs;
            }
            inode_t node;
            if (fs_read_inode((uint32_t)ino, &node) < 0) {
                regs->eax = (uint32_t)-1;
                return regs;
            }
            /* Set FD type based on inode */
            int fd_type;
            switch (node.type) {
                case INODE_FILE:    fd_type = FD_FILE; break;
                case INODE_DIR:     fd_type = FD_DIR;  break;
                case INODE_CHARDEV:
                    fd_type = ((uint8_t)node.blocks[0] == DEV_MAJOR_DRM) ? FD_DRM : FD_DEV;
                    break;
                default:
                    regs->eax = (uint32_t)-1;
                    return regs;
            }
            t->fds[fd].type = fd_type;
            t->fds[fd].inode = (uint32_t)ino;
            t->fds[fd].offset = 0;
            t->fds[fd].flags = regs->ecx;
            regs->eax = (uint32_t)fd;
            return regs;
        }

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
            int signum = (int)regs->ecx;
            if (signum <= 0 || signum >= NSIG) signum = SIGTERM;
            int rc = sig_send_pid(pid, signum);
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case SYS_SIGACTION: {
            int tid = task_get_current();
            int signum = (int)regs->ebx;
            sig_handler_t handler = (sig_handler_t)regs->ecx;
            sig_handler_t old = sig_set_handler(tid, signum, handler);
            regs->eax = (uint32_t)old;
            return regs;
        }

        case SYS_SIGRETURN: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (!t || !t->is_user) return regs;

            /* User ESP at int $0x80 points to: [signum] [sig_context_t] */
            uint32_t user_esp = regs->useresp;
            uint32_t offset = user_esp - USER_SPACE_BASE;
            if (offset > PAGE_SIZE) return regs;

            uint32_t *phys_sp = (uint32_t *)(t->user_stack + offset);
            sig_context_t *ctx = (sig_context_t *)(phys_sp + 1); /* skip signum */

            /* Restore all registers */
            regs->eip     = ctx->eip;
            regs->cs      = ctx->cs;
            regs->eflags  = ctx->eflags;
            regs->useresp = ctx->esp;
            regs->ss      = ctx->ss;
            regs->eax     = ctx->eax;
            regs->ecx     = ctx->ecx;
            regs->edx     = ctx->edx;
            regs->ebx     = ctx->ebx;
            regs->esi     = ctx->esi;
            regs->edi     = ctx->edi;
            regs->ebp     = ctx->ebp;
            regs->ds      = ctx->ds;
            regs->es      = ctx->es;
            regs->fs      = ctx->fs;
            regs->gs      = ctx->gs;

            t->sig.in_handler = 0;
            return regs;
        }

        case SYS_SHM_CREATE: {
            const char *name = (const char *)regs->ebx;
            uint32_t size = regs->ecx;
            regs->eax = (uint32_t)shm_create(name, size);
            return regs;
        }

        case SYS_SHM_ATTACH: {
            int tid = task_get_current();
            int region_id = (int)regs->ebx;
            regs->eax = shm_attach(region_id, tid);
            return regs;
        }

        case SYS_SHM_DETACH: {
            int tid = task_get_current();
            int region_id = (int)regs->ebx;
            regs->eax = (uint32_t)shm_detach(region_id, tid);
            return regs;
        }

        case SYS_IOCTL: {
            int fd = (int)regs->ebx;
            uint32_t cmd = regs->ecx;
            void *arg = (void *)regs->edx;
            regs->eax = (uint32_t)ioctl_dispatch(fd, cmd, arg);
            return regs;
        }

        case SYS_MMAP: {
            /* Minimal mmap: anonymous mapping only.
             * In kernel space (identity-mapped), this just allocates
             * physical frames and returns the physical address.
             * EBX=addr (hint, ignored), ECX=length, EDX=prot, ESI=flags */
            uint32_t len = regs->ecx;
            if (len == 0) {
                regs->eax = (uint32_t)-1;
                return regs;
            }
            uint32_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
            /* For single page, use pmm_alloc_frame directly */
            if (num_pages == 1) {
                uint32_t frame = pmm_alloc_frame();
                if (!frame) {
                    regs->eax = (uint32_t)-1;
                    return regs;
                }
                memset((void *)frame, 0, PAGE_SIZE);
                regs->eax = frame;
            } else {
                /* Multi-page: allocate consecutive frames (best effort).
                 * For now, allocate individually — they'll be identity-mapped
                 * and contiguous in the first 256MB anyway. */
                uint32_t first = 0;
                uint32_t frames[64];
                if (num_pages > 64) {
                    regs->eax = (uint32_t)-1;
                    return regs;
                }
                for (uint32_t i = 0; i < num_pages; i++) {
                    frames[i] = pmm_alloc_frame();
                    if (!frames[i]) {
                        /* Rollback */
                        for (uint32_t j = 0; j < i; j++)
                            pmm_free_frame(frames[j]);
                        regs->eax = (uint32_t)-1;
                        return regs;
                    }
                    memset((void *)frames[i], 0, PAGE_SIZE);
                    if (i == 0) first = frames[i];
                }
                regs->eax = first;
            }
            return regs;
        }

        default:
            return regs;
    }
}
