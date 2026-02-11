#include <kernel/pipe.h>
#include <kernel/task.h>
#include <kernel/idt.h>
#include <kernel/signal.h>
#include <string.h>

static pipe_t pipes[MAX_PIPES];

static pipe_t *pipe_get(int pipe_id) {
    if (pipe_id >= 0 && pipe_id < MAX_PIPES && pipes[pipe_id].active)
        return &pipes[pipe_id];
    return 0;
}

int pipe_create(int *read_fd, int *write_fd, int tid) {
    task_info_t *t = task_get(tid);
    if (!t) return -1;

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
    for (int i = 0; i < MAX_FDS; i++) {
        if (t->fds[i].type == FD_NONE) {
            if (rfd < 0) rfd = i;
            else if (wfd < 0) { wfd = i; break; }
        }
    }
    if (rfd < 0 || wfd < 0) return -1;

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
    if (!t || fd < 0 || fd >= MAX_FDS) return -1;
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
    if (!t || fd < 0 || fd >= MAX_FDS) return -1;
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
    if (!t || fd < 0 || fd >= MAX_FDS) return;
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

void pipe_cleanup_task(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t) return;

    for (int i = 0; i < MAX_FDS; i++) {
        if (t->fds[i].type != FD_NONE) {
            /* Inline close logic using task_get_raw since task may be dying */
            int pipe_id = t->fds[i].pipe_id;
            if (pipe_id >= 0 && pipe_id < MAX_PIPES && pipes[pipe_id].active) {
                pipe_t *p = &pipes[pipe_id];
                if (t->fds[i].type == FD_PIPE_R) {
                    p->readers--;
                    if (p->readers == 0 && p->write_tid >= 0) {
                        task_unblock(p->write_tid);
                        p->write_tid = -1;
                    }
                } else if (t->fds[i].type == FD_PIPE_W) {
                    p->writers--;
                    if (p->writers == 0 && p->read_tid >= 0) {
                        task_unblock(p->read_tid);
                        p->read_tid = -1;
                    }
                }
                if (p->readers == 0 && p->writers == 0)
                    p->active = 0;
            }
            t->fds[i].type = FD_NONE;
            t->fds[i].pipe_id = 0;
        }
    }
}
