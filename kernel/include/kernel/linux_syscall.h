#ifndef _KERNEL_LINUX_SYSCALL_H
#define _KERNEL_LINUX_SYSCALL_H

#include <kernel/idt.h>
#include <stdint.h>

/* ── Linux i386 syscall numbers ─────────────────────────────────── */

#define LINUX_SYS_exit            1
#define LINUX_SYS_fork            2
#define LINUX_SYS_execve          11
#define LINUX_SYS_read            3
#define LINUX_SYS_write           4
#define LINUX_SYS_open            5
#define LINUX_SYS_close           6
#define LINUX_SYS_waitpid         7
#define LINUX_SYS_lseek           19
#define LINUX_SYS_getpid          20
#define LINUX_SYS_alarm           27
#define LINUX_SYS_access          33
#define LINUX_SYS_kill            37
#define LINUX_SYS_dup             41
#define LINUX_SYS_dup2            63
#define LINUX_SYS_setsid          66
#define LINUX_SYS_sigaction       67
#define LINUX_SYS_setpgid         57
#define LINUX_SYS_getppid         64
#define LINUX_SYS_brk             45
#define LINUX_SYS_ioctl           54
#define LINUX_SYS_readlink        85
#define LINUX_SYS_munmap          91
#define LINUX_SYS_ftruncate       93
#define LINUX_SYS_wait4           114
#define LINUX_SYS_clone           120
#define LINUX_SYS_vfork           190
#define LINUX_SYS_uname           122
#define LINUX_SYS_sigprocmask     126
#define LINUX_SYS_getpgid         132
#define LINUX_SYS__llseek         140
#define LINUX_SYS_writev          146
#define LINUX_SYS_getcwd          183
#define LINUX_SYS_mmap2           192
#define LINUX_SYS_stat64          195
#define LINUX_SYS_lstat64         196
#define LINUX_SYS_fstat64         197
#define LINUX_SYS_getuid32        199
#define LINUX_SYS_getgid32        200
#define LINUX_SYS_geteuid32       201
#define LINUX_SYS_getegid32       202
#define LINUX_SYS_getdents64      220
#define LINUX_SYS_fcntl64         221
#define LINUX_SYS_futex           240
#define LINUX_SYS_set_thread_area 243
#define LINUX_SYS_exit_group      252
#define LINUX_SYS_unlink          10
#define LINUX_SYS_chdir           12
#define LINUX_SYS_time            13
#define LINUX_SYS_rename          38
#define LINUX_SYS_mkdir           39
#define LINUX_SYS_rmdir           40
#define LINUX_SYS_pipe            42
#define LINUX_SYS_umask           60
#define LINUX_SYS_getpgrp         65
#define LINUX_SYS_gettimeofday    78
#define LINUX_SYS_fchdir          133
#define LINUX_SYS_readv           145
#define LINUX_SYS_nanosleep       162
#define LINUX_SYS_poll            168
#define LINUX_SYS_setuid32        213
#define LINUX_SYS_setgid32        214
#define LINUX_SYS_set_tid_address  258
#define LINUX_SYS_clock_gettime   265
#define LINUX_SYS_clock_nanosleep 267
#define LINUX_SYS_socketcall      102
#define LINUX_SYS_link            9
#define LINUX_SYS_chmod           15
#define LINUX_SYS_chown           16
#define LINUX_SYS_symlink         83
#define LINUX_SYS_fchmod          94
#define LINUX_SYS_fchown          95
#define LINUX_SYS_lchown          198
#define LINUX_SYS_statfs64        268
#define LINUX_SYS_fstatfs64       269

/* ── Linux errno values ─────────────────────────────────────────── */

