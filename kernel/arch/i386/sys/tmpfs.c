/*
 * tmpfs.c — RAM-backed Temporary Filesystem
 *
 * Mounted at /tmp.  All data lives in memory, lost on reboot.
 * Simplified inode/block system: 1024 inodes, 4096 blocks (16MB).
 * Supports files, directories, permissions, and timestamps.
 */

#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/rtc.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Geometry ──────────────────────────────────────────────────────── */

#define TMPFS_NUM_INODES  1024
#define TMPFS_NUM_BLOCKS  4096
#define TMPFS_BLOCK_SIZE  4096
#define TMPFS_DIRECT      8
#define TMPFS_MAX_NAME    28

/* ── On-disk structures (all in RAM) ──────────────────────────────── */

typedef struct {
    uint32_t inode;
    char     name[TMPFS_MAX_NAME];
} tmpfs_dirent_t;

typedef struct {
    uint8_t  type;       /* INODE_FREE, INODE_FILE, INODE_DIR */
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    uint32_t blocks[TMPFS_DIRECT];
    uint8_t  num_blocks;
    uint32_t created_at;
    uint32_t modified_at;
} tmpfs_inode_t;

/* ── State ─────────────────────────────────────────────────────────── */

static tmpfs_inode_t  tmpfs_inodes[TMPFS_NUM_INODES];
static uint8_t       *tmpfs_data = NULL;
static uint8_t        tmpfs_inode_bmp[TMPFS_NUM_INODES / 8];
static uint8_t        tmpfs_block_bmp[TMPFS_NUM_BLOCKS / 8];

#define TMPFS_BLK(i) (tmpfs_data + (size_t)(i) * TMPFS_BLOCK_SIZE)

static void tbmp_set(uint8_t *map, uint32_t bit)   { map[bit/8] |= (1 << (bit%8)); }
static void tbmp_clear(uint8_t *map, uint32_t bit)  { map[bit/8] &= ~(1 << (bit%8)); }
static int  tbmp_test(const uint8_t *map, uint32_t bit) { return (map[bit/8] >> (bit%8)) & 1; }

static uint32_t tmpfs_alloc_inode(void) {
    for (uint32_t i = 1; i < TMPFS_NUM_INODES; i++) {
        if (!tbmp_test(tmpfs_inode_bmp, i)) {
            tbmp_set(tmpfs_inode_bmp, i);
            memset(&tmpfs_inodes[i], 0, sizeof(tmpfs_inode_t));
            return i;
        }
    }
    return 0;
}

static void tmpfs_free_inode(uint32_t ino) {
    if (ino == 0 || ino >= TMPFS_NUM_INODES) return;
    tbmp_clear(tmpfs_inode_bmp, ino);
    tmpfs_inodes[ino].type = INODE_FREE;
}

static uint32_t tmpfs_alloc_block(void) {
    for (uint32_t i = 0; i < TMPFS_NUM_BLOCKS; i++) {
        if (!tbmp_test(tmpfs_block_bmp, i)) {
            tbmp_set(tmpfs_block_bmp, i);
            memset(TMPFS_BLK(i), 0, TMPFS_BLOCK_SIZE);
            return i;
        }
    }
    return 0xFFFFFFFF; /* no space */
}

static void tmpfs_free_block(uint32_t blk) {
    if (blk >= TMPFS_NUM_BLOCKS) return;
    tbmp_clear(tmpfs_block_bmp, blk);
}

/* ── Directory helpers ─────────────────────────────────────────────── */

static int tmpfs_dir_lookup(uint32_t dir_ino, const char *name) {
    tmpfs_inode_t *dir = &tmpfs_inodes[dir_ino];
    if (dir->type != INODE_DIR) return -1;

    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(dir->blocks[b]);
        int per_block = TMPFS_BLOCK_SIZE / sizeof(tmpfs_dirent_t);
        for (int j = 0; j < per_block; j++) {
            if (entries[j].inode != 0 &&
                strncmp(entries[j].name, name, TMPFS_MAX_NAME) == 0)
                return (int)entries[j].inode;
        }
    }
    return -1;
}

