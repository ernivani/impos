#include <kernel/pipe.h>
#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/signal.h>
#include <string.h>
#include <stdlib.h>

static pipe_t pipes[MAX_PIPES];

static pipe_t *pipe_get(int pipe_id) {
    if (pipe_id >= 0 && pipe_id < MAX_PIPES && pipes[pipe_id].active)
        return &pipes[pipe_id];
    return 0;
}

/* ═══ FD table management ═══════════════════════════════════════ */

void fd_table_init(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t) return;
    t->fds = (fd_entry_t *)calloc(FD_INIT_SIZE, sizeof(fd_entry_t));
    t->fd_count = t->fds ? FD_INIT_SIZE : 0;
}

void fd_table_free(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t) return;
    if (t->fds) {
        free(t->fds);
        t->fds = 0;
    }
    t->fd_count = 0;
}

/* Grow the FD table to accommodate at least `needed` entries.
 * Doubles current capacity up to FD_MAX. Returns 0 on success. */
static int fd_table_grow(task_info_t *t, int needed) {
    if (!t || !t->fds) return -1;
    if (needed <= t->fd_count) return 0;
    if (needed > FD_MAX) return -1;

    int new_count = t->fd_count;
    while (new_count < needed && new_count < FD_MAX)
        new_count *= 2;
    if (new_count > FD_MAX)
        new_count = FD_MAX;

    fd_entry_t *new_fds = (fd_entry_t *)realloc(t->fds, new_count * sizeof(fd_entry_t));
    if (!new_fds) return -1;

    /* Zero-init the new entries */
    memset(&new_fds[t->fd_count], 0, (new_count - t->fd_count) * sizeof(fd_entry_t));
    t->fds = new_fds;
    t->fd_count = new_count;
    return 0;
}

/* ═══ Pipe refcount helpers ═════════════════════════════════════ */

void pipe_fork_bump(int pipe_id, int is_reader) {
    pipe_t *p = pipe_get(pipe_id);
    if (!p) return;
    if (is_reader)
        p->readers++;
    else
        p->writers++;
}

/* ═══ FD allocation ═════════════════════════════════════════════ */

int fd_alloc(int tid) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;

    /* Find lowest free slot */
    for (int i = 0; i < t->fd_count; i++) {
        if (t->fds[i].type == FD_NONE)
            return i;
    }

    /* All slots full — try to grow */
    if (t->fd_count < FD_MAX) {
        int old_count = t->fd_count;
        if (fd_table_grow(t, old_count + 1) == 0)
            return old_count;  /* first slot in the new region */
    }
    return -1;
}

/* ═══ dup / dup2 ════════════════════════════════════════════════ */

int fd_dup(int tid, int oldfd) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;
    if (oldfd < 0 || oldfd >= t->fd_count) return -1;
    if (t->fds[oldfd].type == FD_NONE) return -1;

    int newfd = fd_alloc(tid);
    if (newfd < 0) return -1;

    /* Copy the FD entry (POSIX: dup clears cloexec on new descriptor) */
    t->fds[newfd] = t->fds[oldfd];
    t->fds[newfd].cloexec = 0;

    /* If it's a pipe end, bump the refcount */
    if (t->fds[newfd].type == FD_PIPE_R || t->fds[newfd].type == FD_PIPE_W) {
        pipe_t *p = pipe_get(t->fds[newfd].pipe_id);
        if (p) {
            if (t->fds[newfd].type == FD_PIPE_R)
                p->readers++;
            else
                p->writers++;
        }
    }

    return newfd;
}

