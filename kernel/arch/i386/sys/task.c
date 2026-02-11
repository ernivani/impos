#include <kernel/task.h>
#include <kernel/io.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pmm.h>
#include <string.h>
#include <stdlib.h>

static task_info_t tasks[TASK_MAX];
static volatile int current_task = TASK_IDLE;
static int next_pid = 1;

void task_init(void) {
    memset(tasks, 0, sizeof(tasks));

    /* Fixed tasks */
    tasks[TASK_IDLE].active = 1;
    strcpy(tasks[TASK_IDLE].name, "idle");
    tasks[TASK_IDLE].killable = 0;
    tasks[TASK_IDLE].wm_id = -1;
    tasks[TASK_IDLE].pid = next_pid++;

    tasks[TASK_KERNEL].active = 1;
    strcpy(tasks[TASK_KERNEL].name, "kernel");
    tasks[TASK_KERNEL].killable = 0;
    tasks[TASK_KERNEL].wm_id = -1;
    tasks[TASK_KERNEL].pid = next_pid++;

    tasks[TASK_WM].active = 1;
    strcpy(tasks[TASK_WM].name, "wm");
    tasks[TASK_WM].killable = 0;
    tasks[TASK_WM].wm_id = -1;
    tasks[TASK_WM].pid = next_pid++;

    tasks[TASK_SHELL].active = 1;
    strcpy(tasks[TASK_SHELL].name, "shell");
    tasks[TASK_SHELL].killable = 0;
    tasks[TASK_SHELL].wm_id = -1;
    tasks[TASK_SHELL].pid = next_pid++;
}

int task_register(const char *name, int killable, int wm_id) {
    uint32_t flags = irq_save();
    for (int i = 4; i < TASK_MAX; i++) {
        if (!tasks[i].active) {
            memset(&tasks[i], 0, sizeof(task_info_t));
            tasks[i].active = 1;
            strncpy(tasks[i].name, name, 31);
            tasks[i].name[31] = '\0';
            tasks[i].killable = killable;
            tasks[i].wm_id = wm_id;
            tasks[i].pid = next_pid++;
            tasks[i].state = TASK_STATE_READY;
            irq_restore(flags);
            return i;
        }
    }
    irq_restore(flags);
    return -1;
}

void task_unregister(int tid) {
    uint32_t flags = irq_save();
    if (tid >= 0 && tid < TASK_MAX) {
        tasks[tid].active = 0;
        tasks[tid].state = TASK_STATE_UNUSED;
        if (current_task == tid)
            current_task = TASK_IDLE;
    }
    irq_restore(flags);
}

void task_set_current(int tid) {
    if (tid >= 0 && tid < TASK_MAX)
        current_task = tid;
}

int task_get_current(void) {
    return current_task;
}

/* Called from PIT IRQ handler — must be very fast */
void task_tick(void) {
    int ct = current_task;
    if (ct >= 0 && ct < TASK_MAX && tasks[ct].active)
        tasks[ct].ticks++;
}

/* Called once per second from PIT handler */
void task_sample(void) {
    uint32_t total = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].active)
            total += tasks[i].ticks;
    }
    if (total == 0) total = 1;

    for (int i = 0; i < TASK_MAX; i++) {
        if (!tasks[i].active) continue;
        tasks[i].total_ticks += tasks[i].ticks;
        tasks[i].prev_ticks = tasks[i].ticks;
        tasks[i].sample_total = total;
        tasks[i].ticks = 0;

        /* Watchdog: check killable tasks */
        if (tasks[i].killable) {
            uint32_t cpu_pct = tasks[i].prev_ticks * 100 / total;
            if (cpu_pct > 90) {
                tasks[i].hog_count++;
                if (tasks[i].hog_count >= 5) {
                    tasks[i].killed = 1;
                    /* For preemptive threads, mark as zombie so scheduler stops them */
                    if (tasks[i].stack_base || tasks[i].is_user) {
                        tasks[i].state = TASK_STATE_ZOMBIE;
                        tasks[i].active = 0;
                    }
                }
            } else {
                tasks[i].hog_count = 0;
            }
        }
    }
}

task_info_t *task_get(int tid) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active)
        return &tasks[tid];
    return 0;
}

task_info_t *task_get_raw(int tid) {
    if (tid >= 0 && tid < TASK_MAX)
        return &tasks[tid];
    return 0;
}

int task_count(void) {
    int n = 0;
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].active) n++;
    return n;
}

void task_set_mem(int tid, int kb) {
    if (tid >= 0 && tid < TASK_MAX)
        tasks[tid].mem_kb = kb;
}

int task_check_killed(int tid) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].killed) {
        tasks[tid].killed = 0;
        return 1;
    }
    return 0;
}

int task_find_by_pid(int pid) {
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].active && tasks[i].pid == pid)
            return i;
    }
    return -1;
}

int task_get_pid(int tid) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active)
        return tasks[tid].pid;
    return -1;
}

