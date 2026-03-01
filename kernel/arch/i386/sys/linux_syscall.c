#include <kernel/linux_syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vma.h>
#include <kernel/frame_ref.h>
#include <kernel/idt.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/hostname.h>
#include <kernel/drm.h>
#include <kernel/elf_loader.h>
#include <kernel/rtc.h>
#include <kernel/socket.h>
#include <kernel/tcp.h>
#include <kernel/udp.h>
#include <kernel/net.h>
#include <kernel/endian.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Fill a linux_stat64 from an ImposOS inode */
static void fill_stat64(struct linux_stat64 *st, uint32_t ino, inode_t *node) {
    memset(st, 0, sizeof(*st));

    st->st_dev = 1;
    st->__st_ino = ino;
    st->st_ino = ino;
    st->st_nlink = 1;
    st->st_uid = node->owner_uid;
    st->st_gid = node->owner_gid;
    st->st_blksize = 4096;

    switch (node->type) {
        case INODE_FILE:
            st->st_mode = LINUX_S_IFREG | (node->mode & 0777);
            st->st_size = node->size;
            st->st_blocks = (node->size + 511) / 512;
            break;
        case INODE_DIR:
            st->st_mode = LINUX_S_IFDIR | (node->mode & 0777);
            st->st_size = node->size;
            st->st_nlink = 2;
            break;
        case INODE_SYMLINK:
            st->st_mode = LINUX_S_IFLNK | 0777;
            st->st_size = node->size;
            break;
        case INODE_CHARDEV:
            st->st_mode = LINUX_S_IFCHR | (node->mode & 0777);
            st->st_rdev = ((uint64_t)node->blocks[0] << 8) | node->blocks[1];
            break;
        default:
            st->st_mode = node->mode & 0777;
            break;
    }

    /* Convert ImposOS epoch (2000-01-01) to Unix epoch (1970-01-01) */
    st->st_atime = node->accessed_at + IMPOS_EPOCH_OFFSET;
    st->st_mtime = node->modified_at + IMPOS_EPOCH_OFFSET;
    st->st_ctime = node->created_at + IMPOS_EPOCH_OFFSET;
}

/* Convert ImposOS inode type to Linux d_type */
static uint8_t inode_type_to_dtype(uint8_t type) {
    switch (type) {
        case INODE_FILE:    return LINUX_DT_REG;
        case INODE_DIR:     return LINUX_DT_DIR;
        case INODE_SYMLINK: return LINUX_DT_LNK;
        case INODE_CHARDEV: return LINUX_DT_CHR;
        default:            return LINUX_DT_UNKNOWN;
    }
}

/* ── Linux open(path, flags, mode) ──────────────────────────────── */

static int32_t linux_sys_open(const char *path, uint32_t flags, uint32_t mode) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return -LINUX_EIO;

    /* Allocate fd first */
    int fd = fd_alloc(tid);
    if (fd < 0) return -LINUX_EMFILE;

    uint32_t parent;
    char name[28]; /* MAX_NAME_LEN */
    int ino = fs_resolve_path(path, &parent, name);

    int accmode = flags & LINUX_O_ACCMODE;

    /* O_CREAT handling */
    if (ino < 0) {
        if (!(flags & LINUX_O_CREAT))
            return -LINUX_ENOENT;
        /* Create the file */
        if (fs_create_file(path, 0) < 0)
            return -LINUX_ENOSPC;
        ino = fs_resolve_path(path, &parent, name);
        if (ino < 0)
            return -LINUX_EIO;
    }

    /* Read inode to check type */
    inode_t node;
    if (fs_read_inode(ino, &node) < 0)
        return -LINUX_EIO;

    /* O_DIRECTORY: must be a directory */
    if ((flags & LINUX_O_DIRECTORY) && node.type != INODE_DIR)
        return -LINUX_ENOTDIR;

    /* Determine FD type based on inode type */
    int fd_type;
    switch (node.type) {
        case INODE_FILE:    fd_type = FD_FILE; break;
        case INODE_DIR:     fd_type = FD_DIR;  break;
        case INODE_CHARDEV:
            fd_type = ((uint8_t)node.blocks[0] == DEV_MAJOR_DRM) ? FD_DRM : FD_DEV;
            break;
        case INODE_SYMLINK: {
            /* Follow symlink: re-resolve using readlink target */
            char target[256];
            if (fs_readlink(path, target, sizeof(target)) < 0)
                return -LINUX_EIO;
            return linux_sys_open(target, flags, mode);
        }
        default:
            return -LINUX_EIO;
    }

    /* O_TRUNC: truncate regular file */
    if ((flags & LINUX_O_TRUNC) && node.type == INODE_FILE &&
        (accmode == LINUX_O_WRONLY || accmode == LINUX_O_RDWR)) {
        fs_write_file(path, NULL, 0);
    }

    /* Set up the fd entry */
    t->fds[fd].type = fd_type;
    t->fds[fd].inode = (uint32_t)ino;
    t->fds[fd].offset = 0;
    t->fds[fd].flags = flags;
    t->fds[fd].pipe_id = 0;
    t->fds[fd].cloexec = (flags & LINUX_O_CLOEXEC) ? 1 : 0;

    return fd;
}

/* ── Linux close(fd) ────────────────────────────────────────────── */

static int32_t linux_sys_close(uint32_t fd) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    /* For pipes, do proper refcount management */
    if (fde->type == FD_PIPE_R || fde->type == FD_PIPE_W) {
        pipe_close((int)fd, tid);
        return 0;
    }

    /* For sockets: close the socket layer object */
    if (fde->type == FD_SOCKET) {
        socket_close(fde->pipe_id);
    }

    /* For files/dirs/devs/tty/sockets: clear */
    fde->type = FD_NONE;
    fde->inode = 0;
    fde->offset = 0;
    fde->flags = 0;
    fde->pipe_id = 0;
    fde->cloexec = 0;
    return 0;
}

/* ── Linux read(fd, buf, count) ─────────────────────────────────── */

static int32_t linux_sys_read(uint32_t fd, char *buf, uint32_t count) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    switch (fde->type) {
        case FD_TTY: {
            /* Read from keyboard — one char at a time for now */
            extern char getchar(void);
            if (count == 0) return 0;
            buf[0] = getchar();
            return 1;
        }
        case FD_FILE: {
            int rc = fs_read_at(fde->inode, (uint8_t *)buf, fde->offset, count);
            if (rc > 0)
                fde->offset += rc;
            return rc;
        }
        case FD_PIPE_R: {
            int rc = pipe_read((int)fd, buf, (int)count, tid);
            if (rc >= 0) return rc;
            if (rc == -2) return 0; /* would block → return 0 for now */
            return -LINUX_EIO;
        }
        case FD_DEV: {
            /* Device read via inode */
            inode_t node;
            if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
            if (node.type != INODE_CHARDEV) return -LINUX_EIO;

            uint8_t major = (uint8_t)node.blocks[0];
            switch (major) {
                case 1: /* /dev/null */
                    return 0;
                case 2: { /* /dev/zero */
                    memset(buf, 0, count);
                    return (int32_t)count;
                }
                case 3: { /* /dev/tty */
                    extern char getchar(void);
                    if (count == 0) return 0;
                    buf[0] = getchar();
                    return 1;
                }
                case 4: { /* /dev/urandom */
                    extern void prng_random(uint8_t *buf, size_t len);
                    prng_random((uint8_t *)buf, count);
                    return (int32_t)count;
                }
                default:
                    return -LINUX_EIO;
            }
        }
        case FD_SOCKET: {
            int sock_id = fde->pipe_id;
            int nb = socket_get_nonblock(sock_id) ||
                     (fde->flags & LINUX_O_NONBLOCK);
            if (nb) {
                int rc = socket_recv_nb(sock_id, buf, count);
                if (rc == -2) return -LINUX_EAGAIN;
                return rc;
            }
            return socket_recv(sock_id, buf, count, 5000);
        }
        case FD_DIR:
            return -LINUX_EISDIR;
        default:
            return -LINUX_EBADF;
    }
}

/* ── Linux write(fd, buf, count) ────────────────────────────────── */

