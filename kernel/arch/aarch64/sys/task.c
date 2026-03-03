/*
 * aarch64 Task Manager — Phase 3.
 *
 * Same task table and API as i386, but thread creation builds an aarch64
 * register frame (registers_t with x0-x30, sp, elr, spsr) instead of
 * the x86 pusha/iret layout.
 *
 * Context switch works through exception return: timer IRQ → save regs →
 * schedule() picks next → restore regs → eret. User mode = EL0 (Phase 4+).
 */

#include <kernel/task.h>
#include <kernel/io.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <string.h>
#include <stdlib.h>

static task_info_t tasks[TASK_MAX];
static volatile int current_task = TASK_IDLE;
static int next_pid = 1;

void task_init(void) {
    memset(tasks, 0, sizeof(tasks));

    uintptr_t kpd = vmm_get_kernel_pagedir();

    /* Fixed tasks (cooperative, share boot stack) */
    tasks[TASK_IDLE].active = 1;
    strcpy(tasks[TASK_IDLE].name, "idle");
    tasks[TASK_IDLE].killable = 0;
    tasks[TASK_IDLE].wm_id = -1;
    tasks[TASK_IDLE].pid = next_pid++;
    tasks[TASK_IDLE].page_dir = kpd;
    tasks[TASK_IDLE].priority = PRIO_IDLE;
    tasks[TASK_IDLE].time_slice = SLICE_IDLE;
    tasks[TASK_IDLE].slice_remaining = SLICE_IDLE;
    tasks[TASK_IDLE].parent_tid = -1;
    tasks[TASK_IDLE].wait_tid = -1;
    tasks[TASK_IDLE].pgid = tasks[TASK_IDLE].pid;
    tasks[TASK_IDLE].sid = tasks[TASK_IDLE].pid;
    fd_table_init(TASK_IDLE);

    tasks[TASK_KERNEL].active = 1;
    strcpy(tasks[TASK_KERNEL].name, "kernel");
    tasks[TASK_KERNEL].killable = 0;
    tasks[TASK_KERNEL].wm_id = -1;
    tasks[TASK_KERNEL].pid = next_pid++;
    tasks[TASK_KERNEL].page_dir = kpd;
    tasks[TASK_KERNEL].priority = PRIO_NORMAL;
    tasks[TASK_KERNEL].time_slice = SLICE_NORMAL;
    tasks[TASK_KERNEL].slice_remaining = SLICE_NORMAL;
    tasks[TASK_KERNEL].parent_tid = -1;
    tasks[TASK_KERNEL].wait_tid = -1;
    tasks[TASK_KERNEL].pgid = tasks[TASK_KERNEL].pid;
    tasks[TASK_KERNEL].sid = tasks[TASK_KERNEL].pid;
    fd_table_init(TASK_KERNEL);

    tasks[TASK_WM].active = 1;
    strcpy(tasks[TASK_WM].name, "wm");
    tasks[TASK_WM].killable = 0;
    tasks[TASK_WM].wm_id = -1;
    tasks[TASK_WM].pid = next_pid++;
    tasks[TASK_WM].page_dir = kpd;
    tasks[TASK_WM].priority = PRIO_NORMAL;
    tasks[TASK_WM].time_slice = SLICE_NORMAL;
    tasks[TASK_WM].slice_remaining = SLICE_NORMAL;
    tasks[TASK_WM].parent_tid = -1;
    tasks[TASK_WM].wait_tid = -1;
    tasks[TASK_WM].pgid = tasks[TASK_WM].pid;
    tasks[TASK_WM].sid = tasks[TASK_WM].pid;
    fd_table_init(TASK_WM);

    tasks[TASK_SHELL].active = 1;
    strcpy(tasks[TASK_SHELL].name, "shell");
    tasks[TASK_SHELL].killable = 0;
    tasks[TASK_SHELL].wm_id = -1;
    tasks[TASK_SHELL].pid = next_pid++;
    tasks[TASK_SHELL].page_dir = kpd;
    tasks[TASK_SHELL].priority = PRIO_NORMAL;
    tasks[TASK_SHELL].time_slice = SLICE_NORMAL;
    tasks[TASK_SHELL].slice_remaining = SLICE_NORMAL;
    tasks[TASK_SHELL].parent_tid = -1;
    tasks[TASK_SHELL].wait_tid = -1;
    tasks[TASK_SHELL].pgid = tasks[TASK_SHELL].pid;
    tasks[TASK_SHELL].sid = tasks[TASK_SHELL].pid;
    fd_table_init(TASK_SHELL);
}

