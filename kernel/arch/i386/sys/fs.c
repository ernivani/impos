#include <kernel/fs.h>
#include <kernel/vfs.h>
#include <kernel/journal.h>

/* Special filesystem init functions (procfs, devfs, tmpfs) */
extern void procfs_init(void);
extern void devfs_init(void);
extern void tmpfs_init(void);
#include <kernel/ata.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <kernel/rtc.h>
#include <kernel/crypto.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static superblock_t sb;
static inode_t inodes[NUM_INODES];
static uint8_t *data_blocks = NULL;
#define BLOCK_PTR(i) (data_blocks + (size_t)(i) * BLOCK_SIZE)

static int fs_dirty = 0;

/* Bitmaps are standalone arrays (too large for superblock in FS v4) */
static uint8_t inode_bitmap[NUM_INODES / 8];     /* 512 bytes */
static uint8_t block_bitmap[NUM_BLOCKS / 8];     /* 8192 bytes */
static uint8_t dirty_bitmap[NUM_BLOCKS / 8];     /* 8192 bytes */

static uint32_t fs_rd_ops = 0, fs_rd_bytes = 0;
static uint32_t fs_wr_ops = 0, fs_wr_bytes = 0;


static void local_strncpy(char* dst, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int local_strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static void local_strcat(char* dst, const char* src) {
    size_t len = strlen(dst);
    size_t i = 0;
    while (src[i]) {
        dst[len + i] = src[i];
        i++;
    }
    dst[len + i] = '\0';
}


static void bitmap_set(uint8_t* map, uint32_t bit) {
    map[bit / 8] |= (1 << (bit % 8));
}

static void bitmap_clear(uint8_t* map, uint32_t bit) {
    map[bit / 8] &= ~(1 << (bit % 8));
}

static int bitmap_test(const uint8_t* map, uint32_t bit) {
    return (map[bit / 8] >> (bit % 8)) & 1;
}

static void mark_dirty(uint32_t block) {
    bitmap_set(dirty_bitmap, block);
}

/* ---- permission check ---- */

static int check_permission(inode_t* node, int required) {
    uint16_t uid = user_get_current_uid();

    /* root bypasses all permission checks */
    if (uid == 0) return 1;

    uint16_t mode = node->mode;
    int perm;

    const char* cur_name = user_get_current();
    if (uid == node->owner_uid) {
        perm = (mode >> 6) & 7;  /* owner bits */
    } else if (node->owner_gid == user_get_current_gid() ||
               (cur_name && group_is_member(node->owner_gid, cur_name))) {
        perm = (mode >> 3) & 7;  /* group bits */
    } else {
        perm = mode & 7;         /* other bits */
    }

    return (perm & required) == required;
}

/* ---- allocation ---- */

static int alloc_inode(void) {
    for (uint32_t i = 0; i < NUM_INODES; i++) {
        if (!bitmap_test(inode_bitmap, i)) {
            bitmap_set(inode_bitmap, i);
            memset(&inodes[i], 0, sizeof(inode_t));
            fs_dirty = 1;
            return (int)i;
        }
    }
    return -1;
}

static int alloc_block(void) {
    /* Skip metadata blocks (0 through DISK_METADATA_BLOCKS-1) */
    for (uint32_t i = DISK_METADATA_BLOCKS; i < NUM_BLOCKS; i++) {
        if (!bitmap_test(block_bitmap, i)) {
            bitmap_set(block_bitmap, i);
            memset(BLOCK_PTR(i), 0, BLOCK_SIZE);
            mark_dirty(i);
            fs_dirty = 1;
            return (int)i;
        }
    }
    return -1;
}

static void free_inode(int idx) {
    bitmap_clear(inode_bitmap, idx);
    inodes[idx].type = INODE_FREE;
    fs_dirty = 1;
}

static void free_block(int idx) {
    if (idx < (int)DISK_METADATA_BLOCKS) return; /* never free metadata blocks */
    bitmap_clear(block_bitmap, idx);
    fs_dirty = 1;
}

/* ---- block addressing helpers (direct / single-indirect / double-indirect) ---- */

/* Get the physical block number for a logical file block index.
 * Returns 0 if the block is a hole (not allocated). */
static uint32_t get_block_at(inode_t *node, uint32_t logical_block) {
    /* Direct blocks */
    if (logical_block < DIRECT_BLOCKS) {
        if (logical_block >= node->num_blocks) return 0;
        return node->blocks[logical_block];
    }

    /* Single-indirect */
    uint32_t ind_offset = logical_block - DIRECT_BLOCKS;
    if (ind_offset < INDIRECT_PTRS) {
        if (node->indirect_block == 0) return 0;
        uint32_t *ptrs = (uint32_t *)BLOCK_PTR(node->indirect_block);
        return ptrs[ind_offset];
    }

    /* Double-indirect */
    uint32_t dbl_offset = ind_offset - INDIRECT_PTRS;
    if (node->double_indirect == 0) return 0;
    uint32_t *l1 = (uint32_t *)BLOCK_PTR(node->double_indirect);
    uint32_t l1_idx = dbl_offset / INDIRECT_PTRS;
    uint32_t l2_idx = dbl_offset % INDIRECT_PTRS;
    if (l1_idx >= INDIRECT_PTRS) return 0;
    if (l1[l1_idx] == 0) return 0;
    uint32_t *l2 = (uint32_t *)BLOCK_PTR(l1[l1_idx]);
    return l2[l2_idx];
}

/* Allocate (if needed) the physical block for a logical file block index.
 * Returns physical block number, or -1 on failure.
 * Creates indirect chain blocks as needed. */
static int alloc_block_at(inode_t *node, uint32_t logical_block) {
    /* Direct blocks */
    if (logical_block < DIRECT_BLOCKS) {
        if (logical_block < node->num_blocks && node->blocks[logical_block] != 0) {
            return (int)node->blocks[logical_block];
        }
        int blk = alloc_block();
        if (blk < 0) return -1;
        node->blocks[logical_block] = blk;
        if (logical_block >= node->num_blocks)
            node->num_blocks = logical_block + 1;
        return blk;
    }

    /* Single-indirect */
    uint32_t ind_offset = logical_block - DIRECT_BLOCKS;
    if (ind_offset < INDIRECT_PTRS) {
        /* Ensure all direct blocks are accounted for */
        if (node->num_blocks < DIRECT_BLOCKS)
            node->num_blocks = DIRECT_BLOCKS;
        /* Ensure indirect block exists */
        if (node->indirect_block == 0) {
            int ind_blk = alloc_block();
            if (ind_blk < 0) return -1;
            node->indirect_block = ind_blk;
        }
        uint32_t *ptrs = (uint32_t *)BLOCK_PTR(node->indirect_block);
        if (ptrs[ind_offset] != 0) return (int)ptrs[ind_offset];
        int blk = alloc_block();
        if (blk < 0) return -1;
        ptrs[ind_offset] = blk;
        mark_dirty(node->indirect_block);
        return blk;
    }

    /* Double-indirect */
    uint32_t dbl_offset = ind_offset - INDIRECT_PTRS;
    uint32_t l1_idx = dbl_offset / INDIRECT_PTRS;
    uint32_t l2_idx = dbl_offset % INDIRECT_PTRS;
    if (l1_idx >= INDIRECT_PTRS) return -1; /* exceeds maximum */

    /* Ensure double-indirect L1 block exists */
    if (node->double_indirect == 0) {
        int dbl_blk = alloc_block();
        if (dbl_blk < 0) return -1;
        node->double_indirect = dbl_blk;
    }
    uint32_t *l1 = (uint32_t *)BLOCK_PTR(node->double_indirect);

    /* Ensure L2 block exists */
    if (l1[l1_idx] == 0) {
        int l2_blk = alloc_block();
        if (l2_blk < 0) return -1;
        l1[l1_idx] = l2_blk;
        mark_dirty(node->double_indirect);
    }
    uint32_t *l2 = (uint32_t *)BLOCK_PTR(l1[l1_idx]);

    if (l2[l2_idx] != 0) return (int)l2[l2_idx];
    int blk = alloc_block();
    if (blk < 0) return -1;
    l2[l2_idx] = blk;
    mark_dirty(l1[l1_idx]);
    return blk;
}

/* ---- device node I/O ---- */

static int dev_read(inode_t* node, uint8_t* buffer, size_t* size) {
    uint8_t major = (uint8_t)node->blocks[0];
    size_t requested = *size;
    /* Cap infinite sources to 256 bytes per read */
    if (requested > 256) requested = 256;

    switch (major) {
    case DEV_MAJOR_NULL:
        *size = 0;
        return 0;
    case DEV_MAJOR_ZERO:
        memset(buffer, 0, requested);
        *size = requested;
        return 0;
    case DEV_MAJOR_TTY: {
        extern char getchar(void);
        buffer[0] = (uint8_t)getchar();
        *size = 1;
        return 0;
    }
    case DEV_MAJOR_URANDOM:
        prng_random(buffer, requested);
        *size = requested;
        return 0;
    case DEV_MAJOR_DRM:
        /* DRM is ioctl-driven, not read/write */
        *size = 0;
        return 0;
    default:
        return -1;
    }
}

static int dev_write(inode_t* node, const uint8_t* data, size_t size) {
    uint8_t major = (uint8_t)node->blocks[0];

    switch (major) {
    case DEV_MAJOR_NULL:
    case DEV_MAJOR_ZERO:
        return 0;  /* discard */
    case DEV_MAJOR_TTY:
        for (size_t i = 0; i < size; i++)
            putchar((char)data[i]);
        return 0;
    case DEV_MAJOR_URANDOM:
        return 0;  /* discard */
    case DEV_MAJOR_DRM:
        /* DRM is ioctl-driven, not read/write */
        return 0;
    default:
        return -1;
    }
}

/* ---- directory operations ---- */

static int dir_lookup(uint32_t dir_inode, const char* name) {
    inode_t* dir = &inodes[dir_inode];
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(dir->blocks[b]);
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] != '\0' &&
                local_strncmp(entries[e].name, name, MAX_NAME_LEN) == 0) {
                return (int)entries[e].inode;
            }
        }
    }
    return -1;
}

