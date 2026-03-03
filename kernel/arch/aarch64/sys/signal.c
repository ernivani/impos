/*
 * aarch64 Signal subsystem — Phase 4.
 *
 * Full signal send/deliver/trampoline mechanism.
 * Signal delivery to user-mode (EL0) threads pushes a sig_context_t frame
 * onto the user stack and redirects ELR to the handler. When the handler
 * returns, the trampoline fires SYS_SIGRETURN (SVC #0 with x8=11).
 *
 * For kernel threads (EL1), signals are applied immediately (no handler
 * dispatch — just kill/stop/ignore).
 */

#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/pipe.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/io.h>
#include <string.h>
#include <stdlib.h>

/*
 * Signal trampoline: placed as return address (x30/LR) when delivering
 * a signal to an EL0 user thread. After the handler returns, this fires
 * SYS_SIGRETURN (syscall 11) to restore the pre-signal context.
 *
 * On aarch64: x8 = syscall number, SVC #0 = syscall instruction.
 * This lives in kernel .text which is mapped with PTE_USER on our
 * identity-mapped setup.
 */
__asm__(
    ".global _sig_trampoline\n"
    "_sig_trampoline:\n"
    "    mov x8, #11\n"       /* SYS_SIGRETURN */
    "    svc #0\n"
);

/* Default action table: 1 = kill, 2 = stop, 3 = continue, 0 = ignore */
#define ACT_IGNORE   0
#define ACT_KILL     1
#define ACT_STOP     2
#define ACT_CONTINUE 3

static const int sig_default_action[NSIG] = {
    [SIGINT]  = ACT_KILL,
    [SIGILL]  = ACT_KILL,
    [SIGBUS]  = ACT_KILL,
    [SIGFPE]  = ACT_KILL,
    [SIGKILL] = ACT_KILL,
    [SIGUSR1] = ACT_IGNORE,
    [SIGSEGV] = ACT_KILL,
    [SIGUSR2] = ACT_IGNORE,
    [SIGPIPE] = ACT_KILL,
    [SIGALRM] = ACT_KILL,
    [SIGTERM] = ACT_KILL,
    [SIGCHLD] = ACT_IGNORE,
    [SIGCONT] = ACT_CONTINUE,
    [SIGSTOP] = ACT_STOP,
    [SIGTSTP] = ACT_STOP,
    [SIGTTIN] = ACT_STOP,
    [SIGTTOU] = ACT_STOP,
};

void sig_init(sig_state_t *ss) {
    for (int i = 0; i < NSIG; i++)
        ss->handlers[i] = SIG_DFL;
    ss->pending = 0;
    ss->blocked = 0;
    ss->in_handler = 0;
    ss->alarm_ticks = 0;
}

/* Kill a task immediately: clean up resources, mark zombie */
static void sig_kill_task(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t || !t->active) return;

    pipe_cleanup_task(tid);
    t->killed = 1;

    /* Reparent children and wake parent */
    task_reparent_children(tid);
    int ptid = t->parent_tid;
    if (ptid >= 0 && ptid < TASK_MAX) {
        task_info_t *parent = task_get(ptid);
        if (parent && parent->state == TASK_STATE_BLOCKED && parent->wait_tid != -1) {
            if (parent->wait_tid == 0 || parent->wait_tid == tid)
                parent->state = TASK_STATE_READY;
        }
    }

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
    if (!t) {
        t = task_get_raw(tid);
        if (!t || t->state == TASK_STATE_UNUSED) return -1;
        return -1;  /* Zombie — can't send */
    }
    if (!t->killable) return -2;

    uint32_t flags = irq_save();

    /* SIGKILL: immediate kill, uncatchable, unblockable */
    if (signum == SIGKILL) {
        sig_kill_task(tid);
        irq_restore(flags);
        return 0;
    }

    /* SIGSTOP: immediate stop, uncatchable, unblockable */
    if (signum == SIGSTOP) {
        t->state = TASK_STATE_STOPPED;
        if (t->parent_tid >= 0 && t->parent_tid < TASK_MAX) {
            task_info_t *parent = task_get(t->parent_tid);
            if (parent && parent->killable)
                parent->sig.pending |= (1 << SIGCHLD);
        }
        irq_restore(flags);
        return 0;
    }

    /* SIGCONT: always delivered, resumes stopped tasks */
    if (signum == SIGCONT) {
        if (t->state == TASK_STATE_STOPPED)
            t->state = TASK_STATE_READY;
        t->sig.pending &= ~((1 << SIGSTOP) | (1 << SIGTSTP) |
                            (1 << SIGTTIN) | (1 << SIGTTOU));
        if (t->sig.handlers[SIGCONT] != SIG_DFL &&
            t->sig.handlers[SIGCONT] != SIG_IGN && t->is_user) {
            t->sig.pending |= (1 << SIGCONT);
        }
        if (t->parent_tid >= 0 && t->parent_tid < TASK_MAX) {
            task_info_t *parent = task_get(t->parent_tid);
            if (parent && parent->killable)
                parent->sig.pending |= (1 << SIGCHLD);
        }
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
        int action = sig_default_action[signum];
        if (action == ACT_KILL)
            sig_kill_task(tid);
        else if (action == ACT_STOP)
            t->state = TASK_STATE_STOPPED;
        irq_restore(flags);
        return 0;
    }

    /* Check blocked mask */
    if (t->sig.blocked & (1 << signum)) {
        t->sig.pending |= (1 << signum);
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
    if (signum == SIGKILL || signum == SIGSTOP) return SIG_DFL;

    task_info_t *t = task_get(tid);
    if (!t) return SIG_DFL;

    sig_handler_t old = t->sig.handlers[signum];
    t->sig.handlers[signum] = handler;
    return old;
}

