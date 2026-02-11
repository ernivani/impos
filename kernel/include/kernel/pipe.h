#ifndef _KERNEL_PIPE_H
#define _KERNEL_PIPE_H

#include <stdint.h>

#define PIPE_BUF_SIZE  4096
#define MAX_PIPES      16
#define MAX_FDS        16

/* File descriptor types */
#define FD_NONE   0
#define FD_PIPE_R 1  /* read end of pipe */
#define FD_PIPE_W 2  /* write end of pipe */

typedef struct {
    int  type;       /* FD_NONE / FD_PIPE_R / FD_PIPE_W */
    int  pipe_id;    /* index into global pipe table */
} fd_entry_t;

typedef struct {
    int      active;
    char     buf[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;       /* bytes currently in buffer */
    int      readers;     /* number of open read ends */
    int      writers;     /* number of open write ends */
    int      read_tid;    /* blocked reader task (-1 if none) */
    int      write_tid;   /* blocked writer task (-1 if none) */
} pipe_t;

/* Pipe API */
int  pipe_create(int *read_fd, int *write_fd, int tid);
int  pipe_read(int fd, char *buf, int count, int tid);
int  pipe_write(int fd, const char *buf, int count, int tid);
void pipe_close(int fd, int tid);
void pipe_cleanup_task(int tid);  /* close all FDs for a task */

#endif