static int dir_add_entry(uint32_t dir_inode, const char* name, uint32_t child_inode) {
    inode_t* dir = &inodes[dir_inode];

    /* try to find a free slot in existing blocks */
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(dir->blocks[b]);
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] == '\0') {
                entries[e].inode = child_inode;
                local_strncpy(entries[e].name, name, MAX_NAME_LEN);
                dir->size += sizeof(dir_entry_t);
                mark_dirty(dir->blocks[b]);
                fs_dirty = 1;
                return 0;
            }
        }
    }

    /* allocate a new block for the directory */
    if (dir->num_blocks >= DIRECT_BLOCKS) return -1;
    int blk = alloc_block();
    if (blk < 0) return -1;
    dir->blocks[dir->num_blocks++] = blk;

    dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(blk);
    entries[0].inode = child_inode;
    local_strncpy(entries[0].name, name, MAX_NAME_LEN);
    dir->size += sizeof(dir_entry_t);
    fs_dirty = 1;
    return 0;
}

static int dir_remove_entry(uint32_t dir_inode, const char* name) {
    inode_t* dir = &inodes[dir_inode];
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(dir->blocks[b]);
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] != '\0' &&
                local_strncmp(entries[e].name, name, MAX_NAME_LEN) == 0) {
                memset(&entries[e], 0, sizeof(dir_entry_t));
                dir->size -= sizeof(dir_entry_t);
                mark_dirty(dir->blocks[b]);
                fs_dirty = 1;
                return 0;
            }
        }
    }
    return -1;
}

#define SYMLINK_MAX_DEPTH 8

/* ---- path resolution ---- */

static int resolve_path_depth(const char* path, uint32_t* out_parent, char* out_name, int depth);

static int resolve_path(const char* path, uint32_t* out_parent, char* out_name) {
    return resolve_path_depth(path, out_parent, out_name, 0);
}

static int resolve_path_depth(const char* path, uint32_t* out_parent, char* out_name, int depth) {
    if (depth > SYMLINK_MAX_DEPTH) return -1;
    uint32_t cur;
    char component[MAX_NAME_LEN];

    if (path[0] == '/') {
        cur = ROOT_INODE;
        path++;
    } else {
        cur = sb.cwd_inode;
    }

    /* skip trailing slash on empty remaining */
    if (*path == '\0') {
        if (out_parent) *out_parent = cur;
        if (out_name) out_name[0] = '\0';
        return (int)cur;
    }

    while (*path) {
        /* extract next component */
        size_t len = 0;
        while (path[len] && path[len] != '/') len++;
        if (len == 0) { path++; continue; }
        if (len >= MAX_NAME_LEN) return -1;

        memset(component, 0, MAX_NAME_LEN);
        for (size_t i = 0; i < len; i++) component[i] = path[i];
        component[len] = '\0';
        path += len;
        if (*path == '/') path++;

        /* if more components follow, descend */
        if (*path != '\0') {
            int child = dir_lookup(cur, component);
            if (child < 0) return -1;
            /* Follow symlinks in intermediate components */
            if (inodes[child].type == INODE_SYMLINK) {
                char target[256];
                if (inodes[child].num_blocks == 0) return -1;
                size_t tlen = inodes[child].size < 255 ? inodes[child].size : 255;
                memcpy(target, BLOCK_PTR(inodes[child].blocks[0]), tlen);
                target[tlen] = '\0';
                child = resolve_path_depth(target, NULL, NULL, depth + 1);
                if (child < 0) return -1;
            }
            if (inodes[child].type != INODE_DIR) return -1;
            cur = (uint32_t)child;
        } else {
            /* last component */
            if (out_parent) *out_parent = cur;
            if (out_name) local_strncpy(out_name, component, MAX_NAME_LEN);
            int found = dir_lookup(cur, component);
            return found;  /* may be -1 if not found */
        }
    }

    /* ended on a directory (trailing slashes) */
    if (out_parent) *out_parent = cur;
    if (out_name) out_name[0] = '\0';
    return (int)cur;
}

/* ---- helper to init a directory inode with . and .. ---- */

static int init_dir_inode(int inode_idx, uint32_t parent_inode) {
    inode_t* inode = &inodes[inode_idx];
    inode->type = INODE_DIR;
    inode->size = 0;
    inode->num_blocks = 0;
    inode->indirect_block = 0;
    inode->double_indirect = 0;

    int blk = alloc_block();
    if (blk < 0) return -1;
    inode->blocks[0] = blk;
    inode->num_blocks = 1;

    dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(blk);
    entries[0].inode = inode_idx;
    local_strncpy(entries[0].name, ".", MAX_NAME_LEN);
    entries[1].inode = parent_inode;
    local_strncpy(entries[1].name, "..", MAX_NAME_LEN);
    inode->size = 2 * sizeof(dir_entry_t);
    return 0;
}

/* ---- helper functions for shell autocompletion ---- */

uint32_t fs_get_cwd_inode(void) {
    return sb.cwd_inode;
}

int fs_read_inode(uint32_t inode_num, inode_t* out_inode) {
    if (inode_num >= NUM_INODES) {
        return -1;
    }
    memcpy(out_inode, &inodes[inode_num], sizeof(inode_t));
    return 0;
}

int fs_read_block(uint32_t block_num, uint8_t* out_data) {
    if (block_num >= NUM_BLOCKS) {
        return -1;
    }
    memcpy(out_data, BLOCK_PTR(block_num), BLOCK_SIZE);
    return 0;
}

/* ---- block-level partial read (uses get_block_at for all levels) ---- */

