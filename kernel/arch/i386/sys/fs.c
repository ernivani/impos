#include <kernel/fs.h>
#include <kernel/ata.h>
#include <kernel/user.h>
#include <kernel/group.h>
#include <string.h>
#include <stdio.h>

static superblock_t sb;
static inode_t inodes[NUM_INODES];
static uint8_t data_blocks[NUM_BLOCKS][BLOCK_SIZE];
static int fs_dirty = 0;
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

static int bitmap_find_free(const uint8_t* map, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!bitmap_test(map, i)) return (int)i;
    }
    return -1;
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
    int idx = bitmap_find_free(sb.inode_bitmap, NUM_INODES);
    if (idx < 0) return -1;
    bitmap_set(sb.inode_bitmap, idx);
    memset(&inodes[idx], 0, sizeof(inode_t));
    fs_dirty = 1;
    return idx;
}

static int alloc_block(void) {
    int idx = bitmap_find_free(sb.block_bitmap, NUM_BLOCKS);
    if (idx < 0) return -1;
    bitmap_set(sb.block_bitmap, idx);
    memset(data_blocks[idx], 0, BLOCK_SIZE);
    fs_dirty = 1;
    return idx;
}

static void free_inode(int idx) {
    bitmap_clear(sb.inode_bitmap, idx);
    inodes[idx].type = INODE_FREE;
    fs_dirty = 1;
}

static void free_block(int idx) {
    bitmap_clear(sb.block_bitmap, idx);
    fs_dirty = 1;
}

/* ---- directory operations ---- */

static int dir_lookup(uint32_t dir_inode, const char* name) {
    inode_t* dir = &inodes[dir_inode];
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)data_blocks[dir->blocks[b]];
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
        dir_entry_t* entries = (dir_entry_t*)data_blocks[dir->blocks[b]];
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] == '\0') {
                entries[e].inode = child_inode;
                local_strncpy(entries[e].name, name, MAX_NAME_LEN);
                dir->size += sizeof(dir_entry_t);
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

    dir_entry_t* entries = (dir_entry_t*)data_blocks[blk];
    entries[0].inode = child_inode;
    local_strncpy(entries[0].name, name, MAX_NAME_LEN);
    dir->size += sizeof(dir_entry_t);
    fs_dirty = 1;
    return 0;
}