int task_kill_by_pid(int pid) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;
    if (!tasks[tid].killable) return -2;

    uint32_t flags = irq_save();
    tasks[tid].killed = 1;

    /* If this is a preemptive thread (has its own stack), kill it immediately */
    if (tasks[tid].is_user) {
        tasks[tid].state = TASK_STATE_ZOMBIE;
        tasks[tid].active = 0;
        if (tasks[tid].kernel_stack) {
            pmm_free_frame(tasks[tid].kernel_stack);
            tasks[tid].kernel_stack = 0;
        }
        if (tasks[tid].user_stack) {
            pmm_free_frame(tasks[tid].user_stack);
            tasks[tid].user_stack = 0;
        }
    } else if (tasks[tid].stack_base) {
        tasks[tid].state = TASK_STATE_ZOMBIE;
        tasks[tid].active = 0;
        free(tasks[tid].stack_base);
        tasks[tid].stack_base = 0;
    }

    irq_restore(flags);
    return 0;
}

void task_set_name(int tid, const char *name) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active) {
        strncpy(tasks[tid].name, name, 31);
        tasks[tid].name[31] = '\0';
    }
}

/* ═══ Preemptive multitasking ═══════════════════════════════════ */

/*
 * Stack layout for a newly created thread (grows downward):
 *   [top of allocated stack]
 *   ...
 *   GS  (0x10)        ← ESP points here after setup
 *   FS  (0x10)
 *   ES  (0x10)
 *   DS  (0x10)
 *   EDI (0)           ← pusha block
 *   ESI (0)
 *   EBP (0)
 *   ESP (ignored by popa)
 *   EBX (0)
 *   EDX (0)
 *   ECX (0)
 *   EAX (0)
 *   int_no (0)
 *   err_code (0)
 *   EIP (entry)       ← iret restores from here
 *   CS  (0x08)
 *   EFLAGS (0x202)    ← IF=1
 *   [bottom/start of stack allocation]
 */

int task_create_thread(const char *name, void (*entry)(void), int killable) {
    uint32_t flags = irq_save();

    int tid = -1;
    for (int i = 4; i < TASK_MAX; i++) {
        if (!tasks[i].active) {
            tid = i;
            break;
        }
    }
    if (tid < 0) {
        irq_restore(flags);
        return -1;
    }

    /* Reserve the slot immediately so no one else takes it */
    tasks[tid].active = 1;
    tasks[tid].state = TASK_STATE_BLOCKED; /* Not ready yet */

    /* Allocate stack with interrupts enabled */
    irq_restore(flags);
    uint32_t *stack = (uint32_t *)malloc(TASK_STACK_SIZE);
    if (!stack) {
        tasks[tid].active = 0;
        tasks[tid].state = TASK_STATE_UNUSED;
        return -1;
    }
    memset(stack, 0, TASK_STACK_SIZE);
    flags = irq_save();

    /* Initialize task slot (keeping active=1 from reservation) */
    tasks[tid].active = 1;
    strncpy(tasks[tid].name, name, 31);
    tasks[tid].name[31] = '\0';
    tasks[tid].killable = killable;
    tasks[tid].wm_id = -1;
    tasks[tid].pid = next_pid++;
    tasks[tid].stack_base = stack;
    tasks[tid].stack_size = TASK_STACK_SIZE;

    /* Set up initial stack frame to look like an interrupt context */
    uint32_t *sp = (uint32_t *)((uint8_t *)stack + TASK_STACK_SIZE);

    /* Return address for when entry() returns — safety net */
    *(--sp) = (uint32_t)task_exit;

    /* iret frame (pushed by CPU) */
    *(--sp) = 0x202;           /* EFLAGS: IF=1 */
    *(--sp) = 0x08;            /* CS: kernel code segment */
    *(--sp) = (uint32_t)entry; /* EIP: thread entry point */

    /* Error code and interrupt number (pushed by ISR stub) */
    *(--sp) = 0;               /* err_code */
    *(--sp) = 0;               /* int_no */

    /* pusha block (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI) */
    *(--sp) = 0;               /* EAX */
    *(--sp) = 0;               /* ECX */
    *(--sp) = 0;               /* EDX */
    *(--sp) = 0;               /* EBX */
    *(--sp) = 0;               /* ESP (ignored by popa) */
    *(--sp) = 0;               /* EBP */
    *(--sp) = 0;               /* ESI */
    *(--sp) = 0;               /* EDI */

    /* Segment registers (pushed by isr_common) */
    *(--sp) = 0x10;            /* DS */
    *(--sp) = 0x10;            /* ES */
    *(--sp) = 0x10;            /* FS */
    *(--sp) = 0x10;            /* GS */

    tasks[tid].esp = (uint32_t)sp;
    tasks[tid].state = TASK_STATE_READY;

    irq_restore(flags);
    return tid;
}

void task_yield(void) {
    /* INT 0x80 with EAX=SYS_YIELD: syscall gate triggers scheduler */
    __asm__ volatile("mov %0, %%eax\n\tint $0x80" : : "i"(SYS_YIELD) : "eax");
}