int fs_read_at(uint32_t inode_num, uint8_t *buffer, uint32_t offset, uint32_t count) {
    if (inode_num >= NUM_INODES) return -1;
    inode_t *node = &inodes[inode_num];

    if (node->type != INODE_FILE) return -1;

    /* Clamp to file size */
    if (offset >= node->size) return 0;
    if (offset + count > node->size)
        count = node->size - offset;
    if (count == 0) return 0;

    uint32_t bytes_read = 0;

    while (bytes_read < count) {
        uint32_t pos = offset + bytes_read;
        uint32_t block_index = pos / BLOCK_SIZE;
        uint32_t block_offset = pos % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - block_offset;
        if (chunk > count - bytes_read)
            chunk = count - bytes_read;

        uint32_t phys_block = get_block_at(node, block_index);
        if (phys_block == 0) {
            /* Hole — fill with zeros */
            memset(buffer + bytes_read, 0, chunk);
        } else {
            memcpy(buffer + bytes_read, BLOCK_PTR(phys_block) + block_offset, chunk);
        }
        bytes_read += chunk;
    }

    node->accessed_hi = (uint16_t)(rtc_get_epoch() >> 16);
    fs_rd_ops++;
    fs_rd_bytes += bytes_read;
    return (int)bytes_read;
}

/* ---- block-level partial write ---- */

int fs_write_at(uint32_t inode_num, const uint8_t *data, uint32_t offset, uint32_t count) {
    if (inode_num >= NUM_INODES) return -1;
    inode_t *node = &inodes[inode_num];

    if (node->type != INODE_FILE) return -1;
    if (count == 0) return 0;

    uint32_t bytes_written = 0;

    while (bytes_written < count) {
        uint32_t pos = offset + bytes_written;
        uint32_t block_index = pos / BLOCK_SIZE;
        uint32_t block_offset = pos % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - block_offset;
        if (chunk > count - bytes_written)
            chunk = count - bytes_written;

        int phys_block = alloc_block_at(node, block_index);
        if (phys_block < 0) break; /* out of blocks */

        memcpy(BLOCK_PTR(phys_block) + block_offset, data + bytes_written, chunk);
        mark_dirty(phys_block);
        bytes_written += chunk;
    }

    /* Update file size if we wrote past the end */
    if (offset + bytes_written > node->size)
        node->size = offset + bytes_written;

    node->modified_at = rtc_get_epoch();
    fs_dirty = 1;

    fs_wr_ops++;
    fs_wr_bytes += bytes_written;
    return (int)bytes_written;
}

/* ---- public wrappers for internal functions ---- */

int fs_resolve_path(const char *path, uint32_t *out_parent, char *out_name) {
    return resolve_path(path, out_parent, out_name);
}

int fs_dir_lookup(uint32_t dir_inode_num, const char *name) {
    return dir_lookup(dir_inode_num, name);
}

/* ---- helper: free all blocks of an inode (direct + indirect + double-indirect) ---- */

static void free_inode_blocks(inode_t *node) {
    /* Free direct blocks */
    for (uint8_t b = 0; b < node->num_blocks; b++) {
        if (node->blocks[b] != 0)
            free_block(node->blocks[b]);
    }

    /* Free single-indirect chain */
    if (node->indirect_block != 0) {
        uint32_t *ptrs = (uint32_t *)BLOCK_PTR(node->indirect_block);
        for (uint32_t i = 0; i < INDIRECT_PTRS; i++) {
            if (ptrs[i] != 0)
                free_block(ptrs[i]);
        }
        free_block(node->indirect_block);
        node->indirect_block = 0;
    }

    /* Free double-indirect chain: walk L1 -> each L2 -> free data -> free L2 -> free L1 */
    if (node->double_indirect != 0) {
        uint32_t *l1 = (uint32_t *)BLOCK_PTR(node->double_indirect);
        for (uint32_t i = 0; i < INDIRECT_PTRS; i++) {
            if (l1[i] == 0) continue;
            uint32_t *l2 = (uint32_t *)BLOCK_PTR(l1[i]);
            for (uint32_t j = 0; j < INDIRECT_PTRS; j++) {
                if (l2[j] != 0)
                    free_block(l2[j]);
            }
            free_block(l1[i]);
        }
        free_block(node->double_indirect);
        node->double_indirect = 0;
    }
}

/* ---- disk persistence ---- */

int fs_sync(void) {
    if (!ata_is_available()) {
        return -1;
    }

    if (!fs_dirty) {
        return 0;
    }

    /* Write superblock (block 0) — clear dirty flag on clean sync */
    sb.magic = FS_MAGIC;
    sb.version = FS_VERSION;
    sb.block_size = BLOCK_SIZE;
    sb.flags &= ~FS_FLAG_DIRTY;
    if (ata_write_sectors(DISK_BLK_SUPERBLOCK * SECTORS_PER_BLOCK,
                          SECTORS_PER_BLOCK, (uint8_t*)&sb) != 0) {
        return -1;
    }

    /* Write inode bitmap (block 1) */
    /* Pad to full block: inode_bitmap is 512 bytes, pad rest with zeros */
    {
        uint8_t bmp_block[BLOCK_SIZE];
        memset(bmp_block, 0, BLOCK_SIZE);
        memcpy(bmp_block, inode_bitmap, sizeof(inode_bitmap));
        if (ata_write_sectors(DISK_BLK_INODE_BITMAP * SECTORS_PER_BLOCK,
                              SECTORS_PER_BLOCK, bmp_block) != 0)
            return -1;
    }

    /* Write block bitmap (blocks 2-3, exactly 8192 bytes = 2 blocks) */
    if (ata_write_sectors(DISK_BLK_BLOCK_BITMAP * SECTORS_PER_BLOCK,
                          DISK_BLK_BLOCK_BITMAP_COUNT * SECTORS_PER_BLOCK,
                          block_bitmap) != 0)
        return -1;

    /* Write inode table (blocks 4-67, one block at a time) */
    for (int i = 0; i < DISK_BLK_INODE_TABLE_COUNT; i++) {
        uint32_t lba = (DISK_BLK_INODE_TABLE + i) * SECTORS_PER_BLOCK;
        uint8_t *ptr = (uint8_t*)inodes + (size_t)i * BLOCK_SIZE;
        if (ata_write_sectors(lba, SECTORS_PER_BLOCK, ptr) != 0)
            return -1;
    }

    /* Write dirty + allocated data blocks */
    for (uint32_t i = DISK_METADATA_BLOCKS; i < NUM_BLOCKS; i++) {
        if (bitmap_test(dirty_bitmap, i) && bitmap_test(block_bitmap, i)) {
            uint32_t lba = i * SECTORS_PER_BLOCK;
            if (ata_write_sectors(lba, SECTORS_PER_BLOCK, BLOCK_PTR(i)) != 0) {
                return -1;
            }
        }
    }

    /* Flush disk cache */
    if (ata_flush() != 0) {
        return -1;
    }

    memset(dirty_bitmap, 0, sizeof(dirty_bitmap));
    fs_dirty = 0;
    return 0;
}