int fd_dup2(int tid, int oldfd, int newfd) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;
    if (oldfd < 0 || oldfd >= t->fd_count) return -1;
    if (t->fds[oldfd].type == FD_NONE) return -1;
    if (newfd < 0 || newfd >= FD_MAX) return -1;

    /* If oldfd == newfd, just return (POSIX behavior) */
    if (oldfd == newfd) return newfd;

    /* Grow table if newfd is beyond current capacity */
    if (newfd >= t->fd_count) {
        if (fd_table_grow(t, newfd + 1) < 0)
            return -1;
    }

    /* Close the target fd if it's open */
    if (t->fds[newfd].type != FD_NONE) {
        if (t->fds[newfd].type == FD_PIPE_R || t->fds[newfd].type == FD_PIPE_W) {
            pipe_close(newfd, tid);
        } else {
            t->fds[newfd].type = FD_NONE;
            t->fds[newfd].inode = 0;
            t->fds[newfd].offset = 0;
            t->fds[newfd].flags = 0;
            t->fds[newfd].pipe_id = 0;
        }
    }

    /* Copy the FD entry (POSIX: dup2 clears cloexec on new descriptor) */
    t->fds[newfd] = t->fds[oldfd];
    t->fds[newfd].cloexec = 0;

    /* If it's a pipe end, bump the refcount */
    if (t->fds[newfd].type == FD_PIPE_R || t->fds[newfd].type == FD_PIPE_W) {
        pipe_t *p = pipe_get(t->fds[newfd].pipe_id);
        if (p) {
            if (t->fds[newfd].type == FD_PIPE_R)
                p->readers++;
            else
                p->writers++;
        }
    }

    return newfd;
}

/* ═══ Pipe operations ═══════════════════════════════════════════ */

int pipe_create(int *read_fd, int *write_fd, int tid) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;

    /* Find a free pipe slot */
    int pid = -1;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) {
            pid = i;
            break;
        }
    }
    if (pid < 0) return -1;

    /* Find two free FDs in the task's table */
    int rfd = -1, wfd = -1;
    for (int i = 0; i < t->fd_count; i++) {
        if (t->fds[i].type == FD_NONE) {
            if (rfd < 0) rfd = i;
            else if (wfd < 0) { wfd = i; break; }
        }
    }
    /* If we couldn't find two free slots, try growing */
    if (rfd < 0 || wfd < 0) {
        if (rfd < 0) {
            rfd = fd_alloc(tid);
            if (rfd < 0) return -1;
        }
        if (wfd < 0) {
            wfd = fd_alloc(tid);
            if (wfd < 0) return -1;
        }
    }

    /* Initialize the pipe */
    memset(&pipes[pid], 0, sizeof(pipe_t));
    pipes[pid].active = 1;
    pipes[pid].readers = 1;
    pipes[pid].writers = 1;
    pipes[pid].read_tid = -1;
    pipes[pid].write_tid = -1;

    /* Set up FD entries */
    t->fds[rfd].type = FD_PIPE_R;
    t->fds[rfd].pipe_id = pid;
    t->fds[wfd].type = FD_PIPE_W;
    t->fds[wfd].pipe_id = pid;

    *read_fd = rfd;
    *write_fd = wfd;
    return 0;
}

/*
 * Returns bytes read, 0 for EOF, or -2 if caller should block and retry.
 */
int pipe_read(int fd, char *buf, int count, int tid) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds || fd < 0 || fd >= t->fd_count) return -1;
    if (t->fds[fd].type != FD_PIPE_R) return -1;

    pipe_t *p = pipe_get(t->fds[fd].pipe_id);
    if (!p) return -1;

    if (count <= 0) return 0;

    if (p->count == 0) {
        if (p->writers == 0)
            return 0;  /* EOF — no writers left */
        /* Buffer empty, writers exist → block */
        p->read_tid = tid;
        return -2;  /* sentinel: caller should block + retry */
    }

    /* Copy min(count, available) bytes from circular buffer */
    int to_read = count;
    if ((uint32_t)to_read > p->count) to_read = p->count;

    for (int i = 0; i < to_read; i++) {
        buf[i] = p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count -= to_read;

    /* Unblock writer if one was waiting */
    if (p->write_tid >= 0) {
        task_unblock(p->write_tid);
        p->write_tid = -1;
    }

    return to_read;
}

/*
 * Returns bytes written, -1 for broken pipe, or -2 if caller should block and retry.
 */