static int32_t linux_sys_write(uint32_t fd, const char *buf, uint32_t count) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    switch (fde->type) {
        case FD_TTY:
            for (uint32_t i = 0; i < count; i++)
                putchar(buf[i]);
            return (int32_t)count;

        case FD_PIPE_W: {
            int rc = pipe_write((int)fd, buf, (int)count, tid);
            if (rc >= 0) return (int32_t)rc;
            return -LINUX_EIO;
        }

        case FD_DEV: {
            inode_t node;
            if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
            if (node.type != INODE_CHARDEV) return -LINUX_EIO;

            uint8_t major = (uint8_t)node.blocks[0];
            switch (major) {
                case 1: /* /dev/null */
                case 2: /* /dev/zero */
                case 4: /* /dev/urandom */
                    return (int32_t)count; /* discard */
                case 3: /* /dev/tty */
                    for (uint32_t i = 0; i < count; i++)
                        putchar(buf[i]);
                    return (int32_t)count;
                default:
                    return -LINUX_EIO;
            }
        }

        case FD_FILE: {
            int rc = fs_write_at(fde->inode, (const uint8_t *)buf, fde->offset, count);
            if (rc > 0)
                fde->offset += rc;
            return rc;
        }

        case FD_SOCKET: {
            int sock_id = fde->pipe_id;
            return socket_send(sock_id, buf, count);
        }

        case FD_DIR:
            return -LINUX_EISDIR;

        default:
            return -LINUX_EBADF;
    }
}

/* ── Linux writev(fd, iov, iovcnt) ───────────────────────────────── */

static int32_t linux_sys_writev(uint32_t fd, const struct linux_iovec *iov,
                                 uint32_t iovcnt) {
    int32_t total = 0;
    for (uint32_t i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0)
            continue;
        int32_t ret = linux_sys_write(fd, (const char *)iov[i].iov_base,
                                       iov[i].iov_len);
        if (ret < 0)
            return ret;
        total += ret;
    }
    return total;
}

/* ── Linux ftruncate(fd, length) ────────────────────────────────── */

static int32_t linux_sys_ftruncate(uint32_t fd, uint32_t length) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type != FD_FILE) return -LINUX_EINVAL;

    if (fs_truncate_inode(fde->inode, length) < 0)
        return -LINUX_EIO;
    return 0;
}

/* ── Linux lseek(fd, offset, whence) ────────────────────────────── */

static int32_t linux_sys_lseek(uint32_t fd, int32_t offset, uint32_t whence) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;
    if (fde->type == FD_PIPE_R || fde->type == FD_PIPE_W)
        return -LINUX_ESPIPE;
    if (fde->type == FD_TTY)
        return -LINUX_ESPIPE;

    int32_t new_off;
    switch (whence) {
        case LINUX_SEEK_SET:
            new_off = offset;
            break;
        case LINUX_SEEK_CUR:
            new_off = (int32_t)fde->offset + offset;
            break;
        case LINUX_SEEK_END: {
            if (fde->type == FD_FILE || fde->type == FD_DIR) {
                inode_t node;
                if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
                new_off = (int32_t)node.size + offset;
            } else {
                return -LINUX_EINVAL;
            }
            break;
        }
        default:
            return -LINUX_EINVAL;
    }

    if (new_off < 0) return -LINUX_EINVAL;
    fde->offset = (uint32_t)new_off;
    return new_off;
}

/* ── Linux _llseek(fd, offset_high, offset_low, result*, whence) ── */

static int32_t linux_sys_llseek(uint32_t fd, uint32_t offset_high,
                                 uint32_t offset_low, uint64_t *result,
                                 uint32_t whence) {
    /* We only support 32-bit offsets, so ignore offset_high */
    int32_t rc = linux_sys_lseek(fd, (int32_t)offset_low, whence);
    if (rc < 0) return rc;
    if (result)
        *result = (uint64_t)(uint32_t)rc;
    return 0;
}

/* ── Linux stat64(path, statbuf) ────────────────────────────────── */

static int32_t linux_sys_stat64(const char *path, struct linux_stat64 *statbuf) {
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(path, &parent, name);

    /* Handle root path */
    if (name[0] == '\0') ino = (int)parent;
    if (ino < 0) return -LINUX_ENOENT;

    inode_t node;
    if (fs_read_inode(ino, &node) < 0) return -LINUX_EIO;

    /* Follow symlinks */
    if (node.type == INODE_SYMLINK) {
        char target[256];
        if (fs_readlink(path, target, sizeof(target)) < 0)
            return -LINUX_EIO;
        return linux_sys_stat64(target, statbuf);
    }

    fill_stat64(statbuf, (uint32_t)ino, &node);
    return 0;
}

/* ── Linux lstat64(path, statbuf) — does NOT follow symlinks ────── */

static int32_t linux_sys_lstat64(const char *path, struct linux_stat64 *statbuf) {
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(path, &parent, name);

    if (name[0] == '\0') ino = (int)parent;
    if (ino < 0) return -LINUX_ENOENT;

    inode_t node;
    if (fs_read_inode(ino, &node) < 0) return -LINUX_EIO;

    fill_stat64(statbuf, (uint32_t)ino, &node);
    return 0;
}

/* ── Linux fstat64(fd, statbuf) ─────────────────────────────────── */

static int32_t linux_sys_fstat64(uint32_t fd, struct linux_stat64 *statbuf) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    if (fde->type == FD_TTY) {
        /* Synthesize a chardev stat for the console */
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = LINUX_S_IFCHR | 0620;
        statbuf->st_rdev = (5 << 8) | 0; /* tty major=5, minor=0 */
        statbuf->st_blksize = 1024;
        return 0;
    }

    if (fde->type == FD_PIPE_R || fde->type == FD_PIPE_W) {
        /* Synthesize a pipe stat */
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = 0600;  /* FIFO */
        statbuf->st_blksize = 4096;
        return 0;
    }

    if (fde->type == FD_SOCKET) {
        /* Synthesize a socket stat */
        memset(statbuf, 0, sizeof(*statbuf));
        statbuf->st_mode = 0140666;  /* S_IFSOCK | 0666 */
        statbuf->st_blksize = 4096;
        return 0;
    }

    inode_t node;
    if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
    fill_stat64(statbuf, fde->inode, &node);
    return 0;
}

/* ── Linux getdents64(fd, dirp, count) ──────────────────────────── */

static int32_t linux_sys_getdents64(uint32_t fd, void *dirp, uint32_t count) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type != FD_DIR) return -LINUX_ENOTDIR;

    inode_t node;
    if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
    if (node.type != INODE_DIR) return -LINUX_ENOTDIR;

    /* Use a static block buffer (syscalls are non-reentrant in our OS) */
    static uint8_t block_buf[4096];

    uint8_t *out = (uint8_t *)dirp;
    uint32_t bytes_written = 0;
    uint32_t entry_index = 0;  /* linear entry counter */

    for (uint8_t b = 0; b < node.num_blocks; b++) {
        if (fs_read_block(node.blocks[b], block_buf) < 0)
            continue;

        dir_entry_t *entries = (dir_entry_t *)block_buf;
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);

        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] == '\0') {
                entry_index++;
                continue;
            }

            /* Skip entries before current offset */
            if (entry_index < fde->offset) {
                entry_index++;
                continue;
            }

            /* Calculate record size */
            size_t name_len = strlen(entries[e].name);
            /* d_ino(8) + d_off(8) + d_reclen(2) + d_type(1) + name + null */
            uint16_t reclen = (uint16_t)(19 + name_len + 1);
            /* Align to 8 bytes */
            reclen = (reclen + 7) & ~7;

            if (bytes_written + reclen > count) {
                /* No more room — return what we have */
                if (bytes_written == 0)
                    return -LINUX_EINVAL; /* buffer too small for even one entry */
                return (int32_t)bytes_written;
            }

            /* Look up child inode for type */
            uint8_t d_type = LINUX_DT_UNKNOWN;
            if (entries[e].inode < NUM_INODES) {
                inode_t child;
                if (fs_read_inode(entries[e].inode, &child) == 0)
                    d_type = inode_type_to_dtype(child.type);
            }

            /* Write the dirent64 entry */
            struct linux_dirent64 *de = (struct linux_dirent64 *)(out + bytes_written);
            de->d_ino = entries[e].inode;
            de->d_off = entry_index + 1;
            de->d_reclen = reclen;
            de->d_type = d_type;
            memcpy(de->d_name, entries[e].name, name_len + 1);
            /* Zero padding after the name */
            size_t used = 19 + name_len + 1;
            if (used < reclen)
                memset(out + bytes_written + used, 0, reclen - used);

            bytes_written += reclen;
            fde->offset = entry_index + 1;
            entry_index++;
        }
    }

    return (int32_t)bytes_written;
}

/* ── Linux fcntl64(fd, cmd, arg) ────────────────────────────────── */

static int32_t linux_sys_fcntl64(uint32_t fd, uint32_t cmd, uint32_t arg) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    switch (cmd) {
        case LINUX_F_GETFD: return fde->cloexec ? FD_CLOEXEC : 0;
        case LINUX_F_SETFD:
            fde->cloexec = (arg & FD_CLOEXEC) ? 1 : 0;
            return 0;
        case LINUX_F_GETFL: return (int32_t)fde->flags;
        case LINUX_F_SETFL:
            /* Only allow O_NONBLOCK and O_APPEND to be changed */
            fde->flags = (fde->flags & ~(LINUX_O_NONBLOCK | LINUX_O_APPEND))
                       | (arg & (LINUX_O_NONBLOCK | LINUX_O_APPEND));
            /* Propagate O_NONBLOCK to socket layer */
            if (fde->type == FD_SOCKET)
                socket_set_nonblock(fde->pipe_id,
                                    (arg & LINUX_O_NONBLOCK) ? 1 : 0);
            return 0;
        default:
            return -LINUX_EINVAL;
    }
}

