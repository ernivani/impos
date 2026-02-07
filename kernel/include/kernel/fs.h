#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <stddef.h>
#include <stdint.h>

#define BLOCK_SIZE      512
#define NUM_BLOCKS      256
#define NUM_INODES      64
#define DIRECT_BLOCKS   8
#define MAX_NAME_LEN    28
#define MAX_FILE_SIZE   (DIRECT_BLOCKS * BLOCK_SIZE)

#define INODE_FREE      0
#define INODE_FILE      1
#define INODE_DIR       2

#define ROOT_INODE      0

#define LS_ALL          0x01
#define LS_LONG         0x02

#define DISK_SECTOR_SUPERBLOCK  0
#define DISK_SECTOR_BITMAPS     1
#define DISK_SECTOR_INODES      3
#define DISK_SECTOR_DATA        35

typedef struct {
    uint32_t inode;
    char name[MAX_NAME_LEN];
} dir_entry_t;

typedef struct {
    uint8_t type;
    uint32_t size;
    uint32_t blocks[DIRECT_BLOCKS];
    uint8_t num_blocks;
    uint8_t padding[3];
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

/* Helper functions for shell autocompletion */
uint32_t fs_get_cwd_inode(void);
int fs_read_inode(uint32_t inode_num, inode_t* out_inode);
int fs_read_block(uint32_t block_num, uint8_t* out_data);

#endif