int pipe_write(int fd, const char *buf, int count, int tid) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds || fd < 0 || fd >= t->fd_count) return -1;
    if (t->fds[fd].type != FD_PIPE_W) return -1;

    pipe_t *p = pipe_get(t->fds[fd].pipe_id);
    if (!p) return -1;

    if (count <= 0) return 0;

    if (p->readers == 0) {
        sig_send(tid, SIGPIPE);
        return -1;  /* broken pipe — no readers */
    }

    uint32_t space = PIPE_BUF_SIZE - p->count;
    if (space == 0) {
        /* Buffer full → block */
        p->write_tid = tid;
        return -2;  /* sentinel: caller should block + retry */
    }

    int to_write = count;
    if ((uint32_t)to_write > space) to_write = space;

    for (int i = 0; i < to_write; i++) {
        p->buf[p->write_pos] = buf[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count += to_write;

    /* Unblock reader if one was waiting */
    if (p->read_tid >= 0) {
        task_unblock(p->read_tid);
        p->read_tid = -1;
    }

    return to_write;
}

void pipe_close(int fd, int tid) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds || fd < 0 || fd >= t->fd_count) return;
    if (t->fds[fd].type == FD_NONE) return;

    int pipe_id = t->fds[fd].pipe_id;
    pipe_t *p = pipe_get(pipe_id);
    if (!p) {
        t->fds[fd].type = FD_NONE;
        return;
    }

    if (t->fds[fd].type == FD_PIPE_R) {
        p->readers--;
        /* If a writer was blocked and no more readers, unblock it (will get EPIPE) */
        if (p->readers == 0 && p->write_tid >= 0) {
            task_unblock(p->write_tid);
            p->write_tid = -1;
        }
    } else if (t->fds[fd].type == FD_PIPE_W) {
        p->writers--;
        /* If a reader was blocked and no more writers, unblock it (will get EOF) */
        if (p->writers == 0 && p->read_tid >= 0) {
            task_unblock(p->read_tid);
            p->read_tid = -1;
        }
    }

    t->fds[fd].type = FD_NONE;
    t->fds[fd].pipe_id = 0;

    /* If both ends closed, free the pipe */
    if (p->readers == 0 && p->writers == 0)
        p->active = 0;
}

/* ═══ Poll query ═══════════════════════════════════════════════ */

int pipe_poll_query(int pipe_idx, int is_write_end) {
    pipe_t *p = pipe_get(pipe_idx);
    if (!p) return PIPE_POLL_NVAL;
    int revents = 0;
    if (is_write_end) {
        if (p->readers == 0) revents |= PIPE_POLL_ERR;
        if (p->count < PIPE_BUF_SIZE) revents |= PIPE_POLL_OUT;
    } else {
        if (p->count > 0) revents |= PIPE_POLL_IN;
        if (p->writers == 0) revents |= PIPE_POLL_HUP;
    }
    return revents;
}

uint32_t pipe_get_count(int pipe_idx) {
    pipe_t *p = pipe_get(pipe_idx);
    return p ? p->count : 0;
}

void pipe_cleanup_task(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t || !t->fds) return;

    for (int i = 0; i < t->fd_count; i++) {
        if (t->fds[i].type == FD_NONE)
            continue;

        /* Close pipe ends with proper refcount management */
        if (t->fds[i].type == FD_PIPE_R || t->fds[i].type == FD_PIPE_W) {
            int pipe_id = t->fds[i].pipe_id;
            if (pipe_id >= 0 && pipe_id < MAX_PIPES && pipes[pipe_id].active) {
                pipe_t *p = &pipes[pipe_id];
                if (t->fds[i].type == FD_PIPE_R) {
                    p->readers--;
                    if (p->readers == 0 && p->write_tid >= 0) {
                        task_unblock(p->write_tid);
                        p->write_tid = -1;
                    }
                } else {
                    p->writers--;
                    if (p->writers == 0 && p->read_tid >= 0) {
                        task_unblock(p->read_tid);
                        p->read_tid = -1;
                    }
                }
                if (p->readers == 0 && p->writers == 0)
                    p->active = 0;
            }
        }
        /* FD_FILE, FD_DEV, FD_DIR, FD_TTY: no extra cleanup needed */

        t->fds[i].type = FD_NONE;
        t->fds[i].pipe_id = 0;
        t->fds[i].inode = 0;
        t->fds[i].offset = 0;
        t->fds[i].flags = 0;
        t->fds[i].cloexec = 0;
    }
}
