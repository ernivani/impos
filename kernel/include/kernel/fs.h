#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <stddef.h>
#include <stdint.h>

#define BLOCK_SIZE      512
#define NUM_BLOCKS      256
#define NUM_INODES      64
#define DIRECT_BLOCKS   8
#define MAX_NAME_LEN    28
#define INDIRECT_PTRS   (BLOCK_SIZE / sizeof(uint32_t))
#define MAX_DIRECT_SIZE (DIRECT_BLOCKS * BLOCK_SIZE)
#define MAX_FILE_SIZE   (MAX_DIRECT_SIZE + INDIRECT_PTRS * BLOCK_SIZE)

#define INODE_FREE      0
#define INODE_FILE      1
#define INODE_DIR       2
#define INODE_SYMLINK   3

#define ROOT_INODE      0

#define LS_ALL          0x01
#define LS_LONG         0x02

/* Permission bits */
#define PERM_R          4
#define PERM_W          2
#define PERM_X          1

#define DISK_SECTOR_SUPERBLOCK  0
#define DISK_SECTOR_BITMAPS     1
#define DISK_SECTOR_INODES      3
#define DISK_SECTOR_DATA        35

/* Number of sectors needed for inode table */
#define INODE_SECTORS   ((sizeof(inode_t) * NUM_INODES + BLOCK_SIZE - 1) / BLOCK_SIZE)

typedef struct {
    uint32_t inode;
    char name[MAX_NAME_LEN];
} dir_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint16_t mode;          /* rwxrwxrwx in low 9 bits */
    uint16_t owner_uid;
    uint16_t owner_gid;
    uint32_t size;
    uint32_t blocks[DIRECT_BLOCKS];
    uint8_t  num_blocks;
    uint32_t indirect_block; /* single-indirect block pointer, 0 = none */
    uint32_t created_at;    /* epoch: seconds since 2000-01-01 */
    uint32_t modified_at;
    uint32_t accessed_at;
} inode_t;

typedef struct {
    uint32_t magic;      /* FS magic number */
    uint32_t num_inodes;
    uint32_t num_blocks;
    uint32_t cwd_inode;
    uint8_t block_bitmap[NUM_BLOCKS / 8];
    uint8_t inode_bitmap[NUM_INODES / 8];
    uint8_t _pad[456];   /* Pad to 512 bytes */
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

/* Symlinks */
int fs_create_symlink(const char* target, const char* linkname);
int fs_readlink(const char* path, char* buf, size_t bufsize);

/* Directory enumeration for GUI apps */
typedef struct {
    char     name[MAX_NAME_LEN];
    uint8_t  type;       /* INODE_FILE, INODE_DIR, etc */
    uint32_t size;
    uint32_t inode;
    uint32_t modified_at; /* epoch: seconds since 2000-01-01 */
} fs_dir_entry_info_t;

int fs_enumerate_directory(fs_dir_entry_info_t *out, int max, int show_dot);

/* Helper functions for shell autocompletion */
uint32_t fs_get_cwd_inode(void);
int fs_read_inode(uint32_t inode_num, inode_t* out_inode);
int fs_read_block(uint32_t block_num, uint8_t* out_data);

/* Rename a file/directory in the current directory */
int fs_rename(const char* old_name, const char* new_name);

/* I/O statistics */
void fs_get_io_stats(uint32_t *rd_ops, uint32_t *rd_bytes, uint32_t *wr_ops, uint32_t *wr_bytes);

#endif