void task_exit(void) {
    uint32_t flags = irq_save();

    int tid = current_task;
    if (tid >= 0 && tid < TASK_MAX) {
        /* Mark as zombie — scheduler will free the stack after switching away */
        tasks[tid].state = TASK_STATE_ZOMBIE;
        tasks[tid].active = 0;
    }

    irq_restore(flags);

    /* Yield to scheduler — we'll never return since state is ZOMBIE */
    task_yield();

    /* Should never reach here */
    while (1) __asm__ volatile("hlt");
}

void task_block(int tid) {
    uint32_t flags = irq_save();
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active)
        tasks[tid].state = TASK_STATE_BLOCKED;
    irq_restore(flags);
}

void task_unblock(int tid) {
    uint32_t flags = irq_save();
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active)
        tasks[tid].state = TASK_STATE_READY;
    irq_restore(flags);
}

/* ═══ Ring 3 user threads ═══════════════════════════════════════ */

/* Trampoline placed as return address on user stack.
 * When user entry() returns, this fires SYS_EXIT. */
static void user_exit_trampoline(void) {
    __asm__ volatile (
        "mov %0, %%eax\n\t"
        "int $0x80\n\t"
        : : "i"(SYS_EXIT) : "eax"
    );
    while (1);  /* never reached */
}

/*
 * Ring 3 user thread stack layout:
 *
 * KERNEL STACK (4KB, PMM):             USER STACK (4KB, PMM):
 *   kern+4096 → (kernel_esp/TSS.esp0)   user+4096 →
 *     SS       = 0x23                      &user_exit_trampoline
 *     UserESP  ──────────────────────→   usp (user+4096-4)
 *     EFLAGS   = 0x202
 *     CS       = 0x1B
 *     EIP      = entry
 *     err_code = 0
 *     int_no   = 0
 *     pusha    (all 0)
 *     DS=0x23, ES=0x23, FS=0x23
 *   task->esp → GS=0x23
 */
int task_create_user_thread(const char *name, void (*entry)(void), int killable) {
    uint32_t flags = irq_save();

    int tid = -1;
    for (int i = 4; i < TASK_MAX; i++) {
        if (!tasks[i].active) {
            tid = i;
            break;
        }
    }
    if (tid < 0) {
        irq_restore(flags);
        return -1;
    }

    /* Reserve the slot */
    memset(&tasks[tid], 0, sizeof(task_info_t));
    tasks[tid].active = 1;
    tasks[tid].state = TASK_STATE_BLOCKED;

    irq_restore(flags);

    /* Allocate kernel stack (4KB) and user stack (4KB) from PMM */
    uint32_t kstack = pmm_alloc_frame();
    uint32_t ustack = pmm_alloc_frame();
    if (!kstack || !ustack) {
        if (kstack) pmm_free_frame(kstack);
        if (ustack) pmm_free_frame(ustack);
        tasks[tid].active = 0;
        tasks[tid].state = TASK_STATE_UNUSED;
        return -1;
    }
    memset((void *)kstack, 0, 4096);
    memset((void *)ustack, 0, 4096);

    flags = irq_save();

    /* Set up user stack: push exit trampoline as return address */
    uint32_t *usp = (uint32_t *)(ustack + 4096);
    *(--usp) = (uint32_t)user_exit_trampoline;
    uint32_t user_esp = (uint32_t)usp;

    /* Set up kernel stack with ring 3 iret frame */
    uint32_t *ksp = (uint32_t *)(kstack + 4096);

    /* Ring 3 iret frame: SS, UserESP, EFLAGS, CS, EIP */
    *(--ksp) = 0x23;            /* SS: user data segment */
    *(--ksp) = user_esp;        /* UserESP */
    *(--ksp) = 0x202;           /* EFLAGS: IF=1 */
    *(--ksp) = 0x1B;            /* CS: user code segment */
    *(--ksp) = (uint32_t)entry; /* EIP: thread entry point */

    /* ISR stub pushes */
    *(--ksp) = 0;               /* err_code */
    *(--ksp) = 0;               /* int_no */

    /* pusha block */
    *(--ksp) = 0;               /* EAX */
    *(--ksp) = 0;               /* ECX */
    *(--ksp) = 0;               /* EDX */
    *(--ksp) = 0;               /* EBX */
    *(--ksp) = 0;               /* ESP (ignored by popa) */
    *(--ksp) = 0;               /* EBP */
    *(--ksp) = 0;               /* ESI */
    *(--ksp) = 0;               /* EDI */

    /* Segment registers: user data selector */
    *(--ksp) = 0x23;            /* DS */
    *(--ksp) = 0x23;            /* ES */
    *(--ksp) = 0x23;            /* FS */
    *(--ksp) = 0x23;            /* GS */

    /* Initialize task */
    strncpy(tasks[tid].name, name, 31);
    tasks[tid].name[31] = '\0';
    tasks[tid].killable = killable;
    tasks[tid].wm_id = -1;
    tasks[tid].pid = next_pid++;
    tasks[tid].is_user = 1;
    tasks[tid].kernel_stack = kstack;
    tasks[tid].user_stack = ustack;
    tasks[tid].kernel_esp = kstack + 4096;  /* top of kernel stack → TSS.esp0 */
    tasks[tid].esp = (uint32_t)ksp;
    tasks[tid].state = TASK_STATE_READY;

    irq_restore(flags);
    return tid;
}