int sig_sigprocmask(int tid, int how, uint32_t set, uint32_t *oldset) {
    task_info_t *t = task_get(tid);
    if (!t) return -1;

    uint32_t flags = irq_save();

    if (oldset)
        *oldset = t->sig.blocked;

    set &= ~((1 << SIGKILL) | (1 << SIGSTOP));

    switch (how) {
        case SIG_BLOCK:   t->sig.blocked |= set; break;
        case SIG_UNBLOCK: t->sig.blocked &= ~set; break;
        case SIG_SETMASK: t->sig.blocked = set; break;
        default:
            irq_restore(flags);
            return -1;
    }

    uint32_t deliverable = t->sig.pending & ~t->sig.blocked;
    if (deliverable && (t->state == TASK_STATE_BLOCKED || t->state == TASK_STATE_SLEEPING))
        t->state = TASK_STATE_READY;

    irq_restore(flags);
    return 0;
}

void sig_check_alarms(void) {
    for (int i = 0; i < TASK_MAX; i++) {
        task_info_t *t = task_get(i);
        if (!t) continue;
        if (t->sig.alarm_ticks == 0) continue;

        t->sig.alarm_ticks--;
        if (t->sig.alarm_ticks == 0)
            sig_send(i, SIGALRM);
    }
}

/*
 * Deliver pending signals before returning to EL0 (user mode).
 * Returns 1 if the task was killed (caller should reschedule), 0 otherwise.
 *
 * Signal frame layout on aarch64 user stack (growing downward):
 *   [LR = _sig_trampoline]  (8 bytes — set in x30)
 *   [signum]                 (8 bytes — set in x0)
 *   [sig_context_t]          (272 bytes — saved registers)
 *
 * The handler is entered with:
 *   x0 = signal number (argument)
 *   x30 (LR) = _sig_trampoline (return address → SYS_SIGRETURN)
 *   ELR = handler address
 *   SP = adjusted user stack (16-byte aligned)
 */
int sig_deliver(int tid, registers_t *regs) {
    task_info_t *t = task_get(tid);
    if (!t || !t->is_user) return 0;
    if (t->sig.in_handler) return 0;

    uint32_t deliverable = t->sig.pending & ~t->sig.blocked;
    if (deliverable == 0) return 0;

    /* Find lowest pending unblocked signal */
    int signum = -1;
    for (int i = 1; i < NSIG; i++) {
        if (deliverable & (1 << i)) {
            signum = i;
            break;
        }
    }
    if (signum < 0) return 0;

    t->sig.pending &= ~(1 << signum);

    sig_handler_t handler = t->sig.handlers[signum];

    if (handler == SIG_IGN) return 0;

    if (handler == SIG_DFL) {
        int action = sig_default_action[signum];
        if (action == ACT_KILL) {
            uint32_t flags = irq_save();
            sig_kill_task(tid);
            irq_restore(flags);
            return 1;
        }
        if (action == ACT_STOP) {
            t->state = TASK_STATE_STOPPED;
            return 0;
        }
        return 0;
    }

    /* User handler: push signal frame on user stack.
     * TODO Phase 6: virtual→physical address translation via page tables.
     * For now, identity-mapped so SP is both virtual and physical. */
    uint64_t user_sp = regs->sp;

    /* Allocate space: sig_context_t (272) + signum (8) + alignment padding */
    user_sp -= sizeof(sig_context_t);
    sig_context_t *ctx = (sig_context_t *)user_sp;

    /* Save current register state */
    for (int i = 0; i < 31; i++)
        ctx->x[i] = regs->x[i];
    ctx->sp   = regs->sp;
    ctx->elr  = regs->elr;
    ctx->spsr = regs->spsr;

    /* Push signum */
    user_sp -= 8;
    *(uint64_t *)user_sp = (uint64_t)signum;

    /* Align SP to 16 bytes (AArch64 ABI requirement) */
    user_sp &= ~(uint64_t)0xF;

    /* Redirect execution to handler */
    regs->elr  = (uint64_t)(uintptr_t)handler;
    regs->sp   = user_sp;
    regs->x[0] = (uint64_t)signum;                        /* arg0 = signal number */
    regs->x[30] = (uint64_t)(uintptr_t)_sig_trampoline;   /* LR = trampoline */

    t->sig.in_handler = 1;

    return 0;
}