static int tmpfs_dir_add(uint32_t dir_ino, const char *name, uint32_t child_ino) {
    tmpfs_inode_t *dir = &tmpfs_inodes[dir_ino];

    /* Try to find a free slot in existing blocks */
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(dir->blocks[b]);
        int per_block = TMPFS_BLOCK_SIZE / sizeof(tmpfs_dirent_t);
        for (int j = 0; j < per_block; j++) {
            if (entries[j].inode == 0) {
                entries[j].inode = child_ino;
                strncpy(entries[j].name, name, TMPFS_MAX_NAME - 1);
                entries[j].name[TMPFS_MAX_NAME - 1] = '\0';
                return 0;
            }
        }
    }

    /* Need new block */
    if (dir->num_blocks >= TMPFS_DIRECT) return -1;
    uint32_t blk = tmpfs_alloc_block();
    if (blk == 0xFFFFFFFF) return -1;

    dir->blocks[dir->num_blocks++] = blk;
    tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(blk);
    entries[0].inode = child_ino;
    strncpy(entries[0].name, name, TMPFS_MAX_NAME - 1);
    entries[0].name[TMPFS_MAX_NAME - 1] = '\0';
    return 0;
}

static int tmpfs_dir_remove(uint32_t dir_ino, const char *name) {
    tmpfs_inode_t *dir = &tmpfs_inodes[dir_ino];

    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(dir->blocks[b]);
        int per_block = TMPFS_BLOCK_SIZE / sizeof(tmpfs_dirent_t);
        for (int j = 0; j < per_block; j++) {
            if (entries[j].inode != 0 &&
                strncmp(entries[j].name, name, TMPFS_MAX_NAME) == 0) {
                entries[j].inode = 0;
                entries[j].name[0] = '\0';
                return 0;
            }
        }
    }
    return -1;
}

/* Resolve path relative to tmpfs root (inode 0 = root dir).
 * Returns inode number or -1. Optionally sets *parent and *basename. */
static int tmpfs_resolve(const char *path, uint32_t *out_parent, char *out_name) {
    if (!path || *path == '\0') {
        if (out_parent) *out_parent = 0;
        if (out_name)   out_name[0] = '\0';
        return 0; /* root */
    }
    if (*path == '/') path++;
    if (*path == '\0') return 0; /* root */

    uint32_t current = 0; /* tmpfs root inode */
    char component[TMPFS_MAX_NAME];
    uint32_t parent = 0;

    while (*path) {
        /* Extract next path component */
        const char *slash = strchr(path, '/');
        size_t clen;
        if (slash) {
            clen = (size_t)(slash - path);
        } else {
            clen = strlen(path);
        }
        if (clen == 0) { path++; continue; }
        if (clen >= TMPFS_MAX_NAME) clen = TMPFS_MAX_NAME - 1;

        memcpy(component, path, clen);
        component[clen] = '\0';

        parent = current;
        int child = tmpfs_dir_lookup(current, component);

        if (slash) {
            /* More path components — must be a directory */
            if (child < 0) return -1;
            current = (uint32_t)child;
            path = slash + 1;
        } else {
            /* Last component */
            if (out_parent) *out_parent = parent;
            if (out_name) {
                strncpy(out_name, component, TMPFS_MAX_NAME - 1);
                out_name[TMPFS_MAX_NAME - 1] = '\0';
            }
            return child; /* may be -1 if not found */
        }
    }

    if (out_parent) *out_parent = parent;
    if (out_name)   out_name[0] = '\0';
    return (int)current;
}

/* ── VFS ops ───────────────────────────────────────────────────────── */

static int tmpfs_create(void *priv, const char *path, uint8_t is_directory)
{
    (void)priv;
    uint32_t parent;
    char name[TMPFS_MAX_NAME];

    int existing = tmpfs_resolve(path, &parent, name);
    if (existing >= 0) return -1; /* already exists */
    if (name[0] == '\0') return -1;

    /* Parent must exist and be a directory */
    if (tmpfs_inodes[parent].type != INODE_DIR) return -1;

    uint32_t ino = tmpfs_alloc_inode();
    if (ino == 0) return -1;

    tmpfs_inodes[ino].type = is_directory ? INODE_DIR : INODE_FILE;
    tmpfs_inodes[ino].mode = is_directory ? 0755 : 0644;
    tmpfs_inodes[ino].created_at = rtc_get_epoch();
    tmpfs_inodes[ino].modified_at = tmpfs_inodes[ino].created_at;

    if (is_directory) {
        /* Allocate initial block for . and .. */
        uint32_t blk = tmpfs_alloc_block();
        if (blk == 0xFFFFFFFF) { tmpfs_free_inode(ino); return -1; }

        tmpfs_inodes[ino].blocks[0] = blk;
        tmpfs_inodes[ino].num_blocks = 1;

        tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(blk);
        entries[0].inode = ino;
        strncpy(entries[0].name, ".", TMPFS_MAX_NAME - 1);
        entries[1].inode = parent;
        strncpy(entries[1].name, "..", TMPFS_MAX_NAME - 1);
    }

    if (tmpfs_dir_add(parent, name, ino) != 0) {
        tmpfs_free_inode(ino);
        return -1;
    }

    return 0;
}