int fs_load(void) {
    if (!ata_is_available()) {
        return -1;
    }

    /* Read superblock (block 0) */
    if (ata_read_sectors(DISK_BLK_SUPERBLOCK * SECTORS_PER_BLOCK,
                         SECTORS_PER_BLOCK, (uint8_t*)&sb) != 0) {
        return -1;
    }

    /* Verify magic number */
    if (sb.magic != FS_MAGIC) {
        return -1;
    }

    /* Check FS version */
    if (sb.version != FS_VERSION) {
        DBG("[FS] Incompatible FS version %u (expected %u) — reformatting",
            sb.version, FS_VERSION);
        return -1;
    }

    /* Validate superblock fields */
    if (sb.num_inodes != NUM_INODES || sb.num_blocks != NUM_BLOCKS ||
        sb.block_size != BLOCK_SIZE) {
        return -1;
    }

    /* Check dirty flag — if set, FS wasn't cleanly unmounted */
    if (sb.flags & FS_FLAG_DIRTY) {
        DBG("[FS] Dirty flag set — replaying journal...");
        journal_init();
        journal_replay();
    }

    /* Read inode bitmap (block 1) */
    {
        uint8_t bmp_block[BLOCK_SIZE];
        if (ata_read_sectors(DISK_BLK_INODE_BITMAP * SECTORS_PER_BLOCK,
                             SECTORS_PER_BLOCK, bmp_block) != 0)
            return -1;
        memcpy(inode_bitmap, bmp_block, sizeof(inode_bitmap));
    }

    /* Read block bitmap (blocks 2-3) */
    if (ata_read_sectors(DISK_BLK_BLOCK_BITMAP * SECTORS_PER_BLOCK,
                         DISK_BLK_BLOCK_BITMAP_COUNT * SECTORS_PER_BLOCK,
                         block_bitmap) != 0)
        return -1;

    /* Read inode table (blocks 4-67) */
    for (int i = 0; i < DISK_BLK_INODE_TABLE_COUNT; i++) {
        uint32_t lba = (DISK_BLK_INODE_TABLE + i) * SECTORS_PER_BLOCK;
        uint8_t *ptr = (uint8_t*)inodes + (size_t)i * BLOCK_SIZE;
        if (ata_read_sectors(lba, SECTORS_PER_BLOCK, ptr) != 0)
            return -1;
    }

    /* Read allocated data blocks */
    for (uint32_t i = DISK_METADATA_BLOCKS; i < NUM_BLOCKS; i++) {
        if (bitmap_test(block_bitmap, i)) {
            uint32_t lba = i * SECTORS_PER_BLOCK;
            if (ata_read_sectors(lba, SECTORS_PER_BLOCK, BLOCK_PTR(i)) != 0) {
                return -1;
            }
        }
    }

    /* Validate active inodes */
    for (uint32_t i = 0; i < NUM_INODES; i++) {
        if (!bitmap_test(inode_bitmap, i))
            continue;
        inode_t* node = &inodes[i];
        if (node->type > INODE_CHARDEV) {
            return -1;
        }
        if (node->num_blocks > DIRECT_BLOCKS) {
            return -1;
        }
        for (uint8_t b = 0; b < node->num_blocks; b++) {
            if (node->blocks[b] >= NUM_BLOCKS) {
                return -1;
            }
        }
        if (node->indirect_block != 0 && node->indirect_block >= NUM_BLOCKS) {
            return -1;
        }
        if (node->double_indirect != 0 && node->double_indirect >= NUM_BLOCKS) {
            return -1;
        }
    }

    /* Validate cwd_inode */
    if (sb.cwd_inode >= NUM_INODES ||
        !bitmap_test(inode_bitmap, sb.cwd_inode) ||
        inodes[sb.cwd_inode].type != INODE_DIR) {
        sb.cwd_inode = ROOT_INODE;
    }

    memset(dirty_bitmap, 0, sizeof(dirty_bitmap));
    fs_dirty = 0;
    return 0;
}

/* ---- public API ---- */

void fs_initialize(void) {
    /* Initialize VFS mount table */
    vfs_init();

    /* Allocate data blocks memory (256MB) */
    if (!data_blocks) {
        data_blocks = (uint8_t *)malloc((size_t)NUM_BLOCKS * BLOCK_SIZE);
        if (!data_blocks) {
            DBG("[FS] FATAL: cannot allocate %u MB for data blocks",
                (NUM_BLOCKS * BLOCK_SIZE) / (1024 * 1024));
            return;
        }
    }
    memset(data_blocks, 0, (size_t)NUM_BLOCKS * BLOCK_SIZE);
    memset(inode_bitmap, 0, sizeof(inode_bitmap));
    memset(block_bitmap, 0, sizeof(block_bitmap));
    memset(dirty_bitmap, 0, sizeof(dirty_bitmap));

    /* Try to load from disk first */
    if (ata_is_available() && fs_load() == 0) {
        DBG("[FS] Loaded v%u filesystem: %u inodes, %u blocks (%u KB each)",
            sb.version, sb.num_inodes, sb.num_blocks, BLOCK_SIZE / 1024);
        /* Set dirty flag on mount (cleared on clean sync) */
        sb.flags |= FS_FLAG_DIRTY;
        ata_write_sectors(DISK_BLK_SUPERBLOCK * SECTORS_PER_BLOCK,
                          SECTORS_PER_BLOCK, (uint8_t*)&sb);
        ata_flush();
        /* Initialize journal */
        journal_init();
    } else {
        /* Otherwise initialize new filesystem in memory */
        memset(&sb, 0, sizeof(sb));
        memset(inodes, 0, sizeof(inodes));
        memset(data_blocks, 0, (size_t)NUM_BLOCKS * BLOCK_SIZE);
        memset(inode_bitmap, 0, sizeof(inode_bitmap));
        memset(block_bitmap, 0, sizeof(block_bitmap));
        memset(dirty_bitmap, 0, sizeof(dirty_bitmap));

        sb.magic = FS_MAGIC;
        sb.version = FS_VERSION;
        sb.num_inodes = NUM_INODES;
        sb.num_blocks = NUM_BLOCKS;
        sb.block_size = BLOCK_SIZE;
        sb.data_start_block = DISK_METADATA_BLOCKS;

        /* Mark metadata + journal blocks as allocated in block bitmap */
        for (uint32_t i = 0; i < DISK_METADATA_BLOCKS; i++) {
            bitmap_set(block_bitmap, i);
        }

        /* allocate root inode */
        bitmap_set(inode_bitmap, ROOT_INODE);
        sb.cwd_inode = ROOT_INODE;
        init_dir_inode(ROOT_INODE, ROOT_INODE);
        inodes[ROOT_INODE].mode = 0755;
        inodes[ROOT_INODE].owner_uid = 0;
        inodes[ROOT_INODE].owner_gid = 0;
        inodes[ROOT_INODE].indirect_block = 0;
        inodes[ROOT_INODE].double_indirect = 0;

        /* create default directory hierarchy */
        fs_create_file("/home", 1);
        fs_create_file("/home/root", 1);
        fs_create_file("/bin", 1);
        fs_create_file("/usr", 1);
        fs_create_file("/usr/bin", 1);
        fs_create_file("/dev", 1);
        fs_create_file("/etc", 1);
        fs_create_file("/tmp", 1);

        /* create device nodes */
        fs_create_device("/dev/null", DEV_MAJOR_NULL, 0);
        fs_create_device("/dev/zero", DEV_MAJOR_ZERO, 0);
        fs_create_device("/dev/tty", DEV_MAJOR_TTY, 0);
        fs_create_device("/dev/urandom", DEV_MAJOR_URANDOM, 0);

        /* DRM device node */
        fs_create_file("/dev/dri", 1);
        fs_create_device("/dev/dri/card0", DEV_MAJOR_DRM, 0);

        fs_change_directory("/home/root");

        DBG("[FS] Formatted new v%u filesystem: %u inodes, %u blocks (%u KB each) = %u MB",
            FS_VERSION, NUM_INODES, NUM_BLOCKS, BLOCK_SIZE / 1024,
            (NUM_BLOCKS * (BLOCK_SIZE / 1024)) / 1024);

        fs_dirty = 1;

        /* Auto-sync if disk available */
        if (ata_is_available()) {
            fs_sync();
            /* Initialize journal after first format */
            journal_init();
        }
    }

    /* Mount special virtual filesystems via VFS */
    procfs_init();
    devfs_init();
    tmpfs_init();
}