/* ── Linux getcwd(buf, size) ────────────────────────────────────── */

static int32_t linux_sys_getcwd(char *buf, uint32_t size) {
    const char *cwd = fs_get_cwd();
    size_t len = strlen(cwd) + 1; /* include null */
    if (len > size) return -LINUX_ERANGE;
    memcpy(buf, cwd, len);
    return (int32_t)len;
}

/* ── Linux uname(buf) ───────────────────────────────────────────── */

static int32_t linux_sys_uname(struct linux_utsname *buf) {
    memset(buf, 0, sizeof(*buf));
    /* musl checks sysname == "Linux" — we must pretend */
    strncpy(buf->sysname, "Linux", 64);
    const char *hn = hostname_get();
    strncpy(buf->nodename, hn ? hn : "imposos", 64);
    strncpy(buf->release, "5.15.0-impos", 64);
    strncpy(buf->version, "#1 SMP ImposOS", 64);
    strncpy(buf->machine, "i686", 64);
    strncpy(buf->domainname, "(none)", 64);
    return 0;
}

/* ── Linux access(path, mode) ───────────────────────────────────── */

static int32_t linux_sys_access(const char *path, uint32_t mode) {
    uint32_t parent;
    char name[28];
    int ino = fs_resolve_path(path, &parent, name);

    /* Handle root path */
    if (name[0] == '\0') ino = (int)parent;
    if (ino < 0) return -LINUX_ENOENT;

    /* F_OK: just check existence */
    if (mode == LINUX_F_OK) return 0;

    /* We have the inode — for now just return 0 (full access) */
    return 0;
}

/* ── Linux ioctl(fd, cmd, arg) ──────────────────────────────────── */

static int32_t linux_sys_ioctl(uint32_t fd, uint32_t cmd, uint32_t arg) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    if (fde->type == FD_TTY) {
        switch (cmd) {
            case LINUX_TIOCGWINSZ: {
                struct linux_winsize *ws = (struct linux_winsize *)arg;
                ws->ws_row = 67;     /* 1080 / 16 */
                ws->ws_col = 240;    /* 1920 / 8 */
                ws->ws_xpixel = 1920;
                ws->ws_ypixel = 1080;
                return 0;
            }
            case LINUX_TCGETS: {
                struct linux_termios *tio = (struct linux_termios *)arg;
                memset(tio, 0, sizeof(*tio));
                return 0;
            }
            case LINUX_TCSETS:
            case LINUX_TCSETSW:
            case LINUX_TCSETSF:
                return 0;  /* accept + ignore */
            case LINUX_FIONREAD:
                if (arg) *(int *)arg = 0;
                return 0;
            case LINUX_TIOCGPGRP: {
                if (arg) *(int *)arg = t->pgid;
                return 0;
            }
            case LINUX_TIOCSPGRP:
                return 0;  /* accept + ignore */
            default:
                return -LINUX_ENOSYS;
        }
    }

    /* Pipe FIONREAD support */
    if (fde->type == FD_PIPE_R && cmd == LINUX_FIONREAD) {
        if (arg) *(int *)arg = (int)pipe_get_count(fde->pipe_id);
        return 0;
    }

    if (fde->type == FD_DRM)
        return drm_ioctl(cmd, (void *)arg);

    return -LINUX_ENOSYS;
}

/* ── Linux readlink(path, buf, bufsz) ───────────────────────────── */

static int32_t linux_sys_readlink(const char *path, char *buf, uint32_t bufsiz) {
    /* /proc/self/exe: return the binary path (musl uses this) */
    if (strcmp(path, "/proc/self/exe") == 0) {
        int tid = task_get_current();
        task_info_t *t = task_get(tid);
        if (t) {
            size_t len = strlen(t->name);
            if (len > bufsiz) len = bufsiz;
            memcpy(buf, t->name, len);
            return (int32_t)len;
        }
        return -LINUX_ENOENT;
    }

    /* Try filesystem readlink */
    char tmp[256];
    if (fs_readlink(path, tmp, sizeof(tmp)) < 0)
        return -LINUX_EINVAL;

    size_t len = strlen(tmp);
    if (len > bufsiz) len = bufsiz;
    memcpy(buf, tmp, len);
    return (int32_t)len;
}

/* ── Linux brk(new_brk) ─────────────────────────────────────────── */

static uint32_t linux_sys_brk(uint32_t new_brk) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return 0;

    /* Query: return current brk */
    if (new_brk == 0)
        return t->brk_current;

    /* Cannot shrink below brk_start */
    if (new_brk < t->brk_start)
        return t->brk_current;

    if (new_brk > t->brk_current) {
        /* ── Grow ── */
        uint32_t old_page = (t->brk_current + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint32_t new_page = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint32_t va = old_page; va < new_page; va += PAGE_SIZE) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame)
                return t->brk_current;
            memset((void *)frame, 0, PAGE_SIZE);

            if (!vmm_map_user_page(t->page_dir, va, frame,
                                    PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
                pmm_free_frame(frame);
                return t->brk_current;
            }

            if (t->num_elf_frames < 64)
                t->elf_frames[t->num_elf_frames++] = frame;
        }

        /* Update BRK VMA */
        if (t->vma) {
            for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
                vma_t *v = &t->vma->vmas[i];
                if (v->active && v->vm_type == VMA_TYPE_BRK) {
                    v->vm_end = new_page;
                    break;
                }
            }
            t->vma->brk_current = new_brk;
        }
    } else if (new_brk < t->brk_current) {
        /* ── Shrink ── */
        uint32_t new_page = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint32_t old_page = (t->brk_current + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        /* Free pages from new_page to old_page */
        if (t->page_dir && t->page_dir != vmm_get_kernel_pagedir()) {
            for (uint32_t va = new_page; va < old_page; va += PAGE_SIZE) {
                uint32_t pte = vmm_get_pte(t->page_dir, va);
                if (pte & PTE_PRESENT) {
                    uint32_t frame = pte & PAGE_MASK;
                    vmm_unmap_user_page(t->page_dir, va);
                    if (frame_ref_dec(frame) == 0)
                        pmm_free_frame(frame);
                }
            }
        }

        /* Shrink BRK VMA */
        if (t->vma) {
            for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
                vma_t *v = &t->vma->vmas[i];
                if (v->active && v->vm_type == VMA_TYPE_BRK) {
                    v->vm_end = new_page;
                    if (v->vm_end <= v->vm_start) {
                        v->vm_end = v->vm_start;  /* zero-size is fine */
                    }
                    break;
                }
            }
            t->vma->brk_current = new_brk;
        }
    }

    t->brk_current = new_brk;
    return new_brk;
}

/* Forward declaration (needed by MAP_FIXED in mmap2) */
static int32_t linux_sys_munmap(uint32_t addr, uint32_t len);

/* ── Linux mmap2(addr, len, prot, flags, fd, pgoff) ──────────────── */

