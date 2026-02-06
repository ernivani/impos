#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>

static superblock_t sb;
static inode_t inodes[NUM_INODES];
static uint8_t data_blocks[NUM_BLOCKS][BLOCK_SIZE];

/* ---- local string helpers (not in our libc) ---- */

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

/* ---- bitmap helpers ---- */

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

/* ---- allocation ---- */

static int alloc_inode(void) {
    int idx = bitmap_find_free(sb.inode_bitmap, NUM_INODES);
    if (idx < 0) return -1;
    bitmap_set(sb.inode_bitmap, idx);
    memset(&inodes[idx], 0, sizeof(inode_t));
    return idx;
}

static int alloc_block(void) {
    int idx = bitmap_find_free(sb.block_bitmap, NUM_BLOCKS);
    if (idx < 0) return -1;
    bitmap_set(sb.block_bitmap, idx);
    memset(data_blocks[idx], 0, BLOCK_SIZE);
    return idx;
}

static void free_inode(int idx) {
    bitmap_clear(sb.inode_bitmap, idx);
    inodes[idx].type = INODE_FREE;
}

static void free_block(int idx) {
    bitmap_clear(sb.block_bitmap, idx);
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
                return 0;
            }
        }
    }
    return -1;
}

/* ---- path resolution ---- */

static int resolve_path(const char* path, uint32_t* out_parent, char* out_name) {
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
            if (child < 0 || inodes[child].type != INODE_DIR) return -1;
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

/* ---- public API ---- */

void fs_initialize(void) {
    memset(&sb, 0, sizeof(sb));
    memset(inodes, 0, sizeof(inodes));
    memset(data_blocks, 0, sizeof(data_blocks));

    sb.num_inodes = NUM_INODES;
    sb.num_blocks = NUM_BLOCKS;

    /* allocate root inode */
    bitmap_set(sb.inode_bitmap, ROOT_INODE);
    sb.cwd_inode = ROOT_INODE;
    init_dir_inode(ROOT_INODE, ROOT_INODE);

    /* create default directory hierarchy */
    fs_create_file("/home", 1);
    fs_create_file("/home/root", 1);
    fs_change_directory("/home/root");
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
    } else {
        inodes[idx].type = INODE_FILE;
        inodes[idx].size = 0;
        inodes[idx].num_blocks = 0;
    }

    if (dir_add_entry(parent, name, idx) < 0) {
        /* clean up allocated blocks */
        for (uint8_t b = 0; b < inodes[idx].num_blocks; b++)
            free_block(inodes[idx].blocks[b]);
        free_inode(idx);
        return -1;
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

    /* free old blocks */
    for (uint8_t b = 0; b < inode->num_blocks; b++)
        free_block(inode->blocks[b]);
    inode->num_blocks = 0;
    inode->size = 0;

    /* allocate new blocks and write data */
    size_t remaining = size;
    size_t offset = 0;
    while (remaining > 0) {
        int blk = alloc_block();
        if (blk < 0) return -1;
        inode->blocks[inode->num_blocks++] = blk;

        size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        memcpy(data_blocks[blk], data + offset, chunk);
        offset += chunk;
        remaining -= chunk;
    }
    inode->size = size;
    return 0;
}

int fs_read_file(const char* filename, uint8_t* buffer, size_t* size) {
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    inode_t* inode = &inodes[inode_idx];
    if (inode->type != INODE_FILE) return -1;

    size_t remaining = inode->size;
    size_t offset = 0;
    for (uint8_t b = 0; b < inode->num_blocks && remaining > 0; b++) {
        size_t chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        memcpy(buffer + offset, data_blocks[inode->blocks[b]], chunk);
        offset += chunk;
        remaining -= chunk;
    }
    *size = inode->size;
    return 0;
}

int fs_delete_file(const char* filename) {
    uint32_t parent;
    char name[MAX_NAME_LEN];
    int inode_idx = resolve_path(filename, &parent, name);

    if (inode_idx < 0) return -1;
    if ((uint32_t)inode_idx == ROOT_INODE) return -1;

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

    /* free data blocks */
    for (uint8_t b = 0; b < inode->num_blocks; b++)
        free_block(inode->blocks[b]);
    free_inode(inode_idx);

    /* remove from parent */
    dir_remove_entry(parent, name);
    return 0;
}

static void print_long_entry(const char* name, uint32_t ino) {
    inode_t* node = &inodes[ino];
    if (node->type == INODE_DIR)
        printf("drwxr-xr-x  root root  %d  %s\n", (int)node->size, name);
    else
        printf("-rw-r--r--  root root  %d  %s\n", (int)node->size, name);
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
    sb.cwd_inode = (uint32_t)inode_idx;
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
