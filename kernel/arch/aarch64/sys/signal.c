/*
 * aarch64 Signal stubs — Phase 3.
 *
 * Minimal implementations to satisfy linker for task.c/sched.c.
 * Full signal delivery (SVC-based trampoline) comes in Phase 4.
 */

#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <string.h>

void sig_init(sig_state_t *ss) {
    memset(ss, 0, sizeof(*ss));
    for (int i = 0; i < NSIG; i++)
        ss->handlers[i] = SIG_DFL;
}

int sig_send(int tid, int signum) {
    if (signum < 1 || signum >= NSIG) return -1;
    task_info_t *t = task_get(tid);
    if (!t) return -1;

    /* SIGKILL and SIGTERM kill the task immediately (no handler support yet) */
    if (signum == SIGKILL || signum == SIGTERM) {
        if (t->killable || signum == SIGKILL) {
            t->state = TASK_STATE_ZOMBIE;
            t->active = 0;
            t->killed = 1;
        }
        return 0;
    }

    /* Queue the signal for later delivery (Phase 4) */
    t->sig.pending |= (1 << signum);
    return 0;
}

int sig_send_pid(int pid, int signum) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;
    return sig_send(tid, signum);
}

sig_handler_t sig_set_handler(int tid, int signum, sig_handler_t handler) {
    task_info_t *t = task_get(tid);
    if (!t || signum < 1 || signum >= NSIG) return SIG_DFL;
    sig_handler_t old = t->sig.handlers[signum];
    t->sig.handlers[signum] = handler;
    return old;
}

int sig_deliver(int tid, registers_t *regs) {
    /* Phase 4: push signal frame to user stack */
    (void)tid; (void)regs;
    return 0;
}

void sig_check_alarms(void) {
    /* Phase 4: decrement alarm_ticks, fire SIGALRM */
}

int sig_sigprocmask(int tid, int how, uint32_t set, uint32_t *oldset) {
    task_info_t *t = task_get(tid);
    if (!t) return -1;
    if (oldset) *oldset = t->sig.blocked;
    switch (how) {
        case SIG_BLOCK:   t->sig.blocked |= set; break;
        case SIG_UNBLOCK: t->sig.blocked &= ~set; break;
        case SIG_SETMASK: t->sig.blocked = set; break;
        default: return -1;
    }
    return 0;
}