/* ═══════════════════════════════════════════════════════════════════
 * Portable task management (same as i386)
 * ═══════════════════════════════════════════════════════════════════ */

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
            tasks[i].priority = PRIO_NORMAL;
            tasks[i].time_slice = SLICE_NORMAL;
            tasks[i].slice_remaining = SLICE_NORMAL;
            tasks[i].parent_tid = current_task;
            tasks[i].wait_tid = -1;
            if (current_task >= 0 && current_task < TASK_MAX && tasks[current_task].active) {
                tasks[i].pgid = tasks[current_task].pgid;
                tasks[i].sid = tasks[current_task].sid;
            } else {
                tasks[i].pgid = tasks[i].pid;
                tasks[i].sid = tasks[i].pid;
            }
            fd_table_init(i);
            irq_restore(flags);
            return i;
        }
    }
    irq_restore(flags);
    return -1;
}

int task_assign_pid(int tid) {
    if (tid < 0 || tid >= TASK_MAX) return -1;
    tasks[tid].pid = next_pid++;
    return tasks[tid].pid;
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

void task_tick(void) {
    int ct = current_task;
    if (ct >= 0 && ct < TASK_MAX && tasks[ct].active)
        tasks[ct].ticks++;
}

void task_add_gpu_ticks(int tid, uint32_t ticks) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active)
        tasks[tid].gpu_ticks += ticks;
}

