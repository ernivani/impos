#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/vma.h>

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
    TASK_STATE_STOPPED,     /* SIGSTOP/SIGTSTP: frozen, resumed by SIGCONT */
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

    /* Priority scheduler fields */
    uint8_t      priority;        /* 0=idle, 1=background, 2=normal, 3=realtime */
    uint8_t      time_slice;      /* ticks per quantum for this priority level */
    uint8_t      slice_remaining; /* ticks remaining in current quantum */

    /* Process lifecycle fields */
    int          parent_tid;      /* slot index of parent (-1 for init/root tasks) */
    int          exit_code;       /* exit status (set on task_exit) */
    int          wait_tid;        /* -1=not waiting, 0=wait any child, >0=specific child tid */

    /* Process groups & sessions */
    int          pgid;            /* process group ID (= PID of group leader) */
    int          sid;             /* session ID (= PID of session leader) */

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

    /* Per-task file descriptor table (dynamically allocated) */
    fd_entry_t  *fds;
    int          fd_count;     /* current capacity (starts at FD_INIT_SIZE) */

    /* Win32 PE task fields */
    uint32_t     tib;          /* pointer to WIN32_TEB (0 if not a PE task) */
    int          is_pe;        /* 1 if this is a PE executable task */

    /* ELF Linux compat fields */
    int          is_elf;         /* 1 if Linux ELF process */
    uint32_t     brk_start;      /* initial program break (end of loaded segments) */
    uint32_t     brk_current;    /* current program break */
    uint32_t     mmap_next;      /* next available VA for anonymous mmap */
    uint32_t     tls_base;       /* TLS base address (set by set_thread_area) */

    /* VMA-based memory tracking (Phase 3) */
    vma_table_t *vma;            /* per-process VMA table (NULL for kernel tasks) */

    /* File creation mask */
    uint16_t     umask;          /* file creation mask (default 0022) */

    /* ELF memory tracking for cleanup (legacy — will be removed after VMA migration) */
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
void        task_exit_code(int code);
void        task_block(int tid);
void        task_unblock(int tid);

/* Process lifecycle */
void        task_reparent_children(int dying_tid);

/* Process groups & sessions */
int         task_setpgid(int pid, int pgid);
int         task_getpgid(int pid);
int         task_setsid(int tid);
int         sig_send_group(int pgid, int signum);

/* waitpid support */
#define WNOHANG  1
int         sys_waitpid(int pid, int *wstatus, int options);

/* clone/fork support */
int         sys_clone(uint32_t clone_flags, uint32_t child_stack,
                      registers_t *parent_regs);

/* futex support */
int         sys_futex(uint32_t *uaddr, int op, uint32_t val);

#endif
