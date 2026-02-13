#include <kernel/linux_syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/idt.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Linux write(fd, buf, count) ─────────────────────────────── */

static int32_t linux_sys_write(uint32_t fd, const char *buf, uint32_t count) {
    if (fd == 1 || fd == 2) {
        /* stdout / stderr → console */
        for (uint32_t i = 0; i < count; i++)
            putchar(buf[i]);
        return (int32_t)count;
    }
    /* Other fds: try pipe system */
    int tid = task_get_current();
    int rc = pipe_write((int)fd, buf, (int)count, tid);
    if (rc >= 0)
        return (int32_t)rc;
    return -LINUX_EBADF;
}

/* ── Linux writev(fd, iov, iovcnt) ───────────────────────────── */

static int32_t linux_sys_writev(uint32_t fd, const struct linux_iovec *iov,
                                 uint32_t iovcnt) {
    int32_t total = 0;
    for (uint32_t i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0)
            continue;
        int32_t ret = linux_sys_write(fd, (const char *)iov[i].iov_base,
                                       iov[i].iov_len);
        if (ret < 0)
            return ret;
        total += ret;
    }
    return total;
}

/* ── Linux brk(new_brk) ─────────────────────────────────────── */

static uint32_t linux_sys_brk(uint32_t new_brk) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return 0;

    /* brk(0) or brk(<=current): return current break */
    if (new_brk == 0 || new_brk <= t->brk_current)
        return t->brk_current;

    /* Expand: allocate pages for the gap */
    uint32_t old_page = (t->brk_current + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t new_page = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint32_t va = old_page; va < new_page; va += PAGE_SIZE) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame)
            return t->brk_current;  /* out of memory — return old value */
        memset((void *)frame, 0, PAGE_SIZE);

        if (!vmm_map_user_page(t->page_dir, va, frame,
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
            pmm_free_frame(frame);
            return t->brk_current;
        }

        /* Track for cleanup */
        if (t->num_elf_frames < 64)
            t->elf_frames[t->num_elf_frames++] = frame;
    }

    t->brk_current = new_brk;
    return new_brk;
}

/* ── Linux mmap2(addr, len, prot, flags, fd, pgoff) ──────────── */

static uint32_t linux_sys_mmap2(uint32_t addr, uint32_t len, uint32_t prot,
                                 uint32_t flags, uint32_t fd, uint32_t pgoff) {
    (void)addr; (void)prot; (void)fd; (void)pgoff;

    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return (uint32_t)-LINUX_ENOMEM;

    /* Only support MAP_ANONYMOUS for now */
    if (!(flags & LINUX_MAP_ANONYMOUS))
        return (uint32_t)-LINUX_ENOSYS;

    uint32_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t va_start = t->mmap_next;

    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame)
            return (uint32_t)-LINUX_ENOMEM;
        memset((void *)frame, 0, PAGE_SIZE);

        uint32_t va = va_start + i * PAGE_SIZE;
        if (!vmm_map_user_page(t->page_dir, va, frame,
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
            pmm_free_frame(frame);
            return (uint32_t)-LINUX_ENOMEM;
        }

        if (t->num_elf_frames < 64)
            t->elf_frames[t->num_elf_frames++] = frame;
    }

    t->mmap_next += num_pages * PAGE_SIZE;
    return va_start;
}

/* ── Linux set_thread_area(user_desc*) ───────────────────────── */

static int32_t linux_sys_set_thread_area(struct linux_user_desc *u_info) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || !u_info) return -LINUX_EINVAL;

    t->tls_base = u_info->base_addr;
    gdt_set_gs_base(u_info->base_addr);

    /* Assign GDT entry number 6 (selector 0x33) */
    u_info->entry_number = 6;

    return 0;
}

/* ── Linux ioctl(fd, cmd, ...) ───────────────────────────────── */

static int32_t linux_sys_ioctl(uint32_t fd, uint32_t cmd, uint32_t arg) {
    (void)fd; (void)cmd; (void)arg;
    /* Stub: musl may call ioctl on stdout (TIOCGWINSZ etc.) */
    return -LINUX_ENOSYS;
}

/* ── Dispatcher ──────────────────────────────────────────────── */

registers_t* linux_syscall_handler(registers_t* regs) {
    uint32_t nr = regs->eax;

    switch (nr) {
        case LINUX_SYS_exit:
        case LINUX_SYS_exit_group: {
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

        case LINUX_SYS_write:
            regs->eax = (uint32_t)linux_sys_write(regs->ebx,
                                                    (const char *)regs->ecx,
                                                    regs->edx);
            return regs;

        case LINUX_SYS_read: {
            int rtid = task_get_current();
            int rrc = pipe_read((int)regs->ebx, (char *)regs->ecx,
                                (int)regs->edx, rtid);
            regs->eax = (rrc >= 0) ? (uint32_t)rrc : 0;
            return regs;
        }

        case LINUX_SYS_writev:
            regs->eax = (uint32_t)linux_sys_writev(
                regs->ebx,
                (const struct linux_iovec *)regs->ecx,
                regs->edx);
            return regs;

        case LINUX_SYS_brk:
            regs->eax = linux_sys_brk(regs->ebx);
            return regs;

        case LINUX_SYS_mmap2:
            regs->eax = linux_sys_mmap2(regs->ebx, regs->ecx, regs->edx,
                                         regs->esi, regs->edi, regs->ebp);
            return regs;

        case LINUX_SYS_set_thread_area:
            regs->eax = (uint32_t)linux_sys_set_thread_area(
                (struct linux_user_desc *)regs->ebx);
            return regs;

        case LINUX_SYS_ioctl:
            regs->eax = (uint32_t)linux_sys_ioctl(regs->ebx, regs->ecx,
                                                    regs->edx);
            return regs;

        default:
            regs->eax = (uint32_t)-LINUX_ENOSYS;
            return regs;
    }
}