void task_sample(void) {
    uint32_t total = 0;
    uint32_t gpu_total = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].active) {
            total += tasks[i].ticks;
            gpu_total += tasks[i].gpu_ticks;
        }
    }
    if (total == 0) total = 1;
    if (gpu_total == 0) gpu_total = 1;

    for (int i = 0; i < TASK_MAX; i++) {
        if (!tasks[i].active) continue;
        tasks[i].total_ticks += tasks[i].ticks;
        tasks[i].prev_ticks = tasks[i].ticks;
        tasks[i].sample_total = total;
        tasks[i].ticks = 0;
        tasks[i].gpu_prev_ticks = tasks[i].gpu_ticks;
        tasks[i].gpu_sample_total = gpu_total;
        tasks[i].gpu_ticks = 0;

        if (tasks[i].killable) {
            uint32_t cpu_pct = tasks[i].prev_ticks * 100 / total;
            if (cpu_pct > 90) {
                tasks[i].hog_count++;
                if (tasks[i].hog_count >= 5) {
                    tasks[i].killed = 1;
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
    return sig_send_pid(pid, SIGKILL);
}

void task_set_name(int tid, const char *name) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active) {
        strncpy(tasks[tid].name, name, 31);
        tasks[tid].name[31] = '\0';
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * aarch64 Preemptive Thread Creation
 *
 * Stack layout for a new thread (registers_t frame):
 *   The scheduler restores context by returning a registers_t* which
 *   the exception_stubs.S RESTORE code uses to eret into the thread.
 *
 *   registers_t {
 *     x[0-30]: all zero (x30=LR=task_exit for safety)
 *     sp:      top of allocated stack
 *     elr:     entry point (thread function address)
 *     spsr:    EL1h + IRQ enabled (0x3C5 → DAIF=0, EL=1, SP=SPx)
 *     _pad:    0
 *   }
 *
 *   On the stack we also place a trampoline return address above the
 *   frame so that if the thread function returns, it lands in task_exit.
 * ═══════════════════════════════════════════════════════════════════ */

/* SPSR for EL1h with all interrupts enabled (DAIF clear) */
#define SPSR_EL1H  0x00000005  /* EL1h, AArch64, all interrupts unmasked */

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

    tasks[tid].active = 1;
    tasks[tid].state = TASK_STATE_BLOCKED;

    irq_restore(flags);
    void *stack = malloc(TASK_STACK_SIZE);
    if (!stack) {
        tasks[tid].active = 0;
        tasks[tid].state = TASK_STATE_UNUSED;
        return -1;
    }
    memset(stack, 0, TASK_STACK_SIZE);
    flags = irq_save();

    /* Build initial register frame on the thread's stack */
    uint8_t *stack_top = (uint8_t *)stack + TASK_STACK_SIZE;

    /* The exception_stubs.S restore code expects:
     *   sp → registers_t frame (288 bytes)
     *   sp + 288 → 16 bytes (trampoline save area, zeroed)
     * We need sp + FRAME_SIZE + 16 to be the original SP. */

    /* Leave 16 bytes for the trampoline save area at the top */
    stack_top -= 16;
    memset(stack_top, 0, 16);

    /* Build registers_t frame below */
    stack_top -= sizeof(registers_t);
    registers_t *frame = (registers_t *)stack_top;
    memset(frame, 0, sizeof(registers_t));

    frame->x[30] = (uint64_t)(uintptr_t)task_exit;  /* LR: safety net if entry() returns */
    frame->sp    = (uint64_t)(uintptr_t)((uint8_t *)stack + TASK_STACK_SIZE);
    frame->elr   = (uint64_t)(uintptr_t)entry;       /* Entry point */
    frame->spsr  = SPSR_EL1H;                         /* EL1h, interrupts enabled */

    tasks[tid].active = 1;
    strncpy(tasks[tid].name, name, 31);
    tasks[tid].name[31] = '\0';
    tasks[tid].killable = killable;
    tasks[tid].wm_id = -1;
    tasks[tid].pid = next_pid++;
    tasks[tid].stack_base = stack;
    tasks[tid].stack_size = TASK_STACK_SIZE;
    tasks[tid].esp = (uintptr_t)frame;
    tasks[tid].page_dir = vmm_get_kernel_pagedir();
    tasks[tid].priority = PRIO_NORMAL;
    tasks[tid].time_slice = SLICE_NORMAL;
    tasks[tid].slice_remaining = SLICE_NORMAL;
    tasks[tid].parent_tid = current_task;
    tasks[tid].wait_tid = -1;
    if (current_task >= 0 && current_task < TASK_MAX && tasks[current_task].active) {
        tasks[tid].pgid = tasks[current_task].pgid;
        tasks[tid].sid = tasks[current_task].sid;
    } else {
        tasks[tid].pgid = tasks[tid].pid;
        tasks[tid].sid = tasks[tid].pid;
    }
    sig_init(&tasks[tid].sig);
    fd_table_init(tid);
    tasks[tid].state = TASK_STATE_READY;

    irq_restore(flags);
    return tid;
}

int task_create_user_thread(const char *name, void (*entry)(void), int killable) {
    /* Phase 4: EL0 user threads — stub for now */
    (void)name; (void)entry; (void)killable;
    return -1;
}

void task_yield(void) {
    /* SVC #0 with x8=SYS_YIELD (1) triggers syscall_handler → schedule() */
    register uint64_t x8 __asm__("x8") = SYS_YIELD;
    __asm__ volatile("svc #0" :: "r"(x8) : "memory");
}

void task_exit(void) {
    task_exit_code(0);
}

void task_exit_code(int code) {
    uint32_t flags = irq_save();

    int tid = current_task;
    if (tid >= 0 && tid < TASK_MAX) {
        tasks[tid].exit_code = code;
        tasks[tid].state = TASK_STATE_ZOMBIE;
        tasks[tid].active = 0;

        task_reparent_children(tid);

        int ptid = tasks[tid].parent_tid;
        if (ptid >= 0 && ptid < TASK_MAX) {
            task_info_t *parent = task_get(ptid);
            if (parent && parent->state == TASK_STATE_BLOCKED && parent->wait_tid != -1) {
                if (parent->wait_tid == 0 || parent->wait_tid == tid)
                    parent->state = TASK_STATE_READY;
            }
        }
    }

    irq_restore(flags);

    /* Yield to scheduler — we're now a zombie, scheduler won't pick us */
    while (1) __asm__ volatile("wfi");
}

void task_block(int tid) {
    uint32_t flags = irq_save();
    task_info_t *t = task_get(tid);
    if (t) t->state = TASK_STATE_BLOCKED;
    irq_restore(flags);
}

void task_unblock(int tid) {
    uint32_t flags = irq_save();
    task_info_t *t = task_get(tid);
    if (t && t->state == TASK_STATE_BLOCKED)
        t->state = TASK_STATE_READY;
    irq_restore(flags);
}

void task_reparent_children(int dying_tid) {
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].parent_tid == dying_tid)
            tasks[i].parent_tid = TASK_KERNEL;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Process groups, sessions, waitpid — portable
 * ═══════════════════════════════════════════════════════════════════ */

int task_setpgid(int pid, int pgid) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;
    if (pgid == 0) pgid = tasks[tid].pid;
    tasks[tid].pgid = pgid;
    return 0;
}

