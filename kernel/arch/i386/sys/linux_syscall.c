#include <kernel/linux_syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/idt.h>
#include <kernel/pipe.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/fs.h>
#include <kernel/user.h>
#include <kernel/hostname.h>
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
        case INODE_CHARDEV: fd_type = FD_DEV;  break;
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

    return fd;
}

/* ── Linux close(fd) ────────────────────────────────────────────── */

static int32_t linux_sys_close(uint32_t fd) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    /* For pipes, do proper refcount management */
    if (fde->type == FD_PIPE_R || fde->type == FD_PIPE_W) {
        pipe_close((int)fd, tid);
        return 0;
    }

    /* For files/dirs/devs/tty: just clear */
    fde->type = FD_NONE;
    fde->inode = 0;
    fde->offset = 0;
    fde->flags = 0;
    fde->pipe_id = 0;
    return 0;
}

/* ── Linux read(fd, buf, count) ─────────────────────────────────── */

static int32_t linux_sys_read(uint32_t fd, char *buf, uint32_t count) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

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
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

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

        case FD_FILE:
            /* TODO: file write at offset — not needed for BusyBox read-only tools */
            return -LINUX_ENOSYS;

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

/* ── Linux lseek(fd, offset, whence) ────────────────────────────── */

static int32_t linux_sys_lseek(uint32_t fd, int32_t offset, uint32_t whence) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

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
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

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

    inode_t node;
    if (fs_read_inode(fde->inode, &node) < 0) return -LINUX_EIO;
    fill_stat64(statbuf, fde->inode, &node);
    return 0;
}

/* ── Linux getdents64(fd, dirp, count) ──────────────────────────── */

static int32_t linux_sys_getdents64(uint32_t fd, void *dirp, uint32_t count) {
    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

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
            if (entries[e].inode < 256) { /* NUM_INODES */
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
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

    fd_entry_t *fde = &t->fds[fd];
    if (fde->type == FD_NONE) return -LINUX_EBADF;

    switch (cmd) {
        case LINUX_F_GETFD: return 0;       /* no CLOEXEC tracking */
        case LINUX_F_SETFD: return 0;       /* noop */
        case LINUX_F_GETFL: return (int32_t)fde->flags;
        case LINUX_F_SETFL:
            /* Only allow O_NONBLOCK and O_APPEND to be changed */
            fde->flags = (fde->flags & ~(LINUX_O_NONBLOCK | LINUX_O_APPEND))
                       | (arg & (LINUX_O_NONBLOCK | LINUX_O_APPEND));
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
    if (!t || fd >= MAX_FDS) return -LINUX_EBADF;

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
            default:
                return -LINUX_ENOSYS;
        }
    }

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

    if (new_brk == 0 || new_brk <= t->brk_current)
        return t->brk_current;

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

    t->brk_current = new_brk;
    return new_brk;
}

/* ── Linux mmap2(addr, len, prot, flags, fd, pgoff) ──────────────── */

static uint32_t linux_sys_mmap2(uint32_t addr, uint32_t len, uint32_t prot,
                                 uint32_t flags, uint32_t fd, uint32_t pgoff) {
    (void)addr; (void)prot; (void)fd; (void)pgoff;

    int tid = task_get_current();
    task_info_t *t = task_get(tid);
    if (!t) return (uint32_t)-LINUX_ENOMEM;

    if (!(flags & LINUX_MAP_ANONYMOUS))
        return (uint32_t)-LINUX_ENOSYS;

    uint32_t num_pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t va_start = t->mmap_next;

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

    t->mmap_next += num_pages * PAGE_SIZE;
    return va_start;
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
                t->state = TASK_STATE_ZOMBIE;
                t->active = 0;
            }
            return schedule(regs);
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

        case LINUX_SYS_munmap:
            regs->eax = 0;  /* stub — cleanup on task exit */
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

        default:
            regs->eax = (uint32_t)-LINUX_ENOSYS;
            return regs;
    }
}