static int tmpfs_unlink(void *priv, const char *path)
{
    (void)priv;
    uint32_t parent;
    char name[TMPFS_MAX_NAME];

    int ino = tmpfs_resolve(path, &parent, name);
    if (ino < 0 || ino == 0) return -1; /* not found or root */

    tmpfs_inode_t *node = &tmpfs_inodes[ino];

    /* Free data blocks */
    for (uint8_t b = 0; b < node->num_blocks; b++) {
        tmpfs_free_block(node->blocks[b]);
    }

    tmpfs_free_inode(ino);
    tmpfs_dir_remove(parent, name);
    return 0;
}

static int tmpfs_read_file(void *priv, const char *path,
                           uint8_t *buf, size_t *size)
{
    (void)priv;
    int ino = tmpfs_resolve(path, NULL, NULL);
    if (ino < 0) return -1;

    tmpfs_inode_t *node = &tmpfs_inodes[ino];
    if (node->type != INODE_FILE) return -1;

    uint32_t to_read = node->size;

    uint32_t done = 0;
    for (uint8_t b = 0; b < node->num_blocks && done < to_read; b++) {
        uint32_t chunk = to_read - done;
        if (chunk > TMPFS_BLOCK_SIZE) chunk = TMPFS_BLOCK_SIZE;
        memcpy(buf + done, TMPFS_BLK(node->blocks[b]), chunk);
        done += chunk;
    }
    *size = done;
    return 0;
}

static int tmpfs_write_file(void *priv, const char *path,
                            const uint8_t *data, size_t size)
{
    (void)priv;
    uint32_t parent;
    char name[TMPFS_MAX_NAME];
    int ino = tmpfs_resolve(path, &parent, name);

    /* Auto-create if not found */
    if (ino < 0) {
        if (name[0] == '\0') return -1;
        if (tmpfs_create(priv, path, 0) != 0) return -1;
        ino = tmpfs_resolve(path, NULL, NULL);
        if (ino < 0) return -1;
    }

    tmpfs_inode_t *node = &tmpfs_inodes[ino];
    if (node->type != INODE_FILE) return -1;

    /* Free old blocks */
    for (uint8_t b = 0; b < node->num_blocks; b++) {
        tmpfs_free_block(node->blocks[b]);
    }
    node->num_blocks = 0;
    node->size = 0;

    /* Write new data */
    uint32_t written = 0;
    while (written < (uint32_t)size) {
        if (node->num_blocks >= TMPFS_DIRECT) break; /* max direct blocks */

        uint32_t blk = tmpfs_alloc_block();
        if (blk == 0xFFFFFFFF) break;

        uint32_t chunk = (uint32_t)size - written;
        if (chunk > TMPFS_BLOCK_SIZE) chunk = TMPFS_BLOCK_SIZE;

        memcpy(TMPFS_BLK(blk), data + written, chunk);
        node->blocks[node->num_blocks++] = blk;
        written += chunk;
    }

    node->size = written;
    node->modified_at = rtc_get_epoch();
    return 0;
}

static int tmpfs_readdir(void *priv, const char *path,
                         fs_dir_entry_info_t *out, int max)
{
    (void)priv;
    int ino = tmpfs_resolve(path, NULL, NULL);
    if (ino < 0) return -1;

    tmpfs_inode_t *dir = &tmpfs_inodes[ino];
    if (dir->type != INODE_DIR) return -1;

    int count = 0;
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(dir->blocks[b]);
        int per_block = TMPFS_BLOCK_SIZE / sizeof(tmpfs_dirent_t);
        for (int j = 0; j < per_block && count < max; j++) {
            if (entries[j].inode == 0) continue;

            memset(&out[count], 0, sizeof(out[count]));
            strncpy(out[count].name, entries[j].name, MAX_NAME_LEN - 1);
            uint32_t ci = entries[j].inode;
            if (ci < TMPFS_NUM_INODES) {
                out[count].type = tmpfs_inodes[ci].type;
                out[count].size = tmpfs_inodes[ci].size;
                out[count].modified_at = tmpfs_inodes[ci].modified_at;
            }
            out[count].inode = ci;
            count++;
        }
    }
    return count;
}

static int tmpfs_stat(void *priv, const char *path, inode_t *out)
{
    (void)priv;
    int ino = tmpfs_resolve(path, NULL, NULL);
    if (ino < 0) return -1;

    tmpfs_inode_t *node = &tmpfs_inodes[ino];
    memset(out, 0, sizeof(*out));
    out->type       = node->type;
    out->mode       = node->mode;
    out->owner_uid  = node->uid;
    out->owner_gid  = node->gid;
    out->size       = node->size;
    out->created_at = node->created_at;
    out->modified_at = node->modified_at;
    return 0;
}

