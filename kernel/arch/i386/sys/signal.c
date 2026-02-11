#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/pipe.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/io.h>
#include <stdlib.h>

/*
 * Signal trampoline: placed as return address on user stack.
 * When signal handler returns, this fires SYS_SIGRETURN (11).
 * No compiler prologue — raw assembly in .text, accessible from ring 3
 * because all kernel pages have PTE_USER.
 */
__asm__(
    ".global _sig_trampoline\n"
    "_sig_trampoline:\n"
    "    mov $11, %eax\n"
    "    int $0x80\n"
);

/* Default action table: 1 = kill, 0 = ignore */
static const int sig_default_kill[NSIG] = {
    [SIGINT]  = 1,
    [SIGKILL] = 1,
    [SIGUSR1] = 0,
    [SIGUSR2] = 0,
    [SIGPIPE] = 1,
    [SIGTERM] = 1,
};

void sig_init(sig_state_t *ss) {
    for (int i = 0; i < NSIG; i++)
        ss->handlers[i] = SIG_DFL;
    ss->pending = 0;
    ss->in_handler = 0;
}

/* Kill a task immediately: clean up pipes, free stacks/pages, mark zombie */
static void sig_kill_task(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t || !t->active) return;

    pipe_cleanup_task(tid);
    t->killed = 1;

    if (t->is_user) {
        t->state = TASK_STATE_ZOMBIE;
        t->active = 0;
        if (t->kernel_stack) {
            pmm_free_frame(t->kernel_stack);
            t->kernel_stack = 0;
        }
        if (t->user_stack) {
            pmm_free_frame(t->user_stack);
            t->user_stack = 0;
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
        t->state = TASK_STATE_ZOMBIE;
        t->active = 0;
        free(t->stack_base);
        t->stack_base = 0;
    }
}

int sig_send(int tid, int signum) {
    if (signum < 0 || signum >= NSIG) return -1;

    task_info_t *t = task_get(tid);
    if (!t) return -1;
    if (!t->killable) return -2;

    uint32_t flags = irq_save();

    /* SIGKILL: immediate kill, uncatchable */
    if (signum == SIGKILL) {
        sig_kill_task(tid);
        irq_restore(flags);
        return 0;
    }

    /* For non-user threads, apply action directly */
    if (!t->is_user) {
        sig_handler_t h = t->sig.handlers[signum];
        if (h == SIG_IGN) {
            irq_restore(flags);
            return 0;
        }
        /* Non-user threads can't run user handlers; apply default */
        if (sig_default_kill[signum])
            sig_kill_task(tid);
        irq_restore(flags);
        return 0;
    }

    /* User thread: set pending, wake if blocked/sleeping */
    t->sig.pending |= (1 << signum);
    if (t->state == TASK_STATE_BLOCKED || t->state == TASK_STATE_SLEEPING)
        t->state = TASK_STATE_READY;

    irq_restore(flags);
    return 0;
}

int sig_send_pid(int pid, int signum) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;
    return sig_send(tid, signum);
}

sig_handler_t sig_set_handler(int tid, int signum, sig_handler_t handler) {
    if (signum < 0 || signum >= NSIG) return SIG_DFL;
    if (signum == SIGKILL) return SIG_DFL;  /* SIGKILL uncatchable */

    task_info_t *t = task_get(tid);
    if (!t) return SIG_DFL;

    sig_handler_t old = t->sig.handlers[signum];
    t->sig.handlers[signum] = handler;
    return old;
}

/*
 * Deliver pending signals before returning to ring 3.
 * Returns 1 if the task was killed (caller should reschedule), 0 otherwise.
 */
int sig_deliver(int tid, registers_t *regs) {
    task_info_t *t = task_get(tid);
    if (!t || !t->is_user) return 0;
    if (t->sig.in_handler) return 0;
    if (t->sig.pending == 0) return 0;

    /* Find lowest pending signal */
    int signum = -1;
    for (int i = 1; i < NSIG; i++) {
        if (t->sig.pending & (1 << i)) {
            signum = i;
            break;
        }
    }
    if (signum < 0) return 0;

    /* Clear pending bit */
    t->sig.pending &= ~(1 << signum);

    sig_handler_t handler = t->sig.handlers[signum];

    /* SIG_IGN: skip */
    if (handler == SIG_IGN) return 0;

    /* SIG_DFL: apply default action */
    if (handler == SIG_DFL) {
        if (sig_default_kill[signum]) {
            uint32_t flags = irq_save();
            sig_kill_task(tid);
            irq_restore(flags);
            return 1;  /* task killed */
        }
        return 0;  /* default = ignore */
    }

    /* User handler: manipulate user stack for signal delivery */
    uint32_t user_esp = regs->useresp;

    /* Convert virtual ESP to physical pointer */
    uint32_t offset = user_esp - USER_SPACE_BASE;
    if (offset > PAGE_SIZE) return 0;  /* out of range, skip */

    uint32_t *phys_sp = (uint32_t *)(t->user_stack + offset);

    /* Push sig_context_t (64 bytes = 16 uint32_t) */
    phys_sp -= 16;
    sig_context_t *ctx = (sig_context_t *)phys_sp;
    ctx->eip    = regs->eip;
    ctx->cs     = regs->cs;
    ctx->eflags = regs->eflags;
    ctx->esp    = regs->useresp;
    ctx->ss     = regs->ss;
    ctx->eax    = regs->eax;
    ctx->ecx    = regs->ecx;
    ctx->edx    = regs->edx;
    ctx->ebx    = regs->ebx;
    ctx->esi    = regs->esi;
    ctx->edi    = regs->edi;
    ctx->ebp    = regs->ebp;
    ctx->ds     = regs->ds;
    ctx->es     = regs->es;
    ctx->fs     = regs->fs;
    ctx->gs     = regs->gs;

    /* Push signal number (handler argument) */
    *(--phys_sp) = (uint32_t)signum;

    /* Push trampoline address (return address for handler) */
    *(--phys_sp) = (uint32_t)_sig_trampoline;

    /* Calculate new virtual ESP */
    uint32_t new_offset = (uint32_t)phys_sp - t->user_stack;
    uint32_t new_virtual_esp = USER_SPACE_BASE + new_offset;

    /* Redirect execution to handler */
    regs->eip = (uint32_t)handler;
    regs->useresp = new_virtual_esp;

    t->sig.in_handler = 1;

    return 0;
}