static uint32_t linux_sys_mmap2(uint32_t addr, uint32_t len, uint32_t prot,
                                 uint32_t flags, uint32_t fd, uint32_t pgoff) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return (uint32_t)-LINUX_ENOMEM;

    uint32_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t alloc_len = num_pages * PAGE_SIZE;
    uint32_t va_start;

    /* ── Address selection (shared by anonymous and file-backed) ── */
    if (flags & LINUX_MAP_FIXED) {
        if (addr & (PAGE_SIZE - 1))
            return (uint32_t)-LINUX_EINVAL;
        va_start = addr;
        /* Unmap anything already at this range */
        linux_sys_munmap(va_start, alloc_len);
    } else if (t->vma) {
        va_start = vma_find_free(t->vma, alloc_len);
        if (!va_start)
            return (uint32_t)-LINUX_ENOMEM;
    } else {
        va_start = t->mmap_next;
        t->mmap_next += alloc_len;
    }

    /* Build VMA flags from prot */
    uint32_t vflags = VMA_ANON;
    if (prot & LINUX_PROT_READ)  vflags |= VMA_READ;
    if (prot & LINUX_PROT_WRITE) vflags |= VMA_WRITE;
    if (prot & LINUX_PROT_EXEC)  vflags |= VMA_EXEC;

    /* ── File-backed mmap: "read-into-anon" ──────────────────────── */
    if (!(flags & LINUX_MAP_ANONYMOUS)) {
        if (!t->fds) return (uint32_t)-LINUX_EBADF;
        int ifd = (int)fd;
        if (ifd < 0 || ifd >= t->fd_count) return (uint32_t)-LINUX_EBADF;
        fd_entry_t *fde = &t->fds[ifd];
        if (fde->type != FD_FILE) return (uint32_t)-LINUX_EBADF;
        uint32_t inode = fde->inode;

        /* Create VMA (type ANON — we eagerly populate, no file tracking) */
        if (t->vma)
            vma_insert(t->vma, va_start, va_start + alloc_len, vflags, VMA_TYPE_ANON);

        /* Eagerly allocate frames, read file data into them */
        uint32_t file_offset = pgoff * PAGE_SIZE;  /* mmap2 offset is in pages */
        for (uint32_t i = 0; i < num_pages; i++) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame) return (uint32_t)-LINUX_ENOMEM;
            memset((void *)frame, 0, PAGE_SIZE);

            /* Read file data into this frame */
            uint32_t off = file_offset + i * PAGE_SIZE;
            fs_read_at(inode, (uint8_t *)frame, off, PAGE_SIZE);

            uint32_t pte_flags = PTE_PRESENT | PTE_USER;
            if (prot & LINUX_PROT_WRITE) pte_flags |= PTE_WRITABLE;
            if (!vmm_map_user_page(t->page_dir, va_start + i * PAGE_SIZE,
                                    frame, pte_flags)) {
                pmm_free_frame(frame);
                return (uint32_t)-LINUX_ENOMEM;
            }
        }
        return va_start;
    }

    /* ── Anonymous mmap ──────────────────────────────────────────── */
    if (t->vma) {
        /* Demand paging path: only create VMA, ensure page tables exist.
         * Physical frames will be allocated on first access via page fault. */
        vma_insert(t->vma, va_start, va_start + alloc_len, vflags, VMA_TYPE_ANON);

        /* Ensure page tables exist so we get proper page faults (not GPF) */
        for (uint32_t i = 0; i < num_pages; i++) {
            uint32_t va = va_start + i * PAGE_SIZE;
            vmm_ensure_pt(t->page_dir, va);
        }
    } else {
        /* Legacy path (no VMA table): eager allocation */
        for (uint32_t i = 0; i < num_pages; i++) {
            uint32_t frame = pmm_alloc_frame();
            if (!frame)
                return (uint32_t)-LINUX_ENOMEM;
            memset((void *)frame, 0, PAGE_SIZE);

            uint32_t va = va_start + i * PAGE_SIZE;
            if (!vmm_map_user_page(t->page_dir, va, frame,
                                    PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
                pmm_free_frame(frame);
                return (uint32_t)-LINUX_ENOMEM;
            }

            if (t->num_elf_frames < 64)
                t->elf_frames[t->num_elf_frames++] = frame;
        }
    }

    return va_start;
}

/* ── Linux munmap(addr, len) ──────────────────────────────────────── */

static int32_t linux_sys_munmap(uint32_t addr, uint32_t len) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return -LINUX_EINVAL;

    if (addr & (PAGE_SIZE - 1)) return -LINUX_EINVAL;
    if (len == 0) return -LINUX_EINVAL;

    uint32_t end = (addr + len + PAGE_SIZE - 1) & PAGE_MASK;

    /* Walk the range and unmap any present pages */
    if (t->page_dir && t->page_dir != vmm_get_kernel_pagedir()) {
        for (uint32_t va = addr; va < end; va += PAGE_SIZE) {
            uint32_t pte = vmm_get_pte(t->page_dir, va);
            if (pte & PTE_PRESENT) {
                uint32_t frame = pte & PAGE_MASK;
                vmm_unmap_user_page(t->page_dir, va);
                /* Use refcount if available, otherwise just free */
                if (frame_ref_get(frame) > 0) {
                    if (frame_ref_dec(frame) == 0)
                        pmm_free_frame(frame);
                } else {
                    pmm_free_frame(frame);
                }
            }
        }
    }

    /* Remove VMA entries */
    if (t->vma)
        vma_remove(t->vma, addr, end);

    return 0;
}

/* ── Linux mprotect(addr, len, prot) ─────────────────────────────── */

static int32_t linux_sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return -LINUX_EINVAL;

    if (addr & (PAGE_SIZE - 1)) return -LINUX_EINVAL;
    if (len == 0) return 0;

    uint32_t end = (addr + len + PAGE_SIZE - 1) & PAGE_MASK;

    if (t->vma) {
        /* Split VMAs at boundaries */
        vma_split(t->vma, addr);
        vma_split(t->vma, end);

        /* Update VMA flags for all VMAs in range */
        for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
            vma_t *v = &t->vma->vmas[i];
            if (!v->active) continue;
            if (v->vm_start >= end || v->vm_end <= addr) continue;

            /* Update flags */
            v->vm_flags &= ~(VMA_READ | VMA_WRITE | VMA_EXEC);
            if (prot & LINUX_PROT_READ)  v->vm_flags |= VMA_READ;
            if (prot & LINUX_PROT_WRITE) v->vm_flags |= VMA_WRITE;
            if (prot & LINUX_PROT_EXEC)  v->vm_flags |= VMA_EXEC;
        }
    }

    /* Update PTEs for already-mapped pages */
    if (t->page_dir && t->page_dir != vmm_get_kernel_pagedir()) {
        for (uint32_t va = addr; va < end; va += PAGE_SIZE) {
            uint32_t pte = vmm_get_pte(t->page_dir, va);
            if (!(pte & PTE_PRESENT)) continue;
            /* Skip COW pages — they stay read-only until faulted */
            if (pte & PTE_COW) continue;

            uint32_t new_pte = pte;
            if (prot & LINUX_PROT_WRITE)
                new_pte |= PTE_WRITABLE;
            else
                new_pte &= ~PTE_WRITABLE;

            if (new_pte != pte) {
                uint32_t frame = pte & PAGE_MASK;
                vmm_map_user_page(t->page_dir, va, frame, new_pte & 0xFFF);
            }
        }
    }

    return 0;
}

/* ── Linux set_thread_area(user_desc*) ───────────────────────────── */

static int32_t linux_sys_set_thread_area(struct linux_user_desc *u_info) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || !u_info) return -LINUX_EINVAL;

    t->tls_base = u_info->base_addr;
    gdt_set_gs_base(u_info->base_addr);

    u_info->entry_number = 6;
    return 0;
}

/* ── Linux unlink(path) ──────────────────────────────────────────── */

static int32_t linux_sys_unlink(const char *path) {
    if (!path) return -LINUX_EFAULT;
    /* Check that target exists and is not a directory */
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int ino = fs_resolve_path(path, &parent, name);
    if (ino < 0) return -LINUX_ENOENT;

    inode_t node;
    if (fs_read_inode((uint32_t)ino, &node) < 0) return -LINUX_EIO;
    if (node.type == INODE_DIR) return -LINUX_EISDIR;

    if (fs_delete_file(path) < 0) return -LINUX_EIO;
    return 0;
}

/* ── Linux mkdir(path, mode) ─────────────────────────────────────── */

static int32_t linux_sys_mkdir(const char *path, uint32_t mode) {
    if (!path) return -LINUX_EFAULT;
    (void)mode; /* mode applied via umask — TODO: integrate fully */
    int rc = fs_create_file(path, 1);
    if (rc < 0) return -LINUX_EEXIST;
    return 0;
}

/* ── Linux rmdir(path) ───────────────────────────────────────────── */

static int32_t linux_sys_rmdir(const char *path) {
    if (!path) return -LINUX_EFAULT;
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int ino = fs_resolve_path(path, &parent, name);
    if (ino < 0) return -LINUX_ENOENT;

    inode_t node;
    if (fs_read_inode((uint32_t)ino, &node) < 0) return -LINUX_EIO;
    if (node.type != INODE_DIR) return -LINUX_ENOTDIR;

    /* fs_delete_file() does its own thorough empty-directory check
     * (walks all blocks, verifies only . and .. remain). If the dir
     * is not empty it returns -1, which we map to ENOTEMPTY. */
    int rc = fs_delete_file(path);
    if (rc < 0) return -LINUX_ENOTEMPTY;
    return 0;
}

/* ── Linux rename(old, new) ──────────────────────────────────────── */

static int32_t linux_sys_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -LINUX_EFAULT;
    int rc = fs_rename(oldpath, newpath);
    if (rc < 0) return -LINUX_ENOENT;
    return 0;
}

/* ── Linux chdir(path) ───────────────────────────────────────────── */

static int32_t linux_sys_chdir(const char *path) {
    if (!path) return -LINUX_EFAULT;
    int rc = fs_change_directory(path);
    if (rc < 0) return -LINUX_ENOENT;
    return 0;
}

/* ── Linux fchdir(fd) ────────────────────────────────────────────── */