static int tmpfs_chmod(void *priv, const char *path, uint16_t mode)
{
    (void)priv;
    int ino = tmpfs_resolve(path, NULL, NULL);
    if (ino < 0) return -1;
    tmpfs_inodes[ino].mode = mode;
    return 0;
}

static int tmpfs_chown(void *priv, const char *path, uint16_t uid, uint16_t gid)
{
    (void)priv;
    int ino = tmpfs_resolve(path, NULL, NULL);
    if (ino < 0) return -1;
    tmpfs_inodes[ino].uid = uid;
    tmpfs_inodes[ino].gid = gid;
    return 0;
}

static int tmpfs_rename(void *priv, const char *old_path, const char *new_path)
{
    (void)priv;
    uint32_t old_parent, new_parent;
    char old_name[TMPFS_MAX_NAME], new_name[TMPFS_MAX_NAME];

    int ino = tmpfs_resolve(old_path, &old_parent, old_name);
    if (ino < 0) return -1;

    /* Check new path doesn't already exist */
    tmpfs_resolve(new_path, &new_parent, new_name);

    /* Remove from old parent, add to new */
    tmpfs_dir_remove(old_parent, old_name);
    tmpfs_dir_add(new_parent, new_name, ino);
    return 0;
}

static int tmpfs_mount_cb(void *priv)
{
    (void)priv;
    DBG("[TMPFS] Mounted at /tmp (%d inodes, %d blocks = %d KB)",
        TMPFS_NUM_INODES, TMPFS_NUM_BLOCKS,
        TMPFS_NUM_BLOCKS * TMPFS_BLOCK_SIZE / 1024);
    return 0;
}

/* ── Public interface ──────────────────────────────────────────────── */

static vfs_ops_t tmpfs_ops = {
    .name       = "tmpfs",
    .mount      = tmpfs_mount_cb,
    .unmount    = NULL,
    .create     = tmpfs_create,
    .unlink     = tmpfs_unlink,
    .read_file  = tmpfs_read_file,
    .write_file = tmpfs_write_file,
    .read_at    = NULL,
    .write_at   = NULL,
    .readdir    = tmpfs_readdir,
    .stat       = tmpfs_stat,
    .chmod      = tmpfs_chmod,
    .chown      = tmpfs_chown,
    .rename     = tmpfs_rename,
    .truncate   = NULL,
    .symlink    = NULL,
    .readlink   = NULL,
    .sync       = NULL,
};

void tmpfs_init(void) {
    memset(tmpfs_inodes, 0, sizeof(tmpfs_inodes));
    memset(tmpfs_inode_bmp, 0, sizeof(tmpfs_inode_bmp));
    memset(tmpfs_block_bmp, 0, sizeof(tmpfs_block_bmp));

    /* Allocate data blocks (16MB) */
    if (!tmpfs_data) {
        tmpfs_data = (uint8_t *)malloc((size_t)TMPFS_NUM_BLOCKS * TMPFS_BLOCK_SIZE);
        if (!tmpfs_data) {
            DBG("[TMPFS] Failed to allocate %d KB",
                TMPFS_NUM_BLOCKS * TMPFS_BLOCK_SIZE / 1024);
            return;
        }
    }
    memset(tmpfs_data, 0, (size_t)TMPFS_NUM_BLOCKS * TMPFS_BLOCK_SIZE);

    /* Initialize root directory (inode 0) */
    tbmp_set(tmpfs_inode_bmp, 0);
    tmpfs_inodes[0].type = INODE_DIR;
    tmpfs_inodes[0].mode = 0777;  /* /tmp is world-writable */
    tmpfs_inodes[0].created_at = rtc_get_epoch();
    tmpfs_inodes[0].modified_at = tmpfs_inodes[0].created_at;

    /* Allocate root dir block with . and .. */
    uint32_t blk = tmpfs_alloc_block();
    tmpfs_inodes[0].blocks[0] = blk;
    tmpfs_inodes[0].num_blocks = 1;

    tmpfs_dirent_t *entries = (tmpfs_dirent_t *)TMPFS_BLK(blk);
    entries[0].inode = 0;
    strncpy(entries[0].name, ".", TMPFS_MAX_NAME - 1);
    entries[1].inode = 0;
    strncpy(entries[1].name, "..", TMPFS_MAX_NAME - 1);

    vfs_mount("/tmp", &tmpfs_ops, NULL);
}