int fs_create_file(const char* filename, uint8_t is_directory) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(filename, &rel);
    if (mnt && mnt->ops->create)
        return mnt->ops->create(mnt->private_data, rel, is_directory);
    if (mnt) return -1; /* mount exists but no create op */

    uint32_t parent;
    char name[MAX_NAME_LEN];

    resolve_path(filename, &parent, name);
    if (name[0] == '\0') return -1;

    /* check if already exists */
    if (dir_lookup(parent, name) >= 0) return -1;

    journal_begin();

    int idx = alloc_inode();
    if (idx < 0) { journal_commit(); return -1; }

    journal_log_inode_alloc(idx);

    if (is_directory) {
        if (init_dir_inode(idx, parent) < 0) {
            free_inode(idx);
            journal_commit();
            return -1;
        }
        inodes[idx].mode = 0755;
    } else {
        inodes[idx].type = INODE_FILE;
        inodes[idx].size = 0;
        inodes[idx].num_blocks = 0;
        inodes[idx].indirect_block = 0;
        inodes[idx].double_indirect = 0;
        inodes[idx].mode = 0644;
    }
    inodes[idx].owner_uid = user_get_current_uid();
    inodes[idx].owner_gid = user_get_current_gid();

    /* Set timestamps */
    uint32_t now = rtc_get_epoch();
    inodes[idx].created_at = now;
    inodes[idx].modified_at = now;
    inodes[idx].nlink = is_directory ? 2 : 1;
    inodes[idx].accessed_hi = (uint16_t)(now >> 16);

    journal_log_inode_update(idx);

    if (dir_add_entry(parent, name, idx) < 0) {
        /* clean up allocated blocks */
        for (uint8_t b = 0; b < inodes[idx].num_blocks; b++)
            free_block(inodes[idx].blocks[b]);
        free_inode(idx);
        journal_commit();
        return -1;
    }

    journal_log_dir_add(parent, idx, name);
    journal_log_inode_update(parent);
    journal_commit();

    /* Auto-sync if disk available */
    if (ata_is_available()) {
        fs_sync();
    }

    return 0;
}

int fs_create_device(const char* path, uint8_t major, uint8_t minor) {
    uint32_t parent;
    char name[MAX_NAME_LEN];

    resolve_path(path, &parent, name);
    if (name[0] == '\0') return -1;

    /* check if already exists */
    if (dir_lookup(parent, name) >= 0) return -1;

    int idx = alloc_inode();
    if (idx < 0) return -1;

    inodes[idx].type = INODE_CHARDEV;
    inodes[idx].mode = 0666;
    inodes[idx].size = 0;
    inodes[idx].num_blocks = 0;
    inodes[idx].indirect_block = 0;
    inodes[idx].double_indirect = 0;
    inodes[idx].blocks[0] = major;  /* major device number */
    inodes[idx].blocks[1] = minor;  /* minor device number */
    inodes[idx].owner_uid = 0;
    inodes[idx].owner_gid = 0;

    uint32_t now = rtc_get_epoch();
    inodes[idx].created_at = now;
    inodes[idx].modified_at = now;
    inodes[idx].nlink = 1;
    inodes[idx].accessed_hi = (uint16_t)(now >> 16);

    if (dir_add_entry(parent, name, idx) < 0) {
        free_inode(idx);
        return -1;
    }

    fs_dirty = 1;
    if (ata_is_available()) {
        fs_sync();
    }
    return 0;
}

int fs_write_file(const char* filename, const uint8_t* data, size_t size) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(filename, &rel);
    if (mnt && mnt->ops->write_file)
        return mnt->ops->write_file(mnt->private_data, rel, data, size);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    inode_t* inode = &inodes[inode_idx];

    /* Handle device nodes */
    if (inode->type == INODE_CHARDEV) {
        return dev_write(inode, data, size);
    }

    if (inode->type != INODE_FILE) return -1;
    if (!check_permission(inode, PERM_W)) return -1;

    /* Free all existing blocks */
    free_inode_blocks(inode);
    inode->num_blocks = 0;
    inode->size = 0;
    inode->indirect_block = 0;
    inode->double_indirect = 0;

    /* Write data using alloc_block_at */
    size_t remaining = size;
    size_t offset = 0;
    uint32_t block_index = 0;

    while (remaining > 0) {
        int phys_blk = alloc_block_at(inode, block_index);
        if (phys_blk < 0) return -1;

        size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        memcpy(BLOCK_PTR(phys_blk), data + offset, chunk);
        mark_dirty(phys_blk);
        offset += chunk;
        remaining -= chunk;
        block_index++;
    }

    inode->size = size;
    inode->modified_at = rtc_get_epoch();
    fs_dirty = 1;

    /* Auto-sync if disk available */
    if (ata_is_available()) {
        fs_sync();
    }

    fs_wr_ops++;
    fs_wr_bytes += (uint32_t)size;
    return 0;
}

int fs_read_file(const char* filename, uint8_t* buffer, size_t* size) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(filename, &rel);
    if (mnt && mnt->ops->read_file)
        return mnt->ops->read_file(mnt->private_data, rel, buffer, size);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    inode_t* inode = &inodes[inode_idx];

    /* Follow symlinks */
    if (inode->type == INODE_SYMLINK) {
        char target[256];
        if (inode->num_blocks == 0) return -1;
        size_t tlen = inode->size < 255 ? inode->size : 255;
        memcpy(target, BLOCK_PTR(inode->blocks[0]), tlen);
        target[tlen] = '\0';
        return fs_read_file(target, buffer, size);
    }

    /* Handle device nodes */
    if (inode->type == INODE_CHARDEV) {
        return dev_read(inode, buffer, size);
    }

    if (inode->type != INODE_FILE) return -1;
    if (!check_permission(inode, PERM_R)) return -1;

    /* Read using get_block_at for all block levels */
    uint32_t remaining = inode->size;
    uint32_t offset = 0;
    uint32_t block_index = 0;

    while (remaining > 0) {
        uint32_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        uint32_t phys_block = get_block_at(inode, block_index);

        if (phys_block == 0) {
            /* Hole — fill with zeros */
            memset(buffer + offset, 0, chunk);
        } else {
            memcpy(buffer + offset, BLOCK_PTR(phys_block), chunk);
        }
        offset += chunk;
        remaining -= chunk;
        block_index++;
    }

    *size = inode->size;
    inode->accessed_hi = (uint16_t)(rtc_get_epoch() >> 16);
    fs_rd_ops++;
    fs_rd_bytes += (uint32_t)(*size);
    return 0;
}

int fs_delete_file(const char* filename) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(filename, &rel);
    if (mnt && mnt->ops->unlink)
        return mnt->ops->unlink(mnt->private_data, rel);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    if ((uint32_t)inode_idx == ROOT_INODE) return -1;

    /* Check write permission on parent directory */
    if (!check_permission(&inodes[parent], PERM_W)) return -1;

    inode_t* inode = &inodes[inode_idx];

    /* if directory, check it's empty (only . and ..) */
    if (inode->type == INODE_DIR) {
        for (uint8_t b = 0; b < inode->num_blocks; b++) {
            dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(inode->blocks[b]);
            int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
            for (int e = 0; e < entries_per_block; e++) {
                if (entries[e].name[0] != '\0' &&
                    local_strncmp(entries[e].name, ".", MAX_NAME_LEN) != 0 &&
                    local_strncmp(entries[e].name, "..", MAX_NAME_LEN) != 0) {
                    return -1;  /* not empty */
                }
            }
        }
    }

    journal_begin();

    /* Remove directory entry regardless of nlink */
    dir_remove_entry(parent, name);
    journal_log_dir_remove(parent, inode_idx, name);
    journal_log_inode_update(parent);

    /* Decrement link count; only free inode+blocks when nlink reaches 0 */
    if (inode->nlink > 1) {
        inode->nlink--;
        inode->modified_at = rtc_get_epoch();
        journal_log_inode_update(inode_idx);
        journal_commit();
    } else {
        /* Last link — free everything */
        if (inode->type != INODE_CHARDEV) {
            free_inode_blocks(inode);
        }
        free_inode(inode_idx);
        journal_log_inode_free(inode_idx);
        journal_commit();
    }

    /* Auto-sync if disk available */
    if (ata_is_available()) {
        fs_sync();
    }

    return 0;
}

/* ---- truncate ---- */

