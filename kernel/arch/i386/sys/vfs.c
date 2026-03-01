/*
 * vfs.c — Virtual Filesystem Switch
 *
 * Mount table with longest-prefix-match resolution.
 * The root filesystem (imposfs in fs.c) is NOT registered here —
 * it's the fallback when no VFS mount matches a given path.
 * Special filesystems (procfs, devfs, tmpfs) register via vfs_mount().
 */

#include <kernel/vfs.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

static vfs_mount_t mount_table[VFS_MAX_MOUNTS];
static int num_mounts = 0;

void vfs_init(void) {
    memset(mount_table, 0, sizeof(mount_table));
    num_mounts = 0;
    DBG("[VFS] Initialized (max %d mounts)", VFS_MAX_MOUNTS);
}

int vfs_mount(const char *path, vfs_ops_t *ops, void *private_data) {
    if (!path || !ops) return -1;
    if (num_mounts >= VFS_MAX_MOUNTS) {
        DBG("[VFS] Mount table full");
        return -1;
    }

    /* Check for duplicate mount */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_table[i].active &&
            strcmp(mount_table[i].prefix, path) == 0) {
            DBG("[VFS] Already mounted at %s", path);
            return -1;
        }
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mount_table[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    vfs_mount_t *m = &mount_table[slot];
    strncpy(m->prefix, path, VFS_MAX_PREFIX - 1);
    m->prefix[VFS_MAX_PREFIX - 1] = '\0';
    m->prefix_len = strlen(m->prefix);
    m->ops = ops;
    m->private_data = private_data;
    m->active = 1;
    num_mounts++;

    /* Call mount callback if provided */
    if (ops->mount) {
        ops->mount(private_data);
    }

    DBG("[VFS] Mounted '%s' at %s", ops->name ? ops->name : "?", path);
    return 0;
}

int vfs_unmount(const char *path) {
    if (!path) return -1;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_table[i].active &&
            strcmp(mount_table[i].prefix, path) == 0) {
            if (mount_table[i].ops->unmount) {
                mount_table[i].ops->unmount(mount_table[i].private_data);
            }
            mount_table[i].active = 0;
            num_mounts--;
            DBG("[VFS] Unmounted %s", path);
            return 0;
        }
    }
    return -1;
}

/* Longest-prefix match resolution.
 * Returns the mount entry whose prefix most specifically matches the path.
 * Sets *rel_path to the portion of path after the mount prefix. */
vfs_mount_t *vfs_resolve(const char *path, const char **rel_path) {
    if (!path) return NULL;

    vfs_mount_t *best = NULL;
    uint32_t best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mount_table[i].active) continue;

        uint32_t plen = mount_table[i].prefix_len;

        /* Check if path starts with this mount's prefix */
        if (strncmp(path, mount_table[i].prefix, plen) == 0) {
            /* Must match at a path boundary:
             * either exact match, or next char is '/' or path ends */
            if (path[plen] == '\0' || path[plen] == '/') {
                if (plen > best_len) {
                    best = &mount_table[i];
                    best_len = plen;
                }
            }
        }
    }

    if (best && rel_path) {
        const char *r = path + best_len;
        /* Skip leading slash in relative path */
        if (*r == '/') r++;
        *rel_path = r;
    }

    return best;
}

int vfs_get_mounts(vfs_mount_t **out_table, int *out_count) {
    if (out_table) *out_table = mount_table;
    if (out_count) *out_count = num_mounts;
    return 0;
}