int task_getpgid(int pid) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;
    return tasks[tid].pgid;
}

int task_setsid(int tid) {
    if (tid < 0 || tid >= TASK_MAX || !tasks[tid].active) return -1;
    tasks[tid].sid = tasks[tid].pid;
    tasks[tid].pgid = tasks[tid].pid;
    return tasks[tid].sid;
}

int sig_send_group(int pgid, int signum) {
    int sent = 0;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].active && tasks[i].pgid == pgid) {
            sig_send(i, signum);
            sent++;
        }
    }
    return sent > 0 ? 0 : -1;
}

int sys_waitpid(int pid, int *wstatus, int options) {
    int tid = task_get_current();

    while (1) {
        int found_child = 0;
        for (int i = 0; i < TASK_MAX; i++) {
            task_info_t *child = task_get_raw(i);
            if (!child) continue;
            if (child->parent_tid != tid) continue;
            if (pid > 0 && child->pid != pid) continue;
            if (pid == 0 && child->pgid != tasks[tid].pgid) continue;

            found_child = 1;

            if (child->state == TASK_STATE_ZOMBIE) {
                int cpid = child->pid;
                if (wstatus) *wstatus = (child->exit_code & 0xFF) << 8;
                /* Clean up slot */
                child->state = TASK_STATE_UNUSED;
                child->active = 0;
                return cpid;
            }
        }

        if (!found_child) return -1;
        if (options & WNOHANG) return 0;

        /* Block until a child exits */
        tasks[tid].wait_tid = (pid > 0) ? task_find_by_pid(pid) : 0;
        tasks[tid].state = TASK_STATE_BLOCKED;
        __asm__ volatile("wfi");  /* Will be preempted by timer */
    }
}

/* Clone/fork stub — Phase 6 */
int sys_clone(uint32_t clone_flags, uint32_t child_stack, registers_t *parent_regs) {
    (void)clone_flags; (void)child_stack; (void)parent_regs;
    return -1;
}

/* Futex stub — Phase 6 */
int sys_futex(uint32_t *uaddr, int op, uint32_t val) {
    (void)uaddr; (void)op; (void)val;
    return -1;
}