static int32_t linux_sys_fchdir(uint32_t fd) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type != FD_DIR && fde->type != FD_FILE) return -LINUX_EBADF;

    inode_t node;
    if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
    if (node.type != INODE_DIR) return -LINUX_ENOTDIR;

    if (fs_change_directory_by_inode(fde->inode) < 0) return -LINUX_EIO;
    return 0;
}

/* ── Linux pipe(fds) ─────────────────────────────────────────────── */

static int32_t linux_sys_pipe(int *fds) {
    if (!fds) return -LINUX_EFAULT;
    int tid = task_get_current();
    int rfd, wfd;
    if (pipe_create(&rfd, &wfd, tid) < 0) return -LINUX_EMFILE;
    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

/* ── Linux umask(mask) ───────────────────────────────────────────── */

static int32_t linux_sys_umask(uint32_t mask) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return 0;
    uint32_t old = t->umask;
    t->umask = mask & 0777;
    return (int32_t)old;
}

/* ── Linux time(tloc) ────────────────────────────────────────────── */

static int32_t linux_sys_time(uint32_t *tloc) {
    uint32_t t = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
    if (tloc) *tloc = t;
    return (int32_t)t;
}

/* ── Linux gettimeofday(tv, tz) ──────────────────────────────────── */

static int32_t linux_sys_gettimeofday(struct linux_timeval *tv,
                                       struct linux_timezone *tz) {
    uint32_t unix_time = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
    if (tv) {
        tv->tv_sec = (int32_t)unix_time;
        tv->tv_usec = 0;
    }
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

/* ── Linux clock_gettime(clockid, tp) ────────────────────────────── */

static int32_t linux_sys_clock_gettime(uint32_t clockid,
                                        struct linux_clock_timespec *tp) {
    if (!tp) return -LINUX_EFAULT;
    extern volatile uint32_t pit_ticks;

    switch (clockid) {
        case LINUX_CLOCK_REALTIME: {
            uint32_t unix_time = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
            tp->tv_sec = (int32_t)unix_time;
            tp->tv_nsec = 0;
            return 0;
        }
        case LINUX_CLOCK_MONOTONIC: {
            /* PIT at 120 Hz. Use 64-bit intermediate to avoid both
             * overflow and truncation drift. */
            uint32_t ticks = pit_ticks;
            tp->tv_sec = (int32_t)(ticks / 120);
            tp->tv_nsec = (int32_t)(((uint64_t)(ticks % 120) * 1000000000ULL) / 120);
            return 0;
        }
        default:
            return -LINUX_EINVAL;
    }
}

/* ── Linux readv(fd, iov, iovcnt) ────────────────────────────────── */

static int32_t linux_sys_readv(uint32_t fd, const struct linux_iovec *iov,
                                uint32_t iovcnt) {
    int32_t total = 0;
    for (uint32_t i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0)
            continue;
        int32_t ret = linux_sys_read(fd, (char *)iov[i].iov_base,
                                      iov[i].iov_len);
        if (ret < 0)
            return (total > 0) ? total : ret;
        total += ret;
        if ((uint32_t)ret < iov[i].iov_len)
            break;  /* short read — don't continue to next iovec */
    }
    return total;
}

/* ── Linux statfs64(path, sz, buf) ───────────────────────────────── */

static int32_t linux_sys_statfs64(const char *path, uint32_t sz, void *buf) {
    (void)path;
    if (!buf || sz < sizeof(struct linux_statfs64)) return -LINUX_EINVAL;
    struct linux_statfs64 *st = (struct linux_statfs64 *)buf;
    memset(st, 0, sizeof(*st));
    st->f_type = 0x696D706F;  /* "impo" */
    st->f_bsize = BLOCK_SIZE;
    st->f_frsize = BLOCK_SIZE;
    st->f_blocks = NUM_BLOCKS;
    st->f_bfree = fs_count_free_blocks();
    st->f_bavail = st->f_bfree;
    st->f_files = NUM_INODES;
    st->f_ffree = fs_count_free_inodes();
    st->f_namelen = MAX_NAME_LEN;
    return 0;
}

/* ── Linux fstatfs64(fd, sz, buf) ────────────────────────────────── */

static int32_t linux_sys_fstatfs64(uint32_t fd, uint32_t sz, void *buf) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= (uint32_t)t->fd_count) return -LINUX_EBADF;
    if (t->fds[fd].type == FD_NONE) return -LINUX_EBADF;
    return linux_sys_statfs64(NULL, sz, buf);
}

/* ── Linux poll helper: check readiness of all fds ───────────────── */

static int poll_check_fds(struct linux_pollfd *fds, uint32_t nfds, int tid) {
    task_info_t *t = task_get(tid);
    if (!t) return 0;

    int ready = 0;
    for (uint32_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        int fd = fds[i].fd;

        if (fd < 0) continue;  /* negative fd → skip (POSIX) */

        if (fd >= t->fd_count || t->fds[fd].type == FD_NONE) {
            fds[i].revents = LINUX_POLLNVAL;
            ready++;
            continue;
        }

        fd_entry_t *fde = &t->fds[fd];
        switch (fde->type) {
            case FD_TTY:
                /* TTY is always writable; reading always possible (blocking getchar) */
                if (fds[i].events & LINUX_POLLIN)
                    fds[i].revents |= LINUX_POLLIN;
                if (fds[i].events & LINUX_POLLOUT)
                    fds[i].revents |= LINUX_POLLOUT;
                break;
            case FD_PIPE_R: {
                int r = pipe_poll_query(fde->pipe_id, 0);
                if ((fds[i].events & LINUX_POLLIN) && (r & PIPE_POLL_IN))
                    fds[i].revents |= LINUX_POLLIN;
                if (r & PIPE_POLL_HUP)
                    fds[i].revents |= LINUX_POLLHUP;
                break;
            }
            case FD_PIPE_W: {
                int r = pipe_poll_query(fde->pipe_id, 1);
                if ((fds[i].events & LINUX_POLLOUT) && (r & PIPE_POLL_OUT))
                    fds[i].revents |= LINUX_POLLOUT;
                if (r & PIPE_POLL_ERR)
                    fds[i].revents |= LINUX_POLLERR;
                break;
            }
            case FD_SOCKET: {
                int r = socket_poll_query(fde->pipe_id);
                if ((fds[i].events & LINUX_POLLIN) && (r & PIPE_POLL_IN))
                    fds[i].revents |= LINUX_POLLIN;
                if ((fds[i].events & LINUX_POLLOUT) && (r & PIPE_POLL_OUT))
                    fds[i].revents |= LINUX_POLLOUT;
                if (r & PIPE_POLL_HUP)
                    fds[i].revents |= LINUX_POLLHUP;
                break;
            }
            case FD_FILE:
            case FD_DEV:
                /* Regular files are always ready */
                if (fds[i].events & LINUX_POLLIN)
                    fds[i].revents |= LINUX_POLLIN;
                if (fds[i].events & LINUX_POLLOUT)
                    fds[i].revents |= LINUX_POLLOUT;
                break;
            default:
                break;
        }

        if (fds[i].revents) ready++;
    }
    return ready;
}

/* ── Linux socketcall(102) ────────────────────────────────────────── */

