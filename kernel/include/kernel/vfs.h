#ifndef _KERNEL_VFS_H
#define _KERNEL_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/fs.h>

/* ── VFS — Virtual Filesystem Switch ────────────────────────────── *
 *
 * Abstracts filesystem operations behind a mount table.  Each mounted
 * filesystem provides a vfs_ops_t with its callbacks.  Path resolution
 * uses longest-prefix matching to dispatch to the correct backend.
 *
 * The public fs_* API in fs.c remains unchanged — it routes through
 * VFS when appropriate, falling back to the root imposfs otherwise.
 */

#define VFS_MAX_MOUNTS  16
#define VFS_MAX_PREFIX  64

/* ── Filesystem operations table ────────────────────────────────── */

typedef struct vfs_ops {
    const char *name;

    /* Mount/unmount lifecycle */
    int  (*mount)(void *private_data);
    void (*unmount)(void *private_data);

    /* File operations — path is relative to mount point */
    int  (*create)(void *priv, const char *path, uint8_t is_directory);
    int  (*unlink)(void *priv, const char *path);
    int  (*read_file)(void *priv, const char *path, uint8_t *buf, size_t *size);
    int  (*write_file)(void *priv, const char *path, const uint8_t *data, size_t size);

    /* Inode-level I/O */
    int  (*read_at)(void *priv, uint32_t ino, uint8_t *buf, uint32_t off, uint32_t count);
    int  (*write_at)(void *priv, uint32_t ino, const uint8_t *data, uint32_t off, uint32_t count);

    /* Directory listing: writes into caller-provided array, returns count */
    int  (*readdir)(void *priv, const char *path, fs_dir_entry_info_t *out, int max);

    /* Stat-like: read inode metadata */
    int  (*stat)(void *priv, const char *path, inode_t *out);

    /* Metadata mutations */
    int  (*chmod)(void *priv, const char *path, uint16_t mode);
    int  (*chown)(void *priv, const char *path, uint16_t uid, uint16_t gid);
    int  (*rename)(void *priv, const char *old_name, const char *new_name);
    int  (*truncate)(void *priv, const char *path, uint32_t new_size);

    /* Symlinks */
    int  (*symlink)(void *priv, const char *target, const char *linkname);
    int  (*readlink)(void *priv, const char *path, char *buf, size_t bufsize);

    /* Sync to backing store */
    int  (*sync)(void *priv);
} vfs_ops_t;

/* ── Mount entry ────────────────────────────────────────────────── */

typedef struct {
    char        prefix[VFS_MAX_PREFIX]; /* mount path, e.g. "/proc" */
    uint32_t    prefix_len;
    vfs_ops_t  *ops;
    void       *private_data;
    int         active;
} vfs_mount_t;

/* ── Public API ─────────────────────────────────────────────────── */

void vfs_init(void);
int  vfs_mount(const char *path, vfs_ops_t *ops, void *private_data);
int  vfs_unmount(const char *path);

/* Resolve a path to the responsible mount.  Returns mount entry and
 * sets *rel_path to the path relative to the mount point.
 * Returns NULL if no mount matches (use root FS). */
vfs_mount_t *vfs_resolve(const char *path, const char **rel_path);

/* Get mount table for debug/enumeration */
int vfs_get_mounts(vfs_mount_t **out_table, int *out_count);

#endif
