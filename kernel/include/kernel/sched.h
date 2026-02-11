#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

#include <kernel/idt.h>

/* Initialize the scheduler (call after task_init) */
void sched_init(void);

/* Called from PIT handler — returns (possibly different) stack pointer */
registers_t* schedule(registers_t* regs);

/* Returns 1 if scheduler is active, 0 if still in boot/legacy mode */
int sched_is_active(void);

#endif