static int32_t linux_sys_socketcall(uint32_t call, uint32_t *args, int tid) {
    task_info_t *t = task_get(tid);
    if (!t) return -LINUX_EINVAL;

    switch (call) {
    case SYS_SOCKET: {
        /* args: domain, type, protocol */
        uint32_t domain = args[0];
        uint32_t type = args[1];
        if (domain != AF_INET)
            return -LINUX_EAFNOSUPPORT;
        int stype;
        if ((type & 0xFF) == SOCK_STREAM) stype = SOCK_STREAM;
        else if ((type & 0xFF) == SOCK_DGRAM) stype = SOCK_DGRAM;
        else return -LINUX_EPROTONOSUPPORT;

        int sock_id = socket_create(stype);
        if (sock_id < 0) return -LINUX_ENOMEM;

        int fd = fd_alloc(tid);
        if (fd < 0) { socket_close(sock_id); return -LINUX_EMFILE; }

        t->fds[fd].type = FD_SOCKET;
        t->fds[fd].pipe_id = sock_id;
        t->fds[fd].flags = 0;
        /* SOCK_NONBLOCK (0x800) and SOCK_CLOEXEC (0x80000) */
        if (type & 0x800) {
            t->fds[fd].flags |= LINUX_O_NONBLOCK;
            socket_set_nonblock(sock_id, 1);
        }
        if (type & 0x80000)
            t->fds[fd].cloexec = 1;
        return fd;
    }
    case SYS_BIND: {
        /* args: sockfd, addr*, addrlen */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        struct linux_sockaddr_in *sa = (struct linux_sockaddr_in *)args[1];
        if (!sa || sa->sin_family != AF_INET)
            return -LINUX_EAFNOSUPPORT;
        uint16_t port = ntohs(sa->sin_port);
        int rc = socket_bind(t->fds[fd].pipe_id, port);
        return (rc < 0) ? -LINUX_EADDRINUSE : 0;
    }
    case SYS_LISTEN: {
        /* args: sockfd, backlog */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        int rc = socket_listen(t->fds[fd].pipe_id, (int)args[1]);
        return (rc < 0) ? -LINUX_EADDRINUSE : 0;
    }
    case SYS_ACCEPT: {
        /* args: sockfd, addr*, addrlen*
         * Non-blocking path — the blocking sleep-loop is in the dispatcher */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        int sock_id = t->fds[fd].pipe_id;

        net_process_packets();
        int new_sock = socket_accept_nb(sock_id);
        if (new_sock == -2)
            return -LINUX_EAGAIN;
        if (new_sock < 0)
            return -LINUX_EINVAL;

        int new_fd = fd_alloc(tid);
        if (new_fd < 0) { socket_close(new_sock); return -LINUX_EMFILE; }

        t->fds[new_fd].type = FD_SOCKET;
        t->fds[new_fd].pipe_id = new_sock;
        t->fds[new_fd].flags = 0;

        /* Fill in client address if requested */
        if (args[1] && args[2]) {
            struct linux_sockaddr_in *sa = (struct linux_sockaddr_in *)args[1];
            uint8_t rip[4]; uint16_t rport;
            socket_get_remote(new_sock, rip, &rport);
            sa->sin_family = AF_INET;
            sa->sin_port = htons(rport);
            memcpy(&sa->sin_addr, rip, 4);
            memset(sa->sin_zero, 0, 8);
            uint32_t *lenp = (uint32_t *)args[2];
            *lenp = sizeof(struct linux_sockaddr_in);
        }
        return new_fd;
    }
    case SYS_CONNECT: {
        /* args: sockfd, addr*, addrlen */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        struct linux_sockaddr_in *sa = (struct linux_sockaddr_in *)args[1];
        if (!sa || sa->sin_family != AF_INET)
            return -LINUX_EAFNOSUPPORT;
        uint8_t ip[4];
        memcpy(ip, &sa->sin_addr, 4);
        uint16_t port = ntohs(sa->sin_port);
        int rc = socket_connect(t->fds[fd].pipe_id, ip, port);
        if (rc < 0) return -LINUX_ECONNREFUSED;
        return 0;
    }
    case SYS_SEND: {
        /* args: sockfd, buf, len, flags */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        int rc = socket_send(t->fds[fd].pipe_id, (void *)args[1], args[2]);
        return (rc < 0) ? -LINUX_ENOTCONN : rc;
    }
    case SYS_RECV: {
        /* args: sockfd, buf, len, flags */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        int sock_id = t->fds[fd].pipe_id;
        int nb = socket_get_nonblock(sock_id) ||
                 (t->fds[fd].flags & LINUX_O_NONBLOCK);
        if (nb) {
            int rc = socket_recv_nb(sock_id, (void *)args[1], args[2]);
            if (rc == -2) return -LINUX_EAGAIN;
            return (rc < 0) ? -LINUX_ENOTCONN : rc;
        }
        int rc = socket_recv(sock_id, (void *)args[1], args[2], 5000);
        return (rc < 0) ? -LINUX_ENOTCONN : rc;
    }
    case SYS_SENDTO: {
        /* args: sockfd, buf, len, flags, dest_addr, addrlen */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        struct linux_sockaddr_in *sa = (struct linux_sockaddr_in *)args[4];
        if (!sa) {
            /* No address = connected socket, use send */
            int rc = socket_send(t->fds[fd].pipe_id, (void *)args[1], args[2]);
            return (rc < 0) ? -LINUX_ENOTCONN : rc;
        }
        uint8_t ip[4];
        memcpy(ip, &sa->sin_addr, 4);
        uint16_t port = ntohs(sa->sin_port);
        int rc = socket_sendto(t->fds[fd].pipe_id, (void *)args[1], args[2],
                               ip, port);
        return (rc < 0) ? -LINUX_ENETUNREACH : rc;
    }
    case SYS_RECVFROM: {
        /* args: sockfd, buf, len, flags, src_addr, addrlen* */
        int fd = (int)args[0];
        if (fd < 0 || fd >= t->fd_count || t->fds[fd].type != FD_SOCKET)
            return -LINUX_ENOTSOCK;
        uint8_t src_ip[4]; uint16_t src_port;
        size_t recv_len = args[2];
        int rc = socket_recvfrom(t->fds[fd].pipe_id, (void *)args[1],
                                 &recv_len, src_ip, &src_port, 5000);
        if (rc < 0) return -LINUX_ENOTCONN;
        if (args[4]) {
            struct linux_sockaddr_in *sa = (struct linux_sockaddr_in *)args[4];
            sa->sin_family = AF_INET;
            sa->sin_port = htons(src_port);
            memcpy(&sa->sin_addr, src_ip, 4);
            memset(sa->sin_zero, 0, 8);
            if (args[5]) {
                uint32_t *lenp = (uint32_t *)args[5];
                *lenp = sizeof(struct linux_sockaddr_in);
            }
        }
        return (int32_t)recv_len;
    }
    case SYS_SHUTDOWN:
    case SYS_SETSOCKOPT:
    case SYS_GETSOCKOPT:
    case SYS_GETSOCKNAME:
    case SYS_GETPEERNAME:
        return 0; /* stub: success */
    default:
        return -LINUX_EINVAL;
    }
}

/* ── Linux nanosleep / clock_nanosleep ────────────────────────────── */

struct linux_timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};

extern volatile uint32_t pit_ticks;

/* Set up nanosleep state.  Returns 1 if the task was put to sleep (caller
 * must invoke schedule()), 0 if zero-sleep (nothing to do), or a negative
 * errno on validation failure.  Does NOT call task_yield() — that would
 * go through int $0x80 which, for ELF tasks, is re-routed to the Linux
 * syscall table where SYS_YIELD (1) == LINUX_SYS_exit.  Instead, the
 * dispatch site must call schedule(regs) directly. */
static int32_t linux_sys_nanosleep_setup(const struct linux_timespec *req,
                                          struct linux_timespec *rem) {
    if (!req) return -LINUX_EINVAL;
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000)
        return -LINUX_EINVAL;

    /* Convert to PIT ticks: PIT runs at 120 Hz */
    uint32_t ms = (uint32_t)req->tv_sec * 1000 + (uint32_t)req->tv_nsec / 1000000;
    uint32_t ticks = (ms * 120 + 999) / 1000;  /* ceiling */
    if (ticks == 0 && ms > 0) ticks = 1;

    /* Zero sleep — just return */
    if (ticks == 0 && req->tv_sec == 0 && req->tv_nsec == 0) {
        if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
        return 0;
    }

    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return -LINUX_EINVAL;

    /* Set sleep target — caller will schedule() */
    t->sleep_until = pit_ticks + ticks;
    t->state = TASK_STATE_SLEEPING;

    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 1;  /* 1 = task is sleeping, caller must schedule() */
}

/* ── Dispatcher ──────────────────────────────────────────────────── */