int fs_truncate(const char *path, uint32_t new_size) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(path, &rel);
    if (mnt && mnt->ops->truncate)
        return mnt->ops->truncate(mnt->private_data, rel, new_size);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(path, &parent, name);
    if (inode_idx < 0) return -1;

    inode_t *node = &inodes[inode_idx];
    if (node->type != INODE_FILE) return -1;
    if (!check_permission(node, PERM_W)) return -1;

    if (new_size >= node->size) {
        /* Extending or no-op — zero the gap in the tail block, then update size */
        uint32_t old_size = node->size;
        uint32_t tail_off = old_size % BLOCK_SIZE;
        if (tail_off != 0 && old_size > 0) {
            /* The last block has data up to tail_off; zero from there to block end
             * (or to new_size if still within same block) */
            uint32_t last_logical = (old_size - 1) / BLOCK_SIZE;
            uint32_t phys = get_block_at(node, last_logical);
            if (phys != 0) {
                uint32_t zero_end = BLOCK_SIZE;
                uint32_t new_in_block = new_size - (last_logical * BLOCK_SIZE);
                if (new_in_block < BLOCK_SIZE) zero_end = new_in_block;
                if (zero_end > tail_off) {
                    memset(BLOCK_PTR(phys) + tail_off, 0, zero_end - tail_off);
                    mark_dirty(phys);
                }
            }
        }
        node->size = new_size;
    } else {
        /* Shrinking — free blocks beyond new_size */
        uint32_t new_last_block = (new_size > 0) ? (new_size - 1) / BLOCK_SIZE : 0;
        uint32_t old_last_block = (node->size > 0) ? (node->size - 1) / BLOCK_SIZE : 0;

        /* Free blocks from new_last_block+1 onwards */
        for (uint32_t b = (new_size == 0 ? 0 : new_last_block + 1); b <= old_last_block; b++) {
            uint32_t phys = get_block_at(node, b);
            if (phys != 0) {
                free_block(phys);
                /* Zero out the pointer — we'd need set_block_at for full cleanup,
                 * but free_block prevents reallocation conflicts. For direct blocks
                 * we can clear directly. */
                if (b < DIRECT_BLOCKS) {
                    node->blocks[b] = 0;
                    if (b < node->num_blocks && b == node->num_blocks - 1) {
                        /* Trim num_blocks */
                        while (node->num_blocks > 0 && node->blocks[node->num_blocks - 1] == 0)
                            node->num_blocks--;
                    }
                }
                /* Indirect/double-indirect pointer cleanup happens
                 * when the entire chain is freed below */
            }
        }

        /* If no indirect blocks needed anymore, free them */
        if (new_size <= MAX_DIRECT_SIZE) {
            if (node->indirect_block != 0) {
                uint32_t *ptrs = (uint32_t *)BLOCK_PTR(node->indirect_block);
                for (uint32_t i = 0; i < INDIRECT_PTRS; i++) {
                    if (ptrs[i] != 0) free_block(ptrs[i]);
                }
                free_block(node->indirect_block);
                node->indirect_block = 0;
            }
            if (node->double_indirect != 0) {
                uint32_t *l1 = (uint32_t *)BLOCK_PTR(node->double_indirect);
                for (uint32_t i = 0; i < INDIRECT_PTRS; i++) {
                    if (l1[i] == 0) continue;
                    uint32_t *l2 = (uint32_t *)BLOCK_PTR(l1[i]);
                    for (uint32_t j = 0; j < INDIRECT_PTRS; j++) {
                        if (l2[j] != 0) free_block(l2[j]);
                    }
                    free_block(l1[i]);
                }
                free_block(node->double_indirect);
                node->double_indirect = 0;
            }
        }

        /* Recalculate num_blocks for direct region */
        while (node->num_blocks > 0 && node->blocks[node->num_blocks - 1] == 0)
            node->num_blocks--;

        node->size = new_size;
    }

    node->modified_at = rtc_get_epoch();
    fs_dirty = 1;

    if (ata_is_available()) fs_sync();
    return 0;
}

int fs_truncate_inode(uint32_t inode_num, uint32_t new_size) {
    if (inode_num >= NUM_INODES) return -1;
    inode_t *node = &inodes[inode_num];
    if (node->type != INODE_FILE) return -1;

    if (new_size >= node->size) {
        node->size = new_size;
    } else {
        uint32_t new_last_block = (new_size > 0) ? (new_size - 1) / BLOCK_SIZE : 0;
        uint32_t old_last_block = (node->size > 0) ? (node->size - 1) / BLOCK_SIZE : 0;

        for (uint32_t b = (new_size == 0 ? 0 : new_last_block + 1); b <= old_last_block; b++) {
            uint32_t phys = get_block_at(node, b);
            if (phys != 0) {
                free_block(phys);
                if (b < DIRECT_BLOCKS) node->blocks[b] = 0;
            }
        }

        if (new_size <= MAX_DIRECT_SIZE) {
            if (node->indirect_block != 0) {
                uint32_t *ptrs = (uint32_t *)BLOCK_PTR(node->indirect_block);
                for (uint32_t i = 0; i < INDIRECT_PTRS; i++) {
                    if (ptrs[i] != 0) free_block(ptrs[i]);
                }
                free_block(node->indirect_block);
                node->indirect_block = 0;
            }
            if (node->double_indirect != 0) {
                uint32_t *l1 = (uint32_t *)BLOCK_PTR(node->double_indirect);
                for (uint32_t i = 0; i < INDIRECT_PTRS; i++) {
                    if (l1[i] == 0) continue;
                    uint32_t *l2 = (uint32_t *)BLOCK_PTR(l1[i]);
                    for (uint32_t j = 0; j < INDIRECT_PTRS; j++) {
                        if (l2[j] != 0) free_block(l2[j]);
                    }
                    free_block(l1[i]);
                }
                free_block(node->double_indirect);
                node->double_indirect = 0;
            }
        }

        while (node->num_blocks > 0 && node->blocks[node->num_blocks - 1] == 0)
            node->num_blocks--;

        node->size = new_size;
    }

    node->modified_at = rtc_get_epoch();
    fs_dirty = 1;
    return 0;
}

static void print_long_entry(const char* name, uint32_t ino) {
    inode_t* node = &inodes[ino];
    char perm[11];
    uint16_t m = node->mode;

    /* Type character */
    if (node->type == INODE_DIR)          perm[0] = 'd';
    else if (node->type == INODE_SYMLINK) perm[0] = 'l';
    else if (node->type == INODE_CHARDEV) perm[0] = 'c';
    else                                  perm[0] = '-';

    /* Owner */
    perm[1] = (m & 0400) ? 'r' : '-';
    perm[2] = (m & 0200) ? 'w' : '-';
    perm[3] = (m & 0100) ? 'x' : '-';
    /* Group */
    perm[4] = (m & 040)  ? 'r' : '-';
    perm[5] = (m & 020)  ? 'w' : '-';
    perm[6] = (m & 010)  ? 'x' : '-';
    /* Other */
    perm[7] = (m & 04)   ? 'r' : '-';
    perm[8] = (m & 02)   ? 'w' : '-';
    perm[9] = (m & 01)   ? 'x' : '-';
    perm[10] = '\0';

    /* Resolve owner name */
    const char* owner = "?";
    user_t* u = user_get_by_uid(node->owner_uid);
    if (u) owner = u->username;

    char timebuf[16];
    rtc_format_epoch(node->modified_at, timebuf, sizeof(timebuf));

    if (node->type == INODE_CHARDEV) {
        printf("%s  %s  %u, %u  %s  %s",
               perm, owner,
               (unsigned)node->blocks[0], (unsigned)node->blocks[1],
               timebuf, name);
    } else {
        printf("%s  %s  %5d  %s  %s", perm, owner, (int)node->size, timebuf, name);
    }

    /* Print symlink target */
    if (node->type == INODE_SYMLINK && node->num_blocks > 0) {
        char target[256];
        size_t tlen = node->size < 255 ? node->size : 255;
        memcpy(target, BLOCK_PTR(node->blocks[0]), tlen);
        target[tlen] = '\0';
        printf(" -> %s", target);
    }

    printf("\n");
}

