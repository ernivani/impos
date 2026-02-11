#include <kernel/syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <stdint.h>

extern volatile uint32_t pit_ticks;
#define TARGET_HZ 120

registers_t* syscall_handler(registers_t* regs) {
    switch (regs->eax) {
        case SYS_EXIT: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
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

        default:
            return regs;
    }
}
