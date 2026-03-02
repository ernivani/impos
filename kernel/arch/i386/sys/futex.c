/*
 * futex.c — Fast userspace mutex support for ImposOS
 *
 * Futexes allow userspace threads to synchronize without syscalls in the
 * uncontended case. Only when contention occurs does the kernel get involved:
 *   FUTEX_WAIT: if *uaddr == expected_val, put thread to sleep
 *   FUTEX_WAKE: wake up to N threads sleeping on uaddr
 *
 * Since ImposOS is identity-mapped (first 256MB), the userspace address
 * and kernel address are the same — no address translation needed.
 */

#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/linux_syscall.h>
#include <stdint.h>

/* Futex operations (from Linux) */
#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_PRIVATE_FLAG  128
#define FUTEX_WAIT_PRIVATE  (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE  (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)

/* Simple futex wait queue: hash table of sleeping threads.
 * Each entry records the address being waited on and the sleeping tid. */
#define FUTEX_HASH_SIZE  64
#define FUTEX_MAX_WAITERS 64

typedef struct {
    uint32_t uaddr;     /* address being waited on (0 = unused slot) */
    int      tid;       /* task slot index */
} futex_waiter_t;

static futex_waiter_t waiters[FUTEX_MAX_WAITERS];

int sys_futex(uint32_t *uaddr, int op, uint32_t val) {
    int masked_op = op & ~FUTEX_PRIVATE_FLAG;

    switch (masked_op) {
        case FUTEX_WAIT: {
            uint32_t irqf = irq_save();

            /* Atomically check if *uaddr still equals val */
            if (*uaddr != val) {
                irq_restore(irqf);
                return -LINUX_EAGAIN;  /* value changed, don't sleep */
            }

            /* Find a free waiter slot */
            int slot = -1;
            for (int i = 0; i < FUTEX_MAX_WAITERS; i++) {
                if (waiters[i].uaddr == 0) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                irq_restore(irqf);
                return -LINUX_EAGAIN;  /* no room */
            }

            int tid = task_get_current();
            waiters[slot].uaddr = (uint32_t)uaddr;
            waiters[slot].tid = tid;

            /* Block the calling thread */
            task_block(tid);

            irq_restore(irqf);

            /* Yield to scheduler — we'll resume when someone calls FUTEX_WAKE */
            task_yield();

            return 0;
        }

        case FUTEX_WAKE: {
            uint32_t irqf = irq_save();
            int woken = 0;

            /* Wake up to 'val' threads waiting on this address */
            for (int i = 0; i < FUTEX_MAX_WAITERS && (uint32_t)woken < val; i++) {
                if (waiters[i].uaddr == (uint32_t)uaddr) {
                    task_unblock(waiters[i].tid);
                    waiters[i].uaddr = 0;
                    waiters[i].tid = 0;
                    woken++;
                }
            }

            irq_restore(irqf);
            return woken;
        }

        default:
            return -LINUX_ENOSYS;
    }
}