void fs_list_directory(int flags) {
    int show_all = flags & LS_ALL;
    int long_fmt = flags & LS_LONG;

    /* VFS dispatch: check if CWD is under a virtual mount */
    const char *cwd = fs_get_cwd();
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(cwd, &rel);
    if (mnt && mnt->ops->readdir) {
        fs_dir_entry_info_t vfs_entries[64];
        int n = mnt->ops->readdir(mnt->private_data, rel, vfs_entries, 64);
        int col = 0;
        for (int i = 0; i < n; i++) {
            int is_dot = (strcmp(vfs_entries[i].name, ".") == 0 ||
                          strcmp(vfs_entries[i].name, "..") == 0);
            if (is_dot && !show_all) continue;

            if (long_fmt) {
                char perm[11] = "----------";
                if (vfs_entries[i].type == INODE_DIR)      perm[0] = 'd';
                else if (vfs_entries[i].type == INODE_CHARDEV) perm[0] = 'c';
                perm[1] = 'r'; perm[2] = '-'; perm[3] = '-';
                perm[4] = 'r'; perm[5] = '-'; perm[6] = '-';
                perm[7] = 'r'; perm[8] = '-'; perm[9] = '-';
                printf("%s  root  %5u  %s\n", perm,
                       (unsigned)vfs_entries[i].size, vfs_entries[i].name);
            } else {
                if (col > 0) printf("  ");
                printf("%s", vfs_entries[i].name);
                col++;
            }
        }
        if (!long_fmt && col > 0) printf("\n");
        return;
    }

    inode_t* dir = &inodes[sb.cwd_inode];
    int col = 0;

    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(dir->blocks[b]);
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] == '\0') continue;
            int is_dot = (local_strncmp(entries[e].name, ".", MAX_NAME_LEN) == 0 ||
                          local_strncmp(entries[e].name, "..", MAX_NAME_LEN) == 0);
            if (is_dot && !show_all) continue;

            if (long_fmt) {
                print_long_entry(entries[e].name, entries[e].inode);
            } else {
                if (col > 0) printf("  ");
                printf("%s", entries[e].name);
                col++;
            }
        }
    }
    if (!long_fmt && col > 0) printf("\n");
}

int fs_enumerate_directory(fs_dir_entry_info_t *out, int max, int show_dot) {
    /* VFS dispatch: check if CWD is under a virtual mount */
    const char *cwd = fs_get_cwd();
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(cwd, &rel);
    if (mnt && mnt->ops->readdir)
        return mnt->ops->readdir(mnt->private_data, rel, out, max);

    inode_t* dir = &inodes[sb.cwd_inode];
    int count = 0;

    for (uint8_t b = 0; b < dir->num_blocks && count < max; b++) {
        dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(dir->blocks[b]);
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block && count < max; e++) {
            if (entries[e].name[0] == '\0') continue;
            int is_dot = (local_strncmp(entries[e].name, ".", MAX_NAME_LEN) == 0 ||
                          local_strncmp(entries[e].name, "..", MAX_NAME_LEN) == 0);
            if (is_dot && !show_dot) continue;

            local_strncpy(out[count].name, entries[e].name, MAX_NAME_LEN);
            uint32_t ino = entries[e].inode;
            out[count].inode = ino;
            if (ino < NUM_INODES) {
                out[count].type = inodes[ino].type;
                out[count].size = inodes[ino].size;
                out[count].modified_at = inodes[ino].modified_at;
            } else {
                out[count].type = 0;
                out[count].size = 0;
                out[count].modified_at = 0;
            }
            count++;
        }
    }
    return count;
}

int fs_change_directory(const char* dirname) {
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(dirname, &parent, name);

    /* handle bare "." or ".." or "/" */
    if (name[0] == '\0') {
        inode_idx = (int)parent;
    } else if (inode_idx < 0) {
        return -1;
    }

    if (inodes[inode_idx].type != INODE_DIR) return -1;
    if (!check_permission(&inodes[inode_idx], PERM_X)) return -1;
    sb.cwd_inode = (uint32_t)inode_idx;
    return 0;
}

int fs_change_directory_by_inode(uint32_t inode_num) {
    if (inode_num >= NUM_INODES) return -1;
    if (inodes[inode_num].type != INODE_DIR) return -1;
    sb.cwd_inode = inode_num;
    return 0;
}

const char* fs_get_cwd(void) {
    static char path[512];

    if (sb.cwd_inode == ROOT_INODE) {
        path[0] = '/';
        path[1] = '\0';
        return path;
    }

    /* walk up via .. to build path in reverse */
    char components[16][MAX_NAME_LEN];
    int depth = 0;
    uint32_t cur = sb.cwd_inode;

    while (cur != ROOT_INODE && depth < 16) {
        /* find parent */
        int parent_inode = dir_lookup(cur, "..");
        if (parent_inode < 0) break;

        /* find cur's name in parent */
        inode_t* pdir = &inodes[parent_inode];
        int found = 0;
        for (uint8_t b = 0; b < pdir->num_blocks && !found; b++) {
            dir_entry_t* entries = (dir_entry_t*)BLOCK_PTR(pdir->blocks[b]);
            int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
            for (int e = 0; e < entries_per_block; e++) {
                if (entries[e].inode == cur && entries[e].name[0] != '\0' &&
                    local_strncmp(entries[e].name, ".", MAX_NAME_LEN) != 0 &&
                    local_strncmp(entries[e].name, "..", MAX_NAME_LEN) != 0) {
                    local_strncpy(components[depth], entries[e].name, MAX_NAME_LEN);
                    found = 1;
                    break;
                }
            }
        }
        if (!found) break;
        depth++;
        cur = (uint32_t)parent_inode;
    }

    path[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        local_strcat(path, "/");
        local_strcat(path, components[i]);
    }
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
    return path;
}

/* Return a pointer to an inode in the in-memory table (for fchmod/fchown) */
inode_t *fs_get_inode_ptr(uint32_t ino) {
    if (ino >= NUM_INODES) return NULL;
    if (inodes[ino].type == INODE_FREE) return NULL;
    return &inodes[ino];
}

int fs_chmod(const char* path, uint16_t mode) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(path, &rel);
    if (mnt && mnt->ops->chmod)
        return mnt->ops->chmod(mnt->private_data, rel, mode);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(path, &parent, name);
    if (inode_idx < 0) return -1;

    inode_t* node = &inodes[inode_idx];
    uint16_t uid = user_get_current_uid();

    /* Only root or owner can chmod */
    if (uid != 0 && uid != node->owner_uid) return -1;

    node->mode = mode & 0777;
    fs_dirty = 1;

    if (ata_is_available()) fs_sync();
    return 0;
}

int fs_chown(const char* path, uint16_t uid, uint16_t gid) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(path, &rel);
    if (mnt && mnt->ops->chown)
        return mnt->ops->chown(mnt->private_data, rel, uid, gid);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(path, &parent, name);
    if (inode_idx < 0) return -1;

    /* Only root can chown */
    if (user_get_current_uid() != 0) return -1;

    inodes[inode_idx].owner_uid = uid;
    inodes[inode_idx].owner_gid = gid;
    fs_dirty = 1;

    if (ata_is_available()) fs_sync();
    return 0;
}

int fs_link(const char* oldpath, const char* newpath) {
    uint32_t old_parent;
    char old_name[MAX_NAME_LEN];
    int old_inode = resolve_path(oldpath, &old_parent, old_name);
    if (old_inode < 0) return -1;

    /* Only regular files can be hardlinked (POSIX restriction) */
    if (inodes[old_inode].type != INODE_FILE) return -1;

    uint32_t new_parent;
    char new_name[MAX_NAME_LEN];
    resolve_path(newpath, &new_parent, new_name);
    if (new_name[0] == '\0') return -1;

    /* Check if destination already exists */
    if (dir_lookup(new_parent, new_name) >= 0) return -1;

    /* Check write permission on destination directory */
    if (!check_permission(&inodes[new_parent], PERM_W)) return -1;

    /* Add new directory entry pointing to the same inode */
    if (dir_add_entry(new_parent, new_name, (uint32_t)old_inode) < 0) return -1;

    /* Increment link count */
    inodes[old_inode].nlink++;
    inodes[old_inode].modified_at = rtc_get_epoch();
    fs_dirty = 1;

    if (ata_is_available()) fs_sync();
    return 0;
}

