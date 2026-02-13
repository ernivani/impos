#ifndef _KERNEL_PIPE_H
#define _KERNEL_PIPE_H

#include <stdint.h>

#define PIPE_BUF_SIZE  4096
#define MAX_PIPES      16
#define MAX_FDS        64

/* File descriptor types */
#define FD_NONE   0
#define FD_PIPE_R 1  /* read end of pipe */
#define FD_PIPE_W 2  /* write end of pipe */
#define FD_FILE   3  /* regular file */
#define FD_DEV    4  /* character device */
#define FD_DIR    5  /* open directory */
#define FD_TTY    6  /* console stdin/stdout/stderr */

/* Linux open flags */
#define LINUX_O_RDONLY     0x0000
#define LINUX_O_WRONLY     0x0001
#define LINUX_O_RDWR       0x0002
#define LINUX_O_ACCMODE    0x0003
#define LINUX_O_CREAT      0x0040
#define LINUX_O_EXCL       0x0080
#define LINUX_O_TRUNC      0x0200
#define LINUX_O_APPEND     0x0400
#define LINUX_O_NONBLOCK   0x0800
#define LINUX_O_DIRECTORY  0x10000
#define LINUX_O_CLOEXEC    0x80000
#define LINUX_O_LARGEFILE  0x8000

typedef struct {
    int      type;       /* FD_NONE / FD_PIPE_R / FD_PIPE_W / FD_FILE / FD_DEV / FD_DIR / FD_TTY */
    int      pipe_id;    /* index into global pipe table (FD_PIPE_R/W) */
    uint32_t inode;      /* inode number for FD_FILE/FD_DEV/FD_DIR */
    uint32_t offset;     /* current read/write position */
    uint32_t flags;      /* open flags (LINUX_O_RDONLY etc.) */
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

/* FD allocation: returns lowest-available fd slot, or -1 */
int  fd_alloc(int tid);

#endif
