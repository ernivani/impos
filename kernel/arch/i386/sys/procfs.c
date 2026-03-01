/*
 * procfs.c — Process Information Virtual Filesystem
 *
 * Read-only virtual filesystem mounted at /proc.
 * Content is generated on-the-fly from task, PMM, and scheduler state.
 *
 * Entries:
 *   /proc/uptime    — system uptime in seconds
 *   /proc/meminfo   — physical memory statistics
 *   /proc/version   — OS version string
 *   /proc/<pid>/status — per-process status
 *   /proc/<pid>/maps   — memory maps (simplified)
 */

#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/task.h>
#include <kernel/vma.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/rtc.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Helpers ───────────────────────────────────────────────────────── */

#define PROCFS_BUF_SIZE 1024

/* Parse a leading integer from path, return -1 if not a number */
static int parse_pid(const char *s) {
    if (!s || *s == '\0') return -1;
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    /* Must have consumed exactly to '/' or end */
    if (*s != '\0' && *s != '/') return -1;
    return val;
}

static const char *task_state_name(task_state_t st) {
    switch (st) {
    case TASK_STATE_UNUSED:   return "unused";
    case TASK_STATE_READY:    return "ready";
    case TASK_STATE_RUNNING:  return "running";
    case TASK_STATE_BLOCKED:  return "blocked";
    case TASK_STATE_SLEEPING: return "sleeping";
    case TASK_STATE_ZOMBIE:   return "zombie";
    default:                  return "unknown";
    }
}

/* ── Content generators ────────────────────────────────────────────── */

static int gen_uptime(char *buf, size_t max) {
    uint32_t ticks = pit_get_ticks();
    uint32_t secs = ticks / 120;  /* PIT runs at 120 Hz */
    uint32_t frac = (ticks % 120) * 100 / 120;
    return snprintf(buf, max, "%u.%02u\n", secs, frac);
}

static int gen_meminfo(char *buf, size_t max) {
    uint32_t free_frames = pmm_free_frame_count();
    uint32_t total_frames = 65536;  /* PMM_MAX_FRAMES */
    uint32_t used_frames = total_frames - free_frames;

    return snprintf(buf, max,
        "MemTotal:    %6u kB\n"
        "MemFree:     %6u kB\n"
        "MemUsed:     %6u kB\n"
        "Buffers:     %6u kB\n",
        total_frames * 4,
        free_frames * 4,
        used_frames * 4,
        0);
}

static int gen_version(char *buf, size_t max) {
    return snprintf(buf, max,
        "ImposOS version 1.0 (i386) FS v%d\n"
        "Compiled: " __DATE__ " " __TIME__ "\n",
        FS_VERSION);
}

static int gen_pid_status(char *buf, size_t max, int pid) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;

    task_info_t *t = task_get(tid);
    if (!t) return -1;

    return snprintf(buf, max,
        "Name:   %s\n"
        "State:  %s\n"
        "Pid:    %d\n"
        "Uid:    0\n"
        "VmRSS:  %d kB\n"
        "Threads: 1\n"
        "Ticks:  %u\n",
        t->name,
        task_state_name(t->state),
        t->pid,
        t->mem_kb,
        t->total_ticks);
}

static int gen_pid_maps(char *buf, size_t max, int pid) {
    int tid = task_find_by_pid(pid);
    if (tid < 0) return -1;

    task_info_t *t = task_get(tid);
    if (!t) return -1;

    int n = 0;

    /* If process has VMA table, dump real VMAs */
    if (t->vma) {
        for (int i = 0; i < VMA_MAX_PER_TASK && (size_t)n < max - 60; i++) {
            vma_t *v = &t->vma->vmas[i];
            if (!v->active) continue;

            char perms[5] = "---p";
            if (v->vm_flags & VMA_READ)  perms[0] = 'r';
            if (v->vm_flags & VMA_WRITE) perms[1] = 'w';
            if (v->vm_flags & VMA_EXEC)  perms[2] = 'x';
            if (v->vm_flags & VMA_SHARED) perms[3] = 's';

            n += snprintf(buf + n, max - n,
                "%08x-%08x %s %s\n",
                v->vm_start, v->vm_end, perms,
                vma_type_name(v->vm_type));
        }
    } else {
        /* Legacy fallback */
        if (t->is_user) {
            n += snprintf(buf + n, max - n,
                "00100000-001fffff r-xp [code]\n");
        }
        if (t->is_elf && t->brk_start) {
            n += snprintf(buf + n, max - n,
                "%08x-%08x rw-p [heap]\n",
                t->brk_start, t->brk_current);
        }
    }
    return n;
}

/* ── VFS ops ───────────────────────────────────────────────────────── */

