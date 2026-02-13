#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>

#define TASK_MAX        32
#define TASK_IDLE        0
#define TASK_KERNEL      1
#define TASK_WM          2
#define TASK_SHELL       3
/* Dynamic tasks: 4+ */

#define TASK_STACK_SIZE  8192  /* 8 KB per thread stack */

typedef enum {
    TASK_STATE_UNUSED = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_BLOCKED,
    TASK_STATE_SLEEPING,
    TASK_STATE_ZOMBIE
} task_state_t;

typedef struct {
    int      active;
    char     name[32];
    uint32_t ticks;           /* ticks in current sample window */
    uint32_t prev_ticks;      /* ticks from last completed window */
    uint32_t sample_total;    /* total ticks in last window */
    int      killable;        /* watchdog can terminate */
    int      wm_id;           /* -1 if not a window */
    int      mem_kb;
    int      killed;          /* set by watchdog or kill command */
    int      hog_count;       /* consecutive seconds at >90% */
    int      pid;             /* monotonically increasing PID */
    uint32_t total_ticks;     /* cumulative CPU ticks (for TIME+) */
    uint32_t gpu_ticks;       /* GPU ticks in current sample window */
    uint32_t gpu_prev_ticks;  /* GPU ticks from last completed window */
    uint32_t gpu_sample_total;/* total GPU ticks in last window */

    /* Preemptive multitasking fields */
    task_state_t state;
    uint32_t     esp;         /* saved stack pointer */
    uint32_t*    stack_base;  /* malloc'd stack (NULL for boot task) */
    uint32_t     stack_size;  /* stack size in bytes */
    uint32_t     sleep_until; /* PIT tick to wake at (for SLEEPING) */

    /* Ring 3 user thread fields */
    int          is_user;       /* 1 if ring 3 thread */
    uint32_t     kernel_esp;    /* top of kernel stack (→ TSS.esp0) */
    uint32_t     kernel_stack;  /* PMM-allocated kernel stack phys addr */
    uint32_t     user_stack;    /* PMM-allocated user stack phys addr */

    /* Per-process page directory */
    uint32_t     page_dir;        /* page directory phys addr (kernel PD for ring 0) */
    uint32_t     user_page_table; /* PMM page table for user space (for cleanup) */

    /* Per-task signal state */
    sig_state_t  sig;

    /* Shared memory attachment bitmask (1 bit per SHM region) */
    uint16_t     shm_attached;

    /* Per-task file descriptor table */
    fd_entry_t   fds[MAX_FDS];

    /* Win32 PE task fields */
    uint32_t     tib;          /* pointer to WIN32_TEB (0 if not a PE task) */
    int          is_pe;        /* 1 if this is a PE executable task */

    /* ELF Linux compat fields */
    int          is_elf;         /* 1 if Linux ELF process */
    uint32_t     brk_start;      /* initial program break (end of loaded segments) */
    uint32_t     brk_current;    /* current program break */
    uint32_t     mmap_next;      /* next available VA for anonymous mmap */
    uint32_t     tls_base;       /* TLS base address (set by set_thread_area) */

    /* ELF memory tracking for cleanup */
    uint32_t     elf_frames[64]; /* PMM frames allocated for ELF segments + brk + mmap */
    uint8_t      num_elf_frames; /* count of allocated frames */
} task_info_t;

void        task_init(void);
int         task_register(const char *name, int killable, int wm_id);
void        task_unregister(int tid);
void        task_set_current(int tid);
int         task_get_current(void);
void        task_tick(void);           /* called from PIT handler */
void        task_sample(void);         /* called every second from PIT */
task_info_t *task_get(int tid);
task_info_t *task_get_raw(int tid);  /* like task_get but doesn't check active */
int         task_count(void);
void        task_set_mem(int tid, int kb);
int         task_check_killed(int tid); /* returns & clears killed flag */
int         task_find_by_pid(int pid); /* slot index or -1 */
int         task_get_pid(int tid);     /* PID for slot, or -1 */
int         task_kill_by_pid(int pid); /* 0=ok, -1=not found, -2=system */
void        task_set_name(int tid, const char *name);
void        task_add_gpu_ticks(int tid, uint32_t ticks);

/* Assign a unique PID to a task slot (used by external loaders) */
int         task_assign_pid(int tid);

/* Preemptive multitasking API */
int         task_create_thread(const char *name, void (*entry)(void), int killable);
int         task_create_user_thread(const char *name, void (*entry)(void), int killable);
void        task_yield(void);
void        task_exit(void);
void        task_block(int tid);
void        task_unblock(int tid);

#endif