#define LINUX_ENOENT   2
#define LINUX_EINTR    4
#define LINUX_EIO      5
#define LINUX_ENOEXEC  8
#define LINUX_EBADF    9
#define LINUX_ECHILD  10
#define LINUX_ENOMEM  12
#define LINUX_EFAULT  14
#define LINUX_EACCES  13
#define LINUX_EEXIST  17
#define LINUX_ENOTDIR 20
#define LINUX_EISDIR  21
#define LINUX_EINVAL  22
#define LINUX_EMFILE  24
#define LINUX_ENOSPC  28
#define LINUX_ESPIPE  29
#define LINUX_ERANGE  34
#define LINUX_ENOSYS     38
#define LINUX_ENOTEMPTY  39
#define LINUX_EPERM       1

/* ── Linux fcntl commands ───────────────────────────────────────── */

#define LINUX_F_GETFD  1
#define LINUX_F_SETFD  2
#define LINUX_F_GETFL  3
#define LINUX_F_SETFL  4

/* ── Linux access mode bits ─────────────────────────────────────── */

#define LINUX_F_OK  0
#define LINUX_R_OK  4
#define LINUX_W_OK  2
#define LINUX_X_OK  1

/* ── Linux seek whence ──────────────────────────────────────────── */

#define LINUX_SEEK_SET  0
#define LINUX_SEEK_CUR  1
#define LINUX_SEEK_END  2

/* ── Linux ioctl commands ───────────────────────────────────────── */

#define LINUX_TCGETS        0x5401
#define LINUX_TCSETS        0x5402
#define LINUX_TCSETSW       0x5403
#define LINUX_TCSETSF       0x5404
#define LINUX_TIOCGPGRP     0x540F
#define LINUX_TIOCSPGRP     0x5410
#define LINUX_TIOCGWINSZ    0x5413
#define LINUX_FIONREAD      0x541B

/* ── Linux d_type constants ─────────────────────────────────────── */

#define LINUX_DT_UNKNOWN  0
#define LINUX_DT_CHR      2
#define LINUX_DT_DIR      4
#define LINUX_DT_REG      8
#define LINUX_DT_LNK     10

/* ── Linux stat mode bits ───────────────────────────────────────── */

#define LINUX_S_IFCHR   0020000
#define LINUX_S_IFDIR   0040000
#define LINUX_S_IFREG   0100000
#define LINUX_S_IFLNK   0120000

/* ── Linux clone flags ─────────────────────────────────────────── */

#define LINUX_CLONE_VM        0x00000100
#define LINUX_CLONE_FS        0x00000200
#define LINUX_CLONE_FILES     0x00000400
#define LINUX_CLONE_SIGHAND   0x00000800
#define LINUX_CLONE_THREAD    0x00010000
#define LINUX_CLONE_CHILD_SETTID   0x01000000
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000
#define LINUX_CLONE_PARENT_SETTID  0x00100000
#define LINUX_SIGCHLD         17

/* ── socketcall sub-functions (i386 ABI) ───────────────────────── */

#define SYS_SOCKET      1
#define SYS_BIND        2
#define SYS_CONNECT     3
#define SYS_LISTEN      4
#define SYS_ACCEPT      5
#define SYS_GETSOCKNAME 6
#define SYS_GETPEERNAME 7
#define SYS_SEND        9
#define SYS_RECV        10
#define SYS_SENDTO      11
#define SYS_RECVFROM    12
#define SYS_SHUTDOWN    13
#define SYS_SETSOCKOPT  14
#define SYS_GETSOCKOPT  15

/* ── Linux errno: additional ───────────────────────────────────── */

#define LINUX_EAGAIN  11
#define LINUX_ENOTSOCK       88
#define LINUX_EPROTONOSUPPORT 93
#define LINUX_EAFNOSUPPORT   97
#define LINUX_EADDRINUSE     98
#define LINUX_ENETUNREACH   101
#define LINUX_ECONNRESET    104
#define LINUX_ENOTCONN      107
#define LINUX_ETIMEDOUT     110
#define LINUX_ECONNREFUSED  111
#define LINUX_EINPROGRESS   115

/* ── Linux mmap/mprotect flags ──────────────────────────────────── */

#define LINUX_PROT_NONE      0x0
#define LINUX_PROT_READ      0x1
#define LINUX_PROT_WRITE     0x2
#define LINUX_PROT_EXEC      0x4