static int procfs_read_file(void *priv, const char *path,
                            uint8_t *buf, size_t *size)
{
    (void)priv;
    char tmp[PROCFS_BUF_SIZE];
    int len = -1;

    if (!path || *path == '\0' || strcmp(path, "/") == 0) {
        return -1; /* directory, not a file */
    }

    /* Skip leading slash */
    if (*path == '/') path++;

    if (strcmp(path, "uptime") == 0) {
        len = gen_uptime(tmp, sizeof(tmp));
    } else if (strcmp(path, "meminfo") == 0) {
        len = gen_meminfo(tmp, sizeof(tmp));
    } else if (strcmp(path, "version") == 0) {
        len = gen_version(tmp, sizeof(tmp));
    } else {
        /* Try /proc/<pid>/subfile */
        int pid = parse_pid(path);
        if (pid >= 0) {
            const char *sub = strchr(path, '/');
            if (sub) {
                sub++; /* skip the '/' */
                if (strcmp(sub, "status") == 0) {
                    len = gen_pid_status(tmp, sizeof(tmp), pid);
                } else if (strcmp(sub, "maps") == 0) {
                    len = gen_pid_maps(tmp, sizeof(tmp), pid);
                }
            }
        }
    }

    if (len < 0) return -1;

    memcpy(buf, tmp, len);
    *size = len;
    return 0;
}

static int procfs_readdir(void *priv, const char *path,
                          fs_dir_entry_info_t *out, int max)
{
    (void)priv;
    int count = 0;

    /* Root of /proc */
    if (!path || *path == '\0' || strcmp(path, "/") == 0) {
        /* Static entries */
        const char *statics[] = { "uptime", "meminfo", "version" };
        for (int i = 0; i < 3 && count < max; i++) {
            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, statics[i], MAX_NAME_LEN - 1);
            out[count].type = INODE_FILE;
            out[count].size = 0;
            out[count].inode = 0x8000 + i;
            count++;
        }

        /* Per-PID directories */
        for (int tid = 0; tid < TASK_MAX && count < max; tid++) {
            task_info_t *t = task_get(tid);
            if (!t) continue;
            memset(&out[count], 0, sizeof(out[count]));
            snprintf(out[count].name, MAX_NAME_LEN, "%d", t->pid);
            out[count].type = INODE_DIR;
            out[count].size = 0;
            out[count].inode = 0x9000 + tid;
            count++;
        }
        return count;
    }

    /* /proc/<pid>/ listing */
    if (*path == '/') path++;
    int pid = parse_pid(path);
    if (pid >= 0 && task_find_by_pid(pid) >= 0) {
        const char *subfiles[] = { "status", "maps" };
        for (int i = 0; i < 2 && count < max; i++) {
            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, subfiles[i], MAX_NAME_LEN - 1);
            out[count].type = INODE_FILE;
            out[count].size = 0;
            out[count].inode = 0xA000 + pid * 10 + i;
            count++;
        }
    }
    return count;
}

static int procfs_stat(void *priv, const char *path, inode_t *out)
{
    (void)priv;
    memset(out, 0, sizeof(*out));
    out->mode = 0444; /* read-only for everyone */

    if (!path || *path == '\0' || strcmp(path, "/") == 0) {
        out->type = INODE_DIR;
        out->mode = 0555;
        return 0;
    }

    if (*path == '/') path++;

    if (strcmp(path, "uptime") == 0 ||
        strcmp(path, "meminfo") == 0 ||
        strcmp(path, "version") == 0) {
        out->type = INODE_FILE;
        return 0;
    }

    /* /proc/<pid> or /proc/<pid>/subfile */
    int pid = parse_pid(path);
    if (pid >= 0 && task_find_by_pid(pid) >= 0) {
        const char *sub = strchr(path, '/');
        if (!sub) {
            /* /proc/<pid> = directory */
            out->type = INODE_DIR;
            out->mode = 0555;
        } else {
            out->type = INODE_FILE;
        }
        return 0;
    }

    return -1; /* not found */
}

static int procfs_mount(void *priv)
{
    (void)priv;
    DBG("[PROCFS] Mounted at /proc");
    return 0;
}

/* ── Public interface ──────────────────────────────────────────────── */

static vfs_ops_t procfs_ops = {
    .name       = "procfs",
    .mount      = procfs_mount,
    .unmount    = NULL,
    .create     = NULL,    /* read-only */
    .unlink     = NULL,    /* read-only */
    .read_file  = procfs_read_file,
    .write_file = NULL,    /* read-only */
    .read_at    = NULL,
    .write_at   = NULL,
    .readdir    = procfs_readdir,
    .stat       = procfs_stat,
    .chmod      = NULL,
    .chown      = NULL,
    .rename     = NULL,
    .truncate   = NULL,
    .symlink    = NULL,
    .readlink   = NULL,
    .sync       = NULL,
};

void procfs_init(void) {
    vfs_mount("/proc", &procfs_ops, NULL);
}
