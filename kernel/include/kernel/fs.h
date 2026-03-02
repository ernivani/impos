#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <stddef.h>
#include <stdint.h>

/* ── FS v4 Geometry ─────────────────────────────────────────────── */

#define BLOCK_SIZE      4096
#define NUM_BLOCKS      65536       /* 256MB total */
#define NUM_INODES      4096
#define DIRECT_BLOCKS   8
#define MAX_NAME_LEN    28
#define INDIRECT_PTRS   (BLOCK_SIZE / sizeof(uint32_t))   /* 1024 */
#define DOUBLE_INDIRECT_PTRS  ((uint32_t)INDIRECT_PTRS * INDIRECT_PTRS) /* 1048576 */
#define MAX_DIRECT_SIZE (DIRECT_BLOCKS * BLOCK_SIZE)       /* 32KB */
/* Theoretical max: 32KB + 4MB + 4GB; capped by uint32_t size field */
#define MAX_FILE_SIZE   0xFFFFFFFFU

#define FS_VERSION      4

#define INODE_FREE      0
#define INODE_FILE      1
#define INODE_DIR       2
#define INODE_SYMLINK   3
#define INODE_CHARDEV   4

#define ROOT_INODE      0

#define LS_ALL          0x01
#define LS_LONG         0x02

/* Permission bits */
#define PERM_R          4
#define PERM_W          2
#define PERM_X          1

/* Device major numbers */
#define DEV_MAJOR_NULL    1
#define DEV_MAJOR_ZERO    2
#define DEV_MAJOR_TTY     3
#define DEV_MAJOR_URANDOM 4
#define DEV_MAJOR_DRM     5   /* /dev/dri/card0 — GPU DRM device */

/* ── Disk Layout (block-based, FS v4) ───────────────────────────── *
 *
 *   Block 0:        Superblock (4KB)
 *   Block 1:        Inode bitmap (4KB — covers 32768 bits, room for 4096 inodes)
 *   Block 2-3:      Block bitmap (8KB — 65536 bits, exact fit)
 *   Block 4-67:     Inode table  (64 blocks — 4096 inodes x 64B = 256KB)
 *   Block 68-1091:  Journal      (1024 blocks = 4MB)
 *   Block 1092+:    Data blocks  (64444 usable)
 */
#define SECTORS_PER_BLOCK       (BLOCK_SIZE / 512)  /* 8 */

#define DISK_BLK_SUPERBLOCK         0
#define DISK_BLK_INODE_BITMAP       1
#define DISK_BLK_BLOCK_BITMAP       2
#define DISK_BLK_BLOCK_BITMAP_COUNT 2
#define DISK_BLK_INODE_TABLE        4
#define DISK_BLK_INODE_TABLE_COUNT  64   /* 4096 * 64B / 4096 = 64 blocks */
#define DISK_BLK_JOURNAL            68   /* journal area starts here */
#define DISK_BLK_JOURNAL_COUNT      1024 /* 4MB journal */
#define DISK_METADATA_BLOCKS        1092 /* blocks 0-1091 reserved (meta + journal) */

/* Superblock flags */
#define FS_FLAG_DIRTY   0x01

typedef struct {
    uint32_t inode;
    char name[MAX_NAME_LEN];
} dir_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint16_t mode;              /* rwxrwxrwx in low 9 bits */
    uint16_t owner_uid;
    uint16_t owner_gid;
    uint32_t size;
    uint32_t blocks[DIRECT_BLOCKS];
    uint8_t  num_blocks;
    uint32_t indirect_block;    /* single-indirect block pointer, 0 = none */
    uint32_t double_indirect;   /* double-indirect block pointer, 0 = none */
    uint32_t created_at;        /* epoch: seconds since 2000-01-01 */
    uint32_t modified_at;
    uint16_t nlink;             /* hard link count */
    uint16_t accessed_hi;       /* high 16 bits of access time (reserved) */
} inode_t;  /* 64 bytes packed */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_inodes;
    uint32_t num_blocks;
    uint32_t block_size;
    uint32_t cwd_inode;
    uint32_t flags;             /* FS_FLAG_DIRTY etc */
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint32_t data_start_block;
    uint8_t  _pad[4096 - 40];  /* pad to one full block */
} superblock_t;

#define FS_MAGIC 0x494D504F

void fs_initialize(void);
int fs_create_file(const char* filename, uint8_t is_directory);
int fs_write_file(const char* filename, const uint8_t* data, size_t size);
int fs_read_file(const char* filename, uint8_t* buffer, size_t* size);
int fs_delete_file(const char* filename);
void fs_list_directory(int flags);
int fs_change_directory(const char* dirname);
int fs_change_directory_by_inode(uint32_t inode_num);
const char* fs_get_cwd(void);

int fs_sync(void);
int fs_load(void);

/* Permissions */
int fs_chmod(const char* path, uint16_t mode);
int fs_chown(const char* path, uint16_t uid, uint16_t gid);

/* Hardlinks */
int fs_link(const char* oldpath, const char* newpath);

/* Symlinks */
int fs_create_symlink(const char* target, const char* linkname);
int fs_readlink(const char* path, char* buf, size_t bufsize);

/* Device nodes */
int fs_create_device(const char* path, uint8_t major, uint8_t minor);

/* Initrd mounting */
int fs_mount_initrd(const uint8_t* data, uint32_t size);

/* Directory enumeration for GUI apps */
typedef struct {
    char     name[MAX_NAME_LEN];
    uint8_t  type;       /* INODE_FILE, INODE_DIR, INODE_CHARDEV, etc */
    uint32_t size;
    uint32_t inode;
    uint32_t modified_at; /* epoch: seconds since 2000-01-01 */
} fs_dir_entry_info_t;

int fs_enumerate_directory(fs_dir_entry_info_t *out, int max, int show_dot);

/* Helper functions for shell autocompletion */
uint32_t fs_get_cwd_inode(void);
int fs_read_inode(uint32_t inode_num, inode_t* out_inode);
int fs_read_block(uint32_t block_num, uint8_t* out_data);

/* Block-level partial read: read 'count' bytes starting at 'offset' from inode.
 * Returns bytes read, or <0 on error. */
int fs_read_at(uint32_t inode_num, uint8_t *buffer, uint32_t offset, uint32_t count);

/* Write 'count' bytes to inode at 'offset', extending the file as needed.
 * Returns bytes written, or <0 on error. */
int fs_write_at(uint32_t inode_num, const uint8_t *data, uint32_t offset, uint32_t count);

/* Truncate or extend file to new_size. Returns 0 on success. */
int fs_truncate(const char *path, uint32_t new_size);

/* Truncate by inode number (used by ftruncate syscall). Returns 0 on success. */
int fs_truncate_inode(uint32_t inode_num, uint32_t new_size);

/* Resolve a path to its parent inode and final name component.
 * Returns inode index of the final component (or -1 if not found).
 * out_parent receives the parent directory inode, out_name receives the final name. */
int fs_resolve_path(const char *path, uint32_t *out_parent, char *out_name);

/* Lookup a name within a directory inode. Returns child inode or -1. */
int fs_dir_lookup(uint32_t dir_inode, const char *name);

/* Rename a file/directory in the current directory */
int fs_rename(const char* old_name, const char* new_name);

/* I/O statistics */
void fs_get_io_stats(uint32_t *rd_ops, uint32_t *rd_bytes, uint32_t *wr_ops, uint32_t *wr_bytes);

/* Bitmap counting helpers for statfs */
uint32_t fs_count_free_blocks(void);
uint32_t fs_count_free_inodes(void);

#endif
