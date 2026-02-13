#ifndef _KERNEL_LINUX_SYSCALL_H
#define _KERNEL_LINUX_SYSCALL_H

#include <kernel/idt.h>
#include <stdint.h>

/* ── Linux i386 syscall numbers ─────────────────────────────────── */

#define LINUX_SYS_exit            1
#define LINUX_SYS_read            3
#define LINUX_SYS_write           4
#define LINUX_SYS_open            5
#define LINUX_SYS_close           6
#define LINUX_SYS_lseek           19
#define LINUX_SYS_getpid          20
#define LINUX_SYS_access          33
#define LINUX_SYS_brk             45
#define LINUX_SYS_ioctl           54
#define LINUX_SYS_readlink        85
#define LINUX_SYS_munmap          91
#define LINUX_SYS_uname           122
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
#define LINUX_SYS_set_thread_area 243
#define LINUX_SYS_exit_group      252
#define LINUX_SYS_set_tid_address  258

/* ── Linux errno values ─────────────────────────────────────────── */

#define LINUX_ENOENT   2
#define LINUX_EIO      5
#define LINUX_EBADF    9
#define LINUX_ENOMEM  12
#define LINUX_EACCES  13
#define LINUX_EEXIST  17
#define LINUX_ENOTDIR 20
#define LINUX_EISDIR  21
#define LINUX_EINVAL  22
#define LINUX_EMFILE  24
#define LINUX_ENOSPC  28
#define LINUX_ESPIPE  29
#define LINUX_ERANGE  34
#define LINUX_ENOSYS  38

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
#define LINUX_TIOCGWINSZ    0x5413

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

/* ── Linux mmap flags ───────────────────────────────────────────── */

#define LINUX_MAP_ANONYMOUS  0x20

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

/* ── API ────────────────────────────────────────────────────────── */

/* Dispatch a Linux i386 syscall. Called from syscall_handler for ELF tasks. */
registers_t* linux_syscall_handler(registers_t* regs);

#endif
