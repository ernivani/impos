#ifndef _KERNEL_TASK_H
#define _KERNEL_TASK_H

#include <stdint.h>

#define TASK_MAX        32
#define TASK_IDLE        0
#define TASK_KERNEL      1
#define TASK_WM          2
#define TASK_SHELL       3
/* Dynamic tasks: 4+ */

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
} task_info_t;

void        task_init(void);
int         task_register(const char *name, int killable, int wm_id);
void        task_unregister(int tid);
void        task_set_current(int tid);
int         task_get_current(void);
void        task_tick(void);           /* called from PIT handler */
void        task_sample(void);         /* called every second from PIT */
task_info_t *task_get(int tid);
int         task_count(void);
void        task_set_mem(int tid, int kb);
int         task_check_killed(int tid); /* returns & clears killed flag */
int         task_find_by_pid(int pid); /* slot index or -1 */
int         task_get_pid(int tid);     /* PID for slot, or -1 */
int         task_kill_by_pid(int pid); /* 0=ok, -1=not found, -2=system */
void        task_set_name(int tid, const char *name);

#endif
