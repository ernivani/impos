/*
 * clone.c — Linux clone()/fork()/vfork() implementation for ImposOS
 *
 * clone() creates a new process or thread by duplicating the calling task.
 * The child inherits (or shares, depending on flags) the parent's address
 * space, FD table, and signal handlers.
 *
 * For ImposOS's identity-mapped kernel (first 256MB), CLONE_VM for kernel
 * tasks is essentially free — they all share the same physical mapping.
 * For ELF user tasks, the child shares the parent's page directory
 * (vfork semantics) since we don't have COW yet.
 */

#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/vma.h>
#include <kernel/frame_ref.h>
#include <kernel/linux_syscall.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <string.h>
#include <stdlib.h>

extern task_info_t *task_get_raw(int tid);

/*
 * sys_clone — core implementation for clone/fork/vfork
 *
 * Parameters (Linux i386 ABI for clone):
 *   clone_flags: combination of LINUX_CLONE_* flags and termination signal
 *   child_stack: user stack pointer for child (0 = inherit parent's)
 *   parent_regs: parent's register state at time of syscall
 *
 * Returns:
 *   Parent: child's PID (> 0)
 *   Child:  0 (set via crafted register frame on child's kernel stack)
 *   Error:  negative errno
 */
int sys_clone(uint32_t clone_flags, uint32_t child_stack,
              registers_t *parent_regs) {
    int parent_tid = task_get_current();
    task_info_t *parent = task_get(parent_tid);
    if (!parent) return -LINUX_EAGAIN;

    /* Find a free task slot */
    uint32_t irqf = irq_save();
    int child_tid = -1;
    for (int i = 4; i < TASK_MAX; i++) {
        task_info_t *t = task_get_raw(i);
        if (t && !t->active && t->state == TASK_STATE_UNUSED) {
            child_tid = i;
            break;
        }
    }
    if (child_tid < 0) {
        irq_restore(irqf);
        return -LINUX_EAGAIN;
    }

    /* Reserve the slot */
    task_info_t *child = task_get_raw(child_tid);
    memset(child, 0, sizeof(task_info_t));
    child->active = 1;
    child->state = TASK_STATE_BLOCKED;  /* not ready yet */
    irq_restore(irqf);

    /* Allocate kernel stack for child (always needed) */
    uint32_t kstack = pmm_alloc_frame();
    if (!kstack) {
        child->active = 0;
        child->state = TASK_STATE_UNUSED;
        return -LINUX_EAGAIN;
    }
    memset((void *)kstack, 0, PAGE_SIZE);

    irqf = irq_save();

    /* Copy basic task info from parent */
    strncpy(child->name, parent->name, 31);
    child->name[31] = '\0';
    child->killable = parent->killable;
    child->wm_id = -1;
    child->pid = task_assign_pid(child_tid);
    child->is_user = parent->is_user;
    child->is_elf = parent->is_elf;
    child->parent_tid = parent_tid;
    child->wait_tid = -1;

    /* Priority: inherit from parent */
    child->priority = parent->priority;
    child->time_slice = parent->time_slice;
    child->slice_remaining = parent->time_slice;

    /* Process group & session: inherit */
    child->pgid = parent->pgid;
    child->sid = parent->sid;

    /* File creation mask: inherit from parent */
    child->umask = parent->umask;

    /* Kernel stack setup */
    child->kernel_stack = kstack;
    child->kernel_esp = kstack + PAGE_SIZE;

    /* Address space */
    if (parent->is_elf || parent->is_user) {
        if (clone_flags & LINUX_CLONE_VM) {
            /* Shared address space (thread-like) */
            child->page_dir = parent->page_dir;
            child->user_stack = 0;
            child->user_page_table = 0;
            /* Share VMA table for threads */
            child->vma = parent->vma;
        } else {
            /* COW Fork: create new page directory, share physical pages
             * as read-only, copy on write via page fault handler. */
            uint32_t child_pd = vmm_create_user_pagedir();
            if (!child_pd) {
                pmm_free_frame(kstack);
                child->active = 0;
                child->state = TASK_STATE_UNUSED;
                irq_restore(irqf);
                return -LINUX_EAGAIN;
            }

            /* Clone VMAs */
            child->vma = parent->vma ? vma_clone(parent->vma) : NULL;

            /* Walk each VMA and set up COW sharing */
            if (parent->vma) {
                for (int v = 0; v < VMA_MAX_PER_TASK; v++) {
                    vma_t *vma = &parent->vma->vmas[v];
                    if (!vma->active) continue;

                    for (uint32_t va = vma->vm_start; va < vma->vm_end; va += PAGE_SIZE) {
                        uint32_t pte = vmm_get_pte(parent->page_dir, va);
                        if (!(pte & PTE_PRESENT)) continue;

                        uint32_t frame = pte & PAGE_MASK;
                        uint32_t flags = pte & 0xFFF;

                        /* Mark as COW: clear writable, set COW bit */
                        if (flags & PTE_WRITABLE) {
                            flags = (flags & ~PTE_WRITABLE) | PTE_COW;
                            /* Update parent's PTE to read-only + COW */
                            vmm_map_user_page(parent->page_dir, va, frame, flags);
                        }

                        /* Map same frame in child with same flags */
                        vmm_map_user_page(child_pd, va, frame, flags);

                        /* Increment frame refcount */
                        frame_ref_inc(frame);
                    }
                }
            }

            /* Flush parent TLB since we changed its PTEs */
            vmm_flush_tlb();

            child->page_dir = child_pd;
            child->user_stack = 0;
            child->user_page_table = 0;
        }
        /* Copy ELF-specific state */
        child->brk_start = parent->brk_start;
        child->brk_current = parent->brk_current;
        child->mmap_next = parent->mmap_next;
        child->tls_base = parent->tls_base;
    } else {
        /* Kernel thread: share kernel page directory */
        child->page_dir = vmm_get_kernel_pagedir();
    }

    /* FD table: copy or share */
    if (clone_flags & LINUX_CLONE_FILES) {
        /* Share FD table — point to same allocation.
         * For now, simple pointer copy. Full refcounting deferred to
         * when we add fd_table_t wrapper struct. */
        child->fds = parent->fds;
        child->fd_count = parent->fd_count;
    } else {
        /* Fork: copy FD table */
        fd_table_init(child_tid);
        if (child->fds && parent->fds) {
            int copy_count = parent->fd_count;
            if (copy_count > child->fd_count) {
                /* Need to grow child's table to match parent */
                fd_entry_t *new_fds = (fd_entry_t *)realloc(
                    child->fds, copy_count * sizeof(fd_entry_t));
                if (new_fds) {
                    child->fds = new_fds;
                    child->fd_count = copy_count;
                }
            }
            int n = (parent->fd_count < child->fd_count)
                    ? parent->fd_count : child->fd_count;
            memcpy(child->fds, parent->fds, n * sizeof(fd_entry_t));

            /* Bump pipe refcounts for copied pipe FDs.
             * For each pipe end in the child's table, the underlying
             * pipe_t needs to know there's an additional reader/writer. */
            for (int i = 0; i < n; i++) {
                if (child->fds[i].type == FD_PIPE_R)
                    pipe_fork_bump(child->fds[i].pipe_id, 1);
                else if (child->fds[i].type == FD_PIPE_W)
                    pipe_fork_bump(child->fds[i].pipe_id, 0);
            }
        }
    }

    /* Signal handlers: copy or share */
    if (clone_flags & LINUX_CLONE_SIGHAND) {
        /* Share signal handlers — for now, just copy */
        child->sig = parent->sig;
    } else {
        /* Fork: copy signal state */
        child->sig = parent->sig;
    }
    child->sig.pending = 0;  /* child starts with no pending signals */
    child->sig.in_handler = 0;

    /* Craft the child's kernel stack to return from the syscall with EAX=0.
     *
     * The child's kernel stack must look exactly like what isr_common
     * would have pushed when the parent made the INT 0x80 syscall.
     * When the scheduler restores this child, it will pop segments,
     * popa, skip int_no/err_code, and iret — returning to user/kernel
     * code with EAX=0.
     */
    uint32_t *ksp = (uint32_t *)(kstack + PAGE_SIZE);

    if (parent->is_user || parent->is_elf) {
        /* Ring 3 iret frame: includes SS and UserESP */
        *(--ksp) = parent_regs->ss;        /* SS */
        uint32_t child_user_esp = child_stack ? child_stack : parent_regs->useresp;
        *(--ksp) = child_user_esp;          /* UserESP */
        *(--ksp) = parent_regs->eflags;    /* EFLAGS */
        *(--ksp) = parent_regs->cs;        /* CS */
        *(--ksp) = parent_regs->eip;       /* EIP */
    } else {
        /* Ring 0 iret frame: no SS/ESP (same privilege) */
        *(--ksp) = parent_regs->eflags;    /* EFLAGS */
        *(--ksp) = parent_regs->cs;        /* CS */
        *(--ksp) = parent_regs->eip;       /* EIP */
    }

    /* ISR stub expects int_no and err_code */
    *(--ksp) = parent_regs->err_code;
    *(--ksp) = parent_regs->int_no;

    /* pusha block: copy parent's registers, but set EAX=0 for child */
    *(--ksp) = 0;                          /* EAX = 0 (child return value!) */
    *(--ksp) = parent_regs->ecx;
    *(--ksp) = parent_regs->edx;
    *(--ksp) = parent_regs->ebx;
    *(--ksp) = 0;                          /* ESP (ignored by popa) */
    *(--ksp) = parent_regs->ebp;
    *(--ksp) = parent_regs->esi;
    *(--ksp) = parent_regs->edi;

    /* Segment registers */
    *(--ksp) = parent_regs->ds;
    *(--ksp) = parent_regs->es;
    *(--ksp) = parent_regs->fs;
    *(--ksp) = parent_regs->gs;

    child->esp = (uint32_t)ksp;

    /* Thread group: if CLONE_THREAD, don't send SIGCHLD on exit */
    /* (For now, we just mark it — the exit path already sends SIGCHLD
     *  to parent, which we'd skip for thread-group members.) */

    child->state = TASK_STATE_READY;

    irq_restore(irqf);

    /* Return child's PID to parent */
    return child->pid;
}