int fs_create_symlink(const char* target, const char* linkname) {
    /* VFS dispatch (on the linkname, not target) */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(linkname, &rel);
    if (mnt && mnt->ops->symlink)
        return mnt->ops->symlink(mnt->private_data, target, rel);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];

    resolve_path(linkname, &parent, name);
    if (name[0] == '\0') return -1;

    /* Check if already exists */
    if (dir_lookup(parent, name) >= 0) return -1;

    int idx = alloc_inode();
    if (idx < 0) return -1;

    inodes[idx].type = INODE_SYMLINK;
    inodes[idx].mode = 0777;
    inodes[idx].nlink = 1;
    inodes[idx].indirect_block = 0;
    inodes[idx].double_indirect = 0;
    inodes[idx].owner_uid = user_get_current_uid();
    inodes[idx].owner_gid = user_get_current_gid();

    /* Store target path in first data block */
    size_t tlen = strlen(target);
    if (tlen >= BLOCK_SIZE) {
        free_inode(idx);
        return -1;
    }

    int blk = alloc_block();
    if (blk < 0) {
        free_inode(idx);
        return -1;
    }

    memcpy(BLOCK_PTR(blk), target, tlen);
    inodes[idx].blocks[0] = blk;
    inodes[idx].num_blocks = 1;
    inodes[idx].size = tlen;

    if (dir_add_entry(parent, name, idx) < 0) {
        free_block(blk);
        free_inode(idx);
        return -1;
    }

    fs_dirty = 1;
    if (ata_is_available()) fs_sync();
    return 0;
}

int fs_readlink(const char* path, char* buf, size_t bufsize) {
    /* VFS dispatch */
    const char *rel;
    vfs_mount_t *mnt = vfs_resolve(path, &rel);
    if (mnt && mnt->ops->readlink)
        return mnt->ops->readlink(mnt->private_data, rel, buf, bufsize);
    if (mnt) return -1;

    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(path, &parent, name);
    if (inode_idx < 0) return -1;

    inode_t* node = &inodes[inode_idx];
    if (node->type != INODE_SYMLINK) return -1;
    if (node->num_blocks == 0) return -1;

    size_t tlen = node->size;
    if (tlen >= bufsize) tlen = bufsize - 1;

    memcpy(buf, BLOCK_PTR(node->blocks[0]), tlen);
    buf[tlen] = '\0';
    return 0;
}

int fs_rename(const char* old_name, const char* new_name) {
    if (!old_name || !new_name || new_name[0] == '\0') return -1;
    if (local_strncmp(old_name, ".", MAX_NAME_LEN) == 0 ||
        local_strncmp(old_name, "..", MAX_NAME_LEN) == 0) return -1;

    /* Check new name doesn't already exist */
    if (dir_lookup(sb.cwd_inode, new_name) >= 0) return -1;

    /* Find and rename the directory entry in-place */
    inode_t *dir = &inodes[sb.cwd_inode];
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t *entries = (dir_entry_t *)BLOCK_PTR(dir->blocks[b]);
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] != '\0' &&
                local_strncmp(entries[e].name, old_name, MAX_NAME_LEN) == 0) {
                local_strncpy(entries[e].name, new_name, MAX_NAME_LEN);
                mark_dirty(dir->blocks[b]);
                fs_dirty = 1;
                if (ata_is_available()) fs_sync();
                return 0;
            }
        }
    }
    return -1;
}

/* ---- initrd mounting ---- */

int fs_mount_initrd(const uint8_t* data, uint32_t size) {
    if (!data || size < 512) return -1;

    /* Parse tar headers and materialize files into the real FS */
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    int files_loaded = 0;

    while (ptr + 512 <= end) {
        /* Check for end-of-archive (two zero blocks) */
        int all_zero = 1;
        for (int i = 0; i < 512; i++) {
            if (ptr[i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) break;

        /* Parse tar header */
        char name[100];
        memcpy(name, ptr, 100);
        name[99] = '\0';

        /* Parse octal size field (offset 124, 12 bytes) */
        uint32_t fsize = 0;
        for (int i = 0; i < 11; i++) {
            char c = (char)ptr[124 + i];
            if (c >= '0' && c <= '7')
                fsize = fsize * 8 + (uint32_t)(c - '0');
        }

        /* File type: offset 156 */
        char typeflag = (char)ptr[156];

        /* Skip past header */
        ptr += 512;

        /* Strip leading ./ if present */
        char *fname = name;
        if (fname[0] == '.' && fname[1] == '/') fname += 2;
        if (fname[0] == '\0') {
            /* Skip data blocks */
            ptr += ((fsize + 511) / 512) * 512;
            continue;
        }

        /* Remove trailing slash for directories */
        size_t nlen = strlen(fname);
        if (nlen > 0 && fname[nlen - 1] == '/') fname[nlen - 1] = '\0';

        /* Build absolute path */
        char abspath[256];
        abspath[0] = '/';
        abspath[1] = '\0';
        local_strcat(abspath, fname);

        if (typeflag == '5' || (typeflag == '\0' && fsize == 0 && nlen > 0 && name[nlen - 1] == '/')) {
            /* Directory — create if doesn't exist */
            uint32_t parent;
            char dname[MAX_NAME_LEN];
            int existing = resolve_path(abspath, &parent, dname);
            if (existing < 0) {
                fs_create_file(abspath, 1);
            }
        } else if (typeflag == '2') {
            /* Symlink — link target is at tar offset 157, 100 bytes */
            char link_target[100];
            memcpy(link_target, ptr - 512 + 157, 100);
            link_target[99] = '\0';

            uint32_t parent;
            char dname[MAX_NAME_LEN];
            int existing = resolve_path(abspath, &parent, dname);
            if (existing < 0 && link_target[0] != '\0') {
                fs_create_symlink(link_target, abspath);
                files_loaded++;
            }
        } else if (typeflag == '0' || typeflag == '\0') {
            /* Regular file */
            uint32_t parent;
            char dname[MAX_NAME_LEN];
            int existing = resolve_path(abspath, &parent, dname);
            if (existing < 0) {
                /* Create and write file */
                if (fs_create_file(abspath, 0) == 0) {
                    if (fsize > 0 && ptr + fsize <= end) {
                        fs_write_file(abspath, ptr, fsize);
                    }
                    files_loaded++;
                }
            }
        }

        /* Advance past data blocks (512-byte aligned) */
        ptr += ((fsize + 511) / 512) * 512;
    }

    printf("[INITRD] Loaded %d files from initrd\n", files_loaded);
    return files_loaded;
}

void fs_get_io_stats(uint32_t *rd_ops, uint32_t *rd_bytes, uint32_t *wr_ops, uint32_t *wr_bytes) {
    if (rd_ops) *rd_ops = fs_rd_ops;
    if (rd_bytes) *rd_bytes = fs_rd_bytes;
    if (wr_ops) *wr_ops = fs_wr_ops;
    if (wr_bytes) *wr_bytes = fs_wr_bytes;
}

uint32_t fs_count_free_blocks(void) {
    uint32_t count = 0;
    for (uint32_t i = DISK_METADATA_BLOCKS; i < NUM_BLOCKS; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if (!(block_bitmap[byte] & (1 << bit)))
            count++;
    }
    return count;
}

uint32_t fs_count_free_inodes(void) {
    uint32_t count = 0;
    for (uint32_t i = 1; i < NUM_INODES; i++) {  /* skip root inode 0 */
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if (!(inode_bitmap[byte] & (1 << bit)))
            count++;
    }
    return count;
}