static int dir_remove_entry(uint32_t dir_inode, const char* name) {
    inode_t* dir = &inodes[dir_inode];
    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)data_blocks[dir->blocks[b]];
        int entries_per_block = BLOCK_SIZE / sizeof(dir_entry_t);
        for (int e = 0; e < entries_per_block; e++) {
            if (entries[e].name[0] != '\0' &&
                local_strncmp(entries[e].name, name, MAX_NAME_LEN) == 0) {
                memset(&entries[e], 0, sizeof(dir_entry_t));
                dir->size -= sizeof(dir_entry_t);
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
                char target[BLOCK_SIZE];
                if (inodes[child].num_blocks == 0) return -1;
                memcpy(target, data_blocks[inodes[child].blocks[0]], inodes[child].size);
                target[inodes[child].size] = '\0';
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

    int blk = alloc_block();
    if (blk < 0) return -1;
    inode->blocks[0] = blk;
    inode->num_blocks = 1;

    dir_entry_t* entries = (dir_entry_t*)data_blocks[blk];
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
    memcpy(out_data, data_blocks[block_num], BLOCK_SIZE);
    return 0;
}

/* ---- disk persistence ---- */

int fs_sync(void) {
    if (!ata_is_available()) {
        return -1;
    }

    if (!fs_dirty) {
        return 0;  /* Nothing to sync */
    }

    /* Write superblock */
    sb.magic = FS_MAGIC;
    if (ata_write_sectors(DISK_SECTOR_SUPERBLOCK, 1, (uint8_t*)&sb) != 0) {
        return -1;
    }

    /* Write inodes */
    if (ata_write_sectors(DISK_SECTOR_INODES, INODE_SECTORS, (uint8_t*)inodes) != 0) {
        return -1;
    }

    /* Write data blocks (256 blocks of 512 bytes each) */
    for (int i = 0; i < NUM_BLOCKS; i++) {
        if (bitmap_test(sb.block_bitmap, i)) {
            if (ata_write_sectors(DISK_SECTOR_DATA + i, 1, data_blocks[i]) != 0) {
                return -1;
            }
        }
    }

    /* Flush disk cache */
    if (ata_flush() != 0) {
        return -1;
    }

    fs_dirty = 0;
    return 0;
}

int fs_load(void) {
    if (!ata_is_available()) {
        return -1;
    }

    /* Read superblock */
    if (ata_read_sectors(DISK_SECTOR_SUPERBLOCK, 1, (uint8_t*)&sb) != 0) {
        return -1;
    }

    /* Verify magic number */
    if (sb.magic != FS_MAGIC) {
        return -1;
    }

    /* Validate superblock fields */
    if (sb.num_inodes != NUM_INODES || sb.num_blocks != NUM_BLOCKS) {
        return -1;
    }

    /* Read inodes */
    if (ata_read_sectors(DISK_SECTOR_INODES, INODE_SECTORS, (uint8_t*)inodes) != 0) {
        return -1;
    }

    /* Read data blocks */
    for (int i = 0; i < NUM_BLOCKS; i++) {
        if (bitmap_test(sb.block_bitmap, i)) {
            if (ata_read_sectors(DISK_SECTOR_DATA + i, 1, data_blocks[i]) != 0) {
                return -1;
            }
        }
    }

    /* Validate active inodes */
    for (uint32_t i = 0; i < NUM_INODES; i++) {
        if (!bitmap_test(sb.inode_bitmap, i))
            continue;
        inode_t* node = &inodes[i];
        if (node->type > INODE_SYMLINK) {
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
    }

    /* Validate cwd_inode */
    if (sb.cwd_inode >= NUM_INODES ||
        !bitmap_test(sb.inode_bitmap, sb.cwd_inode) ||
        inodes[sb.cwd_inode].type != INODE_DIR) {
        sb.cwd_inode = ROOT_INODE;
    }

    fs_dirty = 0;
    return 0;
}

/* ---- public API ---- */

void fs_initialize(void) {
    /* Try to load from disk first */
    if (ata_is_available() && fs_load() == 0) {
        return;
    }

    /* Otherwise initialize new filesystem in memory */
    memset(&sb, 0, sizeof(sb));
    memset(inodes, 0, sizeof(inodes));
    memset(data_blocks, 0, sizeof(data_blocks));

    sb.magic = FS_MAGIC;
    sb.num_inodes = NUM_INODES;
    sb.num_blocks = NUM_BLOCKS;

    /* allocate root inode */
    bitmap_set(sb.inode_bitmap, ROOT_INODE);
    sb.cwd_inode = ROOT_INODE;
    init_dir_inode(ROOT_INODE, ROOT_INODE);
    inodes[ROOT_INODE].mode = 0755;
    inodes[ROOT_INODE].owner_uid = 0;
    inodes[ROOT_INODE].owner_gid = 0;
    inodes[ROOT_INODE].indirect_block = 0;

    /* create default directory hierarchy */
    fs_create_file("/home", 1);
    fs_create_file("/home/root", 1);
    fs_change_directory("/home/root");

    fs_dirty = 1;
    
    /* Auto-sync if disk available */
    if (ata_is_available()) {
        fs_sync();
    }
}

int fs_create_file(const char* filename, uint8_t is_directory) {
    uint32_t parent;
    char name[MAX_NAME_LEN];

    resolve_path(filename, &parent, name);
    if (name[0] == '\0') return -1;

    /* check if already exists */
    if (dir_lookup(parent, name) >= 0) return -1;

    int idx = alloc_inode();
    if (idx < 0) return -1;

    if (is_directory) {
        if (init_dir_inode(idx, parent) < 0) {
            free_inode(idx);
            return -1;
        }
        inodes[idx].mode = 0755;
    } else {
        inodes[idx].type = INODE_FILE;
        inodes[idx].size = 0;
        inodes[idx].num_blocks = 0;
        inodes[idx].indirect_block = 0;
        inodes[idx].mode = 0644;
    }
    inodes[idx].owner_uid = user_get_current_uid();
    inodes[idx].owner_gid = user_get_current_gid();

    if (dir_add_entry(parent, name, idx) < 0) {
        /* clean up allocated blocks */
        for (uint8_t b = 0; b < inodes[idx].num_blocks; b++)
            free_block(inodes[idx].blocks[b]);
        free_inode(idx);
        return -1;
    }

    /* Auto-sync if disk available */
    if (ata_is_available()) {
        fs_sync();
    }

    return 0;
}

int fs_write_file(const char* filename, const uint8_t* data, size_t size) {
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    inode_t* inode = &inodes[inode_idx];
    if (inode->type != INODE_FILE) return -1;
    if (size > MAX_FILE_SIZE) return -1;
    if (!check_permission(inode, PERM_W)) return -1;

    /* free old direct blocks */
    for (uint8_t b = 0; b < inode->num_blocks; b++)
        free_block(inode->blocks[b]);

    /* free old indirect blocks */
    if (inode->indirect_block != 0) {
        uint32_t* ptrs = (uint32_t*)data_blocks[inode->indirect_block];
        for (size_t i = 0; i < INDIRECT_PTRS; i++) {
            if (ptrs[i] != 0)
                free_block(ptrs[i]);
        }
        free_block(inode->indirect_block);
        inode->indirect_block = 0;
    }

    inode->num_blocks = 0;
    inode->size = 0;

    /* allocate new blocks and write data */
    size_t remaining = size;
    size_t offset = 0;

    /* Fill direct blocks first */
    while (remaining > 0 && inode->num_blocks < DIRECT_BLOCKS) {
        int blk = alloc_block();
        if (blk < 0) return -1;
        inode->blocks[inode->num_blocks++] = blk;

        size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        memcpy(data_blocks[blk], data + offset, chunk);
        offset += chunk;
        remaining -= chunk;
    }

    /* If more data, use indirect block */
    if (remaining > 0) {
        int ind_blk = alloc_block();
        if (ind_blk < 0) return -1;
        inode->indirect_block = ind_blk;
        uint32_t* ptrs = (uint32_t*)data_blocks[ind_blk];
        memset(ptrs, 0, BLOCK_SIZE);

        size_t ind_idx = 0;
        while (remaining > 0 && ind_idx < INDIRECT_PTRS) {
            int blk = alloc_block();
            if (blk < 0) return -1;
            ptrs[ind_idx++] = blk;

            size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
            memcpy(data_blocks[blk], data + offset, chunk);
            offset += chunk;
            remaining -= chunk;
        }
    }

    inode->size = size;
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
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    inode_t* inode = &inodes[inode_idx];

    /* Follow symlinks */
    if (inode->type == INODE_SYMLINK) {
        char target[BLOCK_SIZE];
        if (inode->num_blocks == 0) return -1;
        memcpy(target, data_blocks[inode->blocks[0]], inode->size);
        target[inode->size] = '\0';
        return fs_read_file(target, buffer, size);
    }

    if (inode->type != INODE_FILE) return -1;
    if (!check_permission(inode, PERM_R)) return -1;

    size_t remaining = inode->size;
    size_t offset = 0;

    /* Read direct blocks */
    for (uint8_t b = 0; b < inode->num_blocks && remaining > 0; b++) {
        size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        memcpy(buffer + offset, data_blocks[inode->blocks[b]], chunk);
        offset += chunk;
        remaining -= chunk;
    }

    /* Read indirect blocks */
    if (remaining > 0 && inode->indirect_block != 0) {
        uint32_t* ptrs = (uint32_t*)data_blocks[inode->indirect_block];
        for (size_t i = 0; i < INDIRECT_PTRS && remaining > 0; i++) {
            if (ptrs[i] == 0) break;
            size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
            memcpy(buffer + offset, data_blocks[ptrs[i]], chunk);
            offset += chunk;
            remaining -= chunk;
        }
    }

    *size = inode->size;
    fs_rd_ops++;
    fs_rd_bytes += (uint32_t)(*size);
    return 0;
}

int fs_delete_file(const char* filename) {
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
            dir_entry_t* entries = (dir_entry_t*)data_blocks[inode->blocks[b]];
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

    /* free direct data blocks */
    for (uint8_t b = 0; b < inode->num_blocks; b++)
        free_block(inode->blocks[b]);

    /* free indirect blocks */
    if (inode->indirect_block != 0) {
        uint32_t* ptrs = (uint32_t*)data_blocks[inode->indirect_block];
        for (size_t i = 0; i < INDIRECT_PTRS; i++) {
            if (ptrs[i] != 0)
                free_block(ptrs[i]);
        }
        free_block(inode->indirect_block);
    }

    free_inode(inode_idx);

    /* remove from parent */
    dir_remove_entry(parent, name);
    
    /* Auto-sync if disk available */
    if (ata_is_available()) {
        fs_sync();
    }
    
    return 0;
}

static void print_long_entry(const char* name, uint32_t ino) {
    inode_t* node = &inodes[ino];
    char perm[11];
    uint16_t m = node->mode;

    /* Type character */
    if (node->type == INODE_DIR)       perm[0] = 'd';
    else if (node->type == INODE_SYMLINK) perm[0] = 'l';
    else                                perm[0] = '-';

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

    printf("%s  %s  %d  %s", perm, owner, (int)node->size, name);

    /* Print symlink target */
    if (node->type == INODE_SYMLINK && node->num_blocks > 0) {
        char target[BLOCK_SIZE];
        size_t tlen = node->size < BLOCK_SIZE ? node->size : BLOCK_SIZE - 1;
        memcpy(target, data_blocks[node->blocks[0]], tlen);
        target[tlen] = '\0';
        printf(" -> %s", target);
    }

    printf("\n");
}

void fs_list_directory(int flags) {
    inode_t* dir = &inodes[sb.cwd_inode];
    int show_all = flags & LS_ALL;
    int long_fmt = flags & LS_LONG;
    int col = 0;

    for (uint8_t b = 0; b < dir->num_blocks; b++) {
        dir_entry_t* entries = (dir_entry_t*)data_blocks[dir->blocks[b]];
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
    inode_t* dir = &inodes[sb.cwd_inode];
    int count = 0;

    for (uint8_t b = 0; b < dir->num_blocks && count < max; b++) {
        dir_entry_t* entries = (dir_entry_t*)data_blocks[dir->blocks[b]];
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
            } else {
                out[count].type = 0;
                out[count].size = 0;
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
            dir_entry_t* entries = (dir_entry_t*)data_blocks[pdir->blocks[b]];
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

int fs_chmod(const char* path, uint16_t mode) {
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

int fs_create_symlink(const char* target, const char* linkname) {
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
    inodes[idx].indirect_block = 0;
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

    memcpy(data_blocks[blk], target, tlen);
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
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(path, &parent, name);
    if (inode_idx < 0) return -1;

    inode_t* node = &inodes[inode_idx];
    if (node->type != INODE_SYMLINK) return -1;
    if (node->num_blocks == 0) return -1;

    size_t tlen = node->size;
    if (tlen >= bufsize) tlen = bufsize - 1;

    memcpy(buf, data_blocks[node->blocks[0]], tlen);
    buf[tlen] = '\0';
    return 0;
}

void fs_get_io_stats(uint32_t *rd_ops, uint32_t *rd_bytes, uint32_t *wr_ops, uint32_t *wr_bytes) {
    if (rd_ops) *rd_ops = fs_rd_ops;
    if (rd_bytes) *rd_bytes = fs_rd_bytes;
    if (wr_ops) *wr_ops = fs_wr_ops;
    if (wr_bytes) *wr_bytes = fs_wr_bytes;
}