#define LINUX_MAP_SHARED     0x01
#define LINUX_MAP_PRIVATE    0x02
#define LINUX_MAP_FIXED      0x10
#define LINUX_MAP_ANONYMOUS  0x20

#define LINUX_SYS_mprotect   125

/* ── Epoch offset: ImposOS epoch (2000-01-01) to Unix (1970-01-01) ─ */

#define IMPOS_EPOCH_OFFSET  946684800U

/* ── Linux struct layouts (i386 ABI) ────────────────────────────── */

/* struct linux_iovec for writev */
struct linux_iovec {
    uint32_t iov_base;
    uint32_t iov_len;
};

/* struct linux_user_desc for set_thread_area */
struct linux_user_desc {
    uint32_t entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t seg_32bit       : 1;
    uint32_t contents        : 2;
    uint32_t read_exec_only  : 1;
    uint32_t limit_in_pages  : 1;
    uint32_t seg_not_present : 1;
    uint32_t useable         : 1;
};

/* struct stat64 — Linux i386 ABI (96 bytes) */
struct linux_stat64 {
    uint64_t st_dev;         /* 0 */
    uint32_t __pad1;         /* 8 */
    uint32_t __st_ino;       /* 12 — old 32-bit inode */
    uint32_t st_mode;        /* 16 */
    uint32_t st_nlink;       /* 20 */
    uint32_t st_uid;         /* 24 */
    uint32_t st_gid;         /* 28 */
    uint64_t st_rdev;        /* 32 */
    uint32_t __pad2;         /* 40 */
    int64_t  st_size;        /* 44 — note: signed 64-bit (off64_t) */
    uint32_t st_blksize;     /* 52 */
    uint64_t st_blocks;      /* 56 — 512-byte blocks */
    uint32_t st_atime;       /* 64 */
    uint32_t st_atime_nsec;  /* 68 */
    uint32_t st_mtime;       /* 72 */
    uint32_t st_mtime_nsec;  /* 76 */
    uint32_t st_ctime;       /* 80 */
    uint32_t st_ctime_nsec;  /* 84 */
    uint64_t st_ino;         /* 88 — real 64-bit inode */
} __attribute__((packed));

/* struct linux_dirent64 (variable-length) */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];     /* variable length, null-terminated */
} __attribute__((packed));

/* struct utsname (6 fields x 65 bytes) */
struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* struct winsize for TIOCGWINSZ */
struct linux_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/* struct termios (simplified — enough to not crash) */
struct linux_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
};

/* struct timeval for gettimeofday */
struct linux_timeval {
    int32_t  tv_sec;
    int32_t  tv_usec;
};

/* struct timezone for gettimeofday */
struct linux_timezone {
    int32_t  tz_minuteswest;
    int32_t  tz_dsttime;
};

/* struct pollfd for poll */
struct linux_pollfd {
    int32_t  fd;
    int16_t  events;
    int16_t  revents;
};

/* poll event flags */
#define LINUX_POLLIN     0x0001
#define LINUX_POLLPRI    0x0002
#define LINUX_POLLOUT    0x0004
#define LINUX_POLLERR    0x0008
#define LINUX_POLLHUP    0x0010
#define LINUX_POLLNVAL   0x0020

/* struct statfs64 (Linux i386 ABI — 84 bytes) */
struct linux_statfs64 {
    uint32_t f_type;
    uint32_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    uint32_t f_namelen;
    uint32_t f_frsize;
    uint32_t f_flags;
    uint32_t f_spare[4];
} __attribute__((packed));

/* struct timespec for clock_gettime */
struct linux_clock_timespec {
    int32_t  tv_sec;
    int32_t  tv_nsec;
};

/* clock IDs */
#define LINUX_CLOCK_REALTIME   0
#define LINUX_CLOCK_MONOTONIC  1

/* ── API ────────────────────────────────────────────────────────── */

/* Dispatch a Linux i386 syscall. Called from syscall_handler for ELF tasks. */
registers_t* linux_syscall_handler(registers_t* regs);

#endif
