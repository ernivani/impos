#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

#include <kernel/idt.h>
#include <stdint.h>

/* Priority levels */
#define PRIO_IDLE       0
#define PRIO_BACKGROUND 1
#define PRIO_NORMAL     2
#define PRIO_REALTIME   3
#define PRIO_LEVELS     4

/* Time slices (PIT ticks) per priority level */
#define SLICE_IDLE       12
#define SLICE_BACKGROUND  6
#define SLICE_NORMAL      3
#define SLICE_REALTIME    1

/* Initialize the scheduler (call after task_init) */
void sched_init(void);

/* Called from PIT handler — returns (possibly different) stack pointer */
registers_t* schedule(registers_t* regs);

/* Returns 1 if scheduler is active, 0 if still in boot/legacy mode */
int sched_is_active(void);

/* Priority management */
void sched_set_priority(int tid, uint8_t priority);
int  sched_get_priority(int tid);

#endif
