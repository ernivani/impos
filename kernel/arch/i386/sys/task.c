#include <kernel/task.h>
#include <string.h>

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
    for (int i = 4; i < TASK_MAX; i++) {
        if (!tasks[i].active) {
            memset(&tasks[i], 0, sizeof(task_info_t));
            tasks[i].active = 1;
            strncpy(tasks[i].name, name, 31);
            tasks[i].name[31] = '\0';
            tasks[i].killable = killable;
            tasks[i].wm_id = wm_id;
            tasks[i].pid = next_pid++;
            return i;
        }
    }
    return -1;
}

void task_unregister(int tid) {
    if (tid >= 0 && tid < TASK_MAX) {
        tasks[tid].active = 0;
        if (current_task == tid)
            current_task = TASK_IDLE;
    }
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
                if (tasks[i].hog_count >= 5)
                    tasks[i].killed = 1;
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
    tasks[tid].killed = 1;
    return 0;
}

void task_set_name(int tid, const char *name) {
    if (tid >= 0 && tid < TASK_MAX && tasks[tid].active) {
        strncpy(tasks[tid].name, name, 31);
        tasks[tid].name[31] = '\0';
    }
}