registers_t* linux_syscall_handler(registers_t* regs) {
    uint32_t nr = regs->eax;

    switch (nr) {
        case LINUX_SYS_exit:
        case LINUX_SYS_exit_group: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (t) {
                pipe_cleanup_task(tid);
                shm_cleanup_task(tid);
                t->exit_code = (int)regs->ebx;
                t->state = TASK_STATE_ZOMBIE;
                t->active = 0;
                task_reparent_children(tid);
                /* Wake parent if blocked in waitpid */
                int ptid = t->parent_tid;
                if (ptid >= 0 && ptid < TASK_MAX) {
                    task_info_t *parent = task_get(ptid);
                    if (parent && parent->state == TASK_STATE_BLOCKED && parent->wait_tid != -1) {
                        if (parent->wait_tid == 0 || parent->wait_tid == tid)
                            parent->state = TASK_STATE_READY;
                    }
                }
            }
            return schedule(regs);
        }

        case LINUX_SYS_waitpid: {
            int pid = (int)regs->ebx;
            int *wstatus = (int *)regs->ecx;
            int options = (int)regs->edx;
            regs->eax = (uint32_t)sys_waitpid(pid, wstatus, options);
            return regs;
        }

        case LINUX_SYS_read:
            regs->eax = (uint32_t)linux_sys_read(regs->ebx,
                                                   (char *)regs->ecx,
                                                   regs->edx);
            return regs;

        case LINUX_SYS_write:
            regs->eax = (uint32_t)linux_sys_write(regs->ebx,
                                                    (const char *)regs->ecx,
                                                    regs->edx);
            return regs;

        case LINUX_SYS_open:
            regs->eax = (uint32_t)linux_sys_open((const char *)regs->ebx,
                                                   regs->ecx,
                                                   regs->edx);
            return regs;

        case LINUX_SYS_close:
            regs->eax = (uint32_t)linux_sys_close(regs->ebx);
            return regs;

        case LINUX_SYS_lseek:
            regs->eax = (uint32_t)linux_sys_lseek(regs->ebx,
                                                    (int32_t)regs->ecx,
                                                    regs->edx);
            return regs;

        case LINUX_SYS_getpid: {
            int tid = task_get_current();
            regs->eax = (uint32_t)task_get_pid(tid);
            return regs;
        }

        case LINUX_SYS_alarm: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (!t) { regs->eax = 0; return regs; }
            /* alarm(seconds) — set SIGALRM timer, return previous remaining */
            uint32_t old_remaining = 0;
            if (t->sig.alarm_ticks > 0)
                old_remaining = t->sig.alarm_ticks / 120 + 1;  /* approx seconds */
            uint32_t seconds = regs->ebx;
            t->sig.alarm_ticks = seconds * 120;  /* 120 Hz PIT */
            regs->eax = old_remaining;
            return regs;
        }

        case LINUX_SYS_kill: {
            int pid = (int)regs->ebx;
            int signum = (int)regs->ecx;
            if (signum <= 0 || signum >= NSIG) { regs->eax = (uint32_t)-LINUX_EINVAL; return regs; }
            int rc;
            if (pid < -1) {
                /* kill(-pgid, sig) → send to process group */
                rc = sig_send_group(-pid, signum);
            } else if (pid == -1) {
                /* kill(-1, sig) → send to all killable tasks */
                rc = -1;
                for (int i = 4; i < TASK_MAX; i++) {
                    task_info_t *t = task_get(i);
                    if (t && t->killable)
                        if (sig_send(i, signum) == 0) rc = 0;
                }
            } else if (pid == 0) {
                /* kill(0, sig) → send to own process group */
                int tid = task_get_current();
                task_info_t *t = task_get(tid);
                rc = t ? sig_send_group(t->pgid, signum) : -1;
            } else {
                rc = sig_send_pid(pid, signum);
            }
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EINVAL : 0;
            return regs;
        }

        case LINUX_SYS_access:
            regs->eax = (uint32_t)linux_sys_access((const char *)regs->ebx,
                                                     regs->ecx);
            return regs;

        case LINUX_SYS_brk:
            regs->eax = linux_sys_brk(regs->ebx);
            return regs;

        case LINUX_SYS_ioctl:
            regs->eax = (uint32_t)linux_sys_ioctl(regs->ebx, regs->ecx,
                                                    regs->edx);
            return regs;

        case LINUX_SYS_readlink:
            regs->eax = (uint32_t)linux_sys_readlink((const char *)regs->ebx,
                                                       (char *)regs->ecx,
                                                       regs->edx);
            return regs;

        case LINUX_SYS_getppid: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            if (!t || t->parent_tid < 0) { regs->eax = 1; return regs; }
            regs->eax = (uint32_t)task_get_pid(t->parent_tid);
            return regs;
        }

        case LINUX_SYS_setpgid: {
            int pid = (int)regs->ebx;
            int pgid = (int)regs->ecx;
            int rc = task_setpgid(pid, pgid);
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EINVAL : 0;
            return regs;
        }

        case LINUX_SYS_setsid: {
            int tid = task_get_current();
            int rc = task_setsid(tid);
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EINVAL : (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_getpgid: {
            int pid = (int)regs->ebx;
            int rc = task_getpgid(pid);
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EINVAL : (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_sigaction: {
            int tid = task_get_current();
            int signum = (int)regs->ebx;
            sig_handler_t handler = (sig_handler_t)regs->ecx;
            /* Simple: treat ECX as the handler address (compat with native) */
            sig_handler_t old = sig_set_handler(tid, signum, handler);
            regs->eax = (uint32_t)old;
            return regs;
        }

        case LINUX_SYS_sigprocmask: {
            int tid = task_get_current();
            int how = (int)regs->ebx;
            uint32_t *setp = (uint32_t *)regs->ecx;
            uint32_t *oldsetp = (uint32_t *)regs->edx;
            uint32_t set_val = setp ? *setp : 0;
            uint32_t old_val = 0;
            int rc = sig_sigprocmask(tid, how, set_val, &old_val);
            if (oldsetp) *oldsetp = old_val;
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EINVAL : 0;
            return regs;
        }

        case LINUX_SYS_wait4: {
            int pid = (int)regs->ebx;
            int *wstatus = (int *)regs->ecx;
            int options = (int)regs->edx;
            regs->eax = (uint32_t)sys_waitpid(pid, wstatus, options);
            return regs;
        }

        case LINUX_SYS_munmap:
            regs->eax = (uint32_t)linux_sys_munmap(regs->ebx, regs->ecx);
            return regs;

        case LINUX_SYS_mprotect:
            regs->eax = (uint32_t)linux_sys_mprotect(regs->ebx, regs->ecx, regs->edx);
            return regs;

        case LINUX_SYS_ftruncate:
            regs->eax = (uint32_t)linux_sys_ftruncate(regs->ebx, regs->ecx);
            return regs;

        case LINUX_SYS_uname:
            regs->eax = (uint32_t)linux_sys_uname(
                (struct linux_utsname *)regs->ebx);
            return regs;

        case LINUX_SYS__llseek:
            regs->eax = (uint32_t)linux_sys_llseek(regs->ebx,
                                                     regs->ecx,
                                                     regs->edx,
                                                     (uint64_t *)regs->esi,
                                                     regs->edi);
            return regs;

        case LINUX_SYS_writev:
            regs->eax = (uint32_t)linux_sys_writev(
                regs->ebx,
                (const struct linux_iovec *)regs->ecx,
                regs->edx);
            return regs;

        case LINUX_SYS_getcwd:
            regs->eax = (uint32_t)linux_sys_getcwd((char *)regs->ebx,
                                                     regs->ecx);
            return regs;

        case LINUX_SYS_mmap2:
            regs->eax = linux_sys_mmap2(regs->ebx, regs->ecx, regs->edx,
                                         regs->esi, regs->edi, regs->ebp);
            return regs;

        case LINUX_SYS_stat64:
            regs->eax = (uint32_t)linux_sys_stat64(
                (const char *)regs->ebx,
                (struct linux_stat64 *)regs->ecx);
            return regs;

        case LINUX_SYS_lstat64:
            regs->eax = (uint32_t)linux_sys_lstat64(
                (const char *)regs->ebx,
                (struct linux_stat64 *)regs->ecx);
            return regs;

        case LINUX_SYS_fstat64:
            regs->eax = (uint32_t)linux_sys_fstat64(regs->ebx,
                (struct linux_stat64 *)regs->ecx);
            return regs;

        case LINUX_SYS_getuid32:
            regs->eax = (uint32_t)user_get_current_uid();
            return regs;

        case LINUX_SYS_getgid32:
            regs->eax = (uint32_t)user_get_current_gid();
            return regs;

        case LINUX_SYS_geteuid32:
            regs->eax = (uint32_t)user_get_current_uid();
            return regs;

        case LINUX_SYS_getegid32:
            regs->eax = (uint32_t)user_get_current_gid();
            return regs;

        case LINUX_SYS_getdents64:
            regs->eax = (uint32_t)linux_sys_getdents64(regs->ebx,
                (void *)regs->ecx, regs->edx);
            return regs;

        case LINUX_SYS_fcntl64:
            regs->eax = (uint32_t)linux_sys_fcntl64(regs->ebx,
                                                      regs->ecx,
                                                      regs->edx);
            return regs;

        case LINUX_SYS_fork: {
            int rc = sys_clone(LINUX_SIGCHLD, 0, regs);
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_vfork: {
            /* vfork is fork with shared address space (CLONE_VM) */
            int rc = sys_clone(LINUX_CLONE_VM | LINUX_SIGCHLD, 0, regs);
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_clone: {
            /* Linux i386 clone: EBX=flags, ECX=child_stack, EDX=ptid, ESI=ctid, EDI=tls */
            uint32_t flags = regs->ebx;
            uint32_t child_stack = regs->ecx;
            int rc = sys_clone(flags, child_stack, regs);
            regs->eax = (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_dup: {
            int tid = task_get_current();
            int rc = fd_dup(tid, (int)regs->ebx);
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EBADF : (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_dup2: {
            int tid = task_get_current();
            int rc = fd_dup2(tid, (int)regs->ebx, (int)regs->ecx);
            regs->eax = (rc < 0) ? (uint32_t)-LINUX_EBADF : (uint32_t)rc;
            return regs;
        }

        case LINUX_SYS_futex: {
            uint32_t *uaddr = (uint32_t *)regs->ebx;
            int op = (int)regs->ecx & 0x7f;  /* strip FUTEX_PRIVATE_FLAG */
            uint32_t val = regs->edx;
            regs->eax = (uint32_t)sys_futex(uaddr, op, val);
            return regs;
        }

        case LINUX_SYS_set_thread_area:
            regs->eax = (uint32_t)linux_sys_set_thread_area(
                (struct linux_user_desc *)regs->ebx);
            return regs;

        case LINUX_SYS_set_tid_address: {
            /* musl startup calls this — just return the current tid */
            int tid = task_get_current();
            regs->eax = (uint32_t)task_get_pid(tid);
            return regs;
        }

        case LINUX_SYS_execve: {
            const char *fn = (const char *)regs->ebx;
            const char **uargv = (const char **)regs->ecx;
            /* const char **envp = (const char **)regs->edx; */
            /* TODO: pass envp to new process — currently ignored.
             * Programs relying on PATH/HOME/TERM after exec get nothing. */

            int ex_argc = 0;
            if (uargv) {
                while (uargv[ex_argc] != NULL && ex_argc < 32) ex_argc++;
            }

            int cur_tid = task_get_current();
            int rc = elf_exec(cur_tid, fn, ex_argc, uargv);
            if (rc < 0) {
                regs->eax = (uint32_t)rc;
                return regs;
            }

            /* Success: task image replaced — return new kernel stack frame.
             * The old user code is gone; isr_common will iret into the new ELF. */
            task_info_t *t = task_get(cur_tid);
            return (registers_t *)t->esp;
        }

        case LINUX_SYS_nanosleep: {
            int32_t rc = linux_sys_nanosleep_setup(
                (const struct linux_timespec *)regs->ebx,
                (struct linux_timespec *)regs->ecx);
            if (rc == 1) {
                /* Task is sleeping — invoke scheduler directly */
                regs->eax = 0;
                return schedule(regs);
            }
            regs->eax = (rc < 0) ? (uint32_t)rc : 0;
            return regs;
        }

        case LINUX_SYS_clock_nanosleep: {
            /* clock_nanosleep(clockid, flags, req, rem)
             * EBX=clockid, ECX=flags, EDX=req, ESI=rem
             * TODO: handle TIMER_ABSTIME flag (ECX & 1) — currently treats
             * all times as relative, which is wrong for absolute timestamps. */
            int32_t rc = linux_sys_nanosleep_setup(
                (const struct linux_timespec *)regs->edx,
                (struct linux_timespec *)regs->esi);
            if (rc == 1) {
                regs->eax = 0;
                return schedule(regs);
            }
            regs->eax = (rc < 0) ? (uint32_t)rc : 0;
            return regs;
        }

        /* ── Phase 4 syscalls ─────────────────────────────────────── */

        case LINUX_SYS_unlink:
            regs->eax = (uint32_t)linux_sys_unlink((const char *)regs->ebx);
            return regs;

        case LINUX_SYS_chdir:
            regs->eax = (uint32_t)linux_sys_chdir((const char *)regs->ebx);
            return regs;

        case LINUX_SYS_time:
            regs->eax = (uint32_t)linux_sys_time((uint32_t *)regs->ebx);
            return regs;

        case LINUX_SYS_rename:
            regs->eax = (uint32_t)linux_sys_rename((const char *)regs->ebx,
                                                     (const char *)regs->ecx);
            return regs;

        case LINUX_SYS_mkdir:
            regs->eax = (uint32_t)linux_sys_mkdir((const char *)regs->ebx,
                                                    regs->ecx);
            return regs;

        case LINUX_SYS_rmdir:
            regs->eax = (uint32_t)linux_sys_rmdir((const char *)regs->ebx);
            return regs;

        case LINUX_SYS_pipe:
            regs->eax = (uint32_t)linux_sys_pipe((int *)regs->ebx);
            return regs;

        case LINUX_SYS_umask:
            regs->eax = (uint32_t)linux_sys_umask(regs->ebx);
            return regs;

        case LINUX_SYS_getpgrp: {
            int tid = task_get_current();
            task_info_t *t = task_get(tid);
            regs->eax = t ? (uint32_t)t->pgid : 0;
            return regs;
        }

        case LINUX_SYS_gettimeofday:
            regs->eax = (uint32_t)linux_sys_gettimeofday(
                (struct linux_timeval *)regs->ebx,
                (struct linux_timezone *)regs->ecx);
            return regs;

        case LINUX_SYS_fchdir:
            regs->eax = (uint32_t)linux_sys_fchdir(regs->ebx);
            return regs;

        case LINUX_SYS_readv:
            regs->eax = (uint32_t)linux_sys_readv(
                regs->ebx,
                (const struct linux_iovec *)regs->ecx,
                regs->edx);
            return regs;

        case LINUX_SYS_poll: {
            struct linux_pollfd *fds = (struct linux_pollfd *)regs->ebx;
            uint32_t nfds = regs->ecx;
            int timeout_ms = (int)regs->edx;
            int tid = task_get_current();

            /* Compute deadline in PIT ticks */
            uint32_t deadline = 0;
            if (timeout_ms > 0)
                deadline = pit_ticks + (uint32_t)((timeout_ms * 120 + 999) / 1000);

            while (1) {
                int ready = poll_check_fds(fds, nfds, tid);
                if (ready > 0 || timeout_ms == 0) {
                    regs->eax = (uint32_t)ready;
                    return regs;
                }
                if (timeout_ms > 0 && (int32_t)(pit_ticks - deadline) >= 0) {
                    regs->eax = 0;  /* timeout */
                    return regs;
                }
                /* Check for pending signals — break with EINTR */
                task_info_t *pt = task_get(tid);
                if (!pt) { regs->eax = (uint32_t)-LINUX_EINVAL; return regs; }
                if (pt->sig.pending & ~pt->sig.blocked) {
                    regs->eax = (uint32_t)-LINUX_EINTR;
                    return regs;
                }
                /* Sleep for ~16ms then re-check */
                pt->sleep_until = pit_ticks + 2;
                pt->state = TASK_STATE_SLEEPING;
                regs = schedule(regs);
            }
        }

        case LINUX_SYS_setuid32:
            regs->eax = 0;  /* always root — accept + ignore */
            return regs;

        case LINUX_SYS_setgid32:
            regs->eax = 0;  /* always root — accept + ignore */
            return regs;

        case LINUX_SYS_clock_gettime:
            regs->eax = (uint32_t)linux_sys_clock_gettime(
                regs->ebx,
                (struct linux_clock_timespec *)regs->ecx);
            return regs;

        case LINUX_SYS_statfs64:
            regs->eax = (uint32_t)linux_sys_statfs64(
                (const char *)regs->ebx, regs->ecx,
                (void *)regs->edx);
            return regs;

        case LINUX_SYS_fstatfs64:
            regs->eax = (uint32_t)linux_sys_fstatfs64(
                regs->ebx, regs->ecx,
                (void *)regs->edx);
            return regs;

        /* ── Phase 6: socketcall ──────────────────────────────────── */

        case LINUX_SYS_socketcall: {
            uint32_t scall = regs->ebx;
            uint32_t *sargs = (uint32_t *)regs->ecx;
            int tid = task_get_current();

            /* Blocking accept: sleep-loop with schedule(regs) */
            if (scall == SYS_ACCEPT) {
                task_info_t *t = task_get(tid);
                if (!t) { regs->eax = (uint32_t)-LINUX_EINVAL; return regs; }
                int afd = (int)sargs[0];
                if (afd < 0 || afd >= t->fd_count ||
                    t->fds[afd].type != FD_SOCKET) {
                    regs->eax = (uint32_t)-LINUX_ENOTSOCK;
                    return regs;
                }

                int nb = socket_get_nonblock(t->fds[afd].pipe_id) ||
                         (t->fds[afd].flags & LINUX_O_NONBLOCK);

                while (1) {
                    int32_t rc = linux_sys_socketcall(scall, sargs, tid);
                    if (rc != -LINUX_EAGAIN || nb) {
                        regs->eax = (uint32_t)rc;
                        return regs;
                    }
                    /* Check for signals */
                    t = task_get(tid);
                    if (!t) { regs->eax = (uint32_t)-LINUX_EINVAL; return regs; }
                    if (t->sig.pending & ~t->sig.blocked) {
                        regs->eax = (uint32_t)-LINUX_EINTR;
                        return regs;
                    }
                    /* Sleep ~16ms then re-check */
                    t->sleep_until = pit_ticks + 2;
                    t->state = TASK_STATE_SLEEPING;
                    regs = schedule(regs);
                }
            }

            regs->eax = (uint32_t)linux_sys_socketcall(scall, sargs, tid);
            return regs;
        }

        default:
            regs->eax = (uint32_t)-LINUX_ENOSYS;
            return regs;
    }
}
