/*
 * aarch64 Pipe & FD table stubs — Phase 3.
 *
 * Provides FD table allocation/free for task lifecycle.
 * Full pipe I/O comes when shell and process infrastructure is ready.
 */

#include <kernel/pipe.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <stdlib.h>
#include <string.h>

void fd_table_init(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t) return;

    t->fds = (fd_entry_t *)malloc(FD_INIT_SIZE * sizeof(fd_entry_t));
    if (t->fds) {
        memset(t->fds, 0, FD_INIT_SIZE * sizeof(fd_entry_t));
        t->fd_count = FD_INIT_SIZE;

        /* Set up stdin/stdout/stderr as TTY */
        for (int i = 0; i < 3 && i < FD_INIT_SIZE; i++) {
            t->fds[i].type = FD_TTY;
        }
    } else {
        t->fd_count = 0;
    }
}

void fd_table_free(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t) return;

    if (t->fds) {
        free(t->fds);
        t->fds = 0;
        t->fd_count = 0;
    }
}

void pipe_cleanup_task(int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t || !t->fds) return;

    for (int i = 0; i < t->fd_count; i++) {
        if (t->fds[i].type == FD_PIPE_R || t->fds[i].type == FD_PIPE_W) {
            pipe_close(i, tid);
        }
    }
}

int fd_alloc(int tid) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;
    for (int i = 0; i < t->fd_count; i++) {
        if (t->fds[i].type == FD_NONE)
            return i;
    }
    return -1;
}

int fd_dup(int tid, int oldfd) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;
    if (oldfd < 0 || oldfd >= t->fd_count) return -1;
    if (t->fds[oldfd].type == FD_NONE) return -1;

    int newfd = fd_alloc(tid);
    if (newfd < 0) return -1;
    t->fds[newfd] = t->fds[oldfd];
    t->fds[newfd].cloexec = 0;
    return newfd;
}

int fd_dup2(int tid, int oldfd, int newfd) {
    task_info_t *t = task_get(tid);
    if (!t || !t->fds) return -1;
    if (oldfd < 0 || oldfd >= t->fd_count) return -1;
    if (newfd < 0 || newfd >= t->fd_count) return -1;
    if (t->fds[oldfd].type == FD_NONE) return -1;
    if (oldfd == newfd) return newfd;
    t->fds[newfd] = t->fds[oldfd];
    t->fds[newfd].cloexec = 0;
    return newfd;
}

int pipe_create(int *read_fd, int *write_fd, int tid) {
    (void)read_fd; (void)write_fd; (void)tid;
    return -1;  /* Phase 4+ */
}

int pipe_read(int fd, char *buf, int count, int tid) {
    (void)fd; (void)buf; (void)count; (void)tid;
    return -1;
}

int pipe_write(int fd, const char *buf, int count, int tid) {
    (void)fd; (void)buf; (void)count; (void)tid;
    return -1;
}

void pipe_close(int fd, int tid) {
    task_info_t *t = task_get_raw(tid);
    if (!t || !t->fds) return;
    if (fd < 0 || fd >= t->fd_count) return;
    t->fds[fd].type = FD_NONE;
}

void pipe_fork_bump(int pipe_id, int is_reader) {
    (void)pipe_id; (void)is_reader;
}

int pipe_poll_query(int pipe_idx, int is_write_end) {
    (void)pipe_idx; (void)is_write_end;
    return PIPE_POLL_NVAL;
}

uint32_t pipe_get_count(int pipe_idx) {
    (void)pipe_idx;
    return 0;
}
