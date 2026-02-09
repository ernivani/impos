#ifndef _KERNEL_QUOTA_H
#define _KERNEL_QUOTA_H

#include <stdint.h>

#define MAX_QUOTAS 16

typedef struct {
    uint16_t uid;
    uint16_t max_inodes;
    uint16_t max_blocks;
    uint16_t used_inodes;
    uint16_t used_blocks;
    int active;
} quota_entry_t;

void quota_initialize(void);
int  quota_set(uint16_t uid, uint16_t max_inodes, uint16_t max_blocks);
int  quota_check_inode(uint16_t uid);
int  quota_check_block(uint16_t uid, uint16_t blocks_needed);
void quota_add_inode(uint16_t uid);
void quota_remove_inode(uint16_t uid);
void quota_add_blocks(uint16_t uid, uint16_t count);
void quota_remove_blocks(uint16_t uid, uint16_t count);
quota_entry_t* quota_get(uint16_t uid);
void quota_save(void);
void quota_load(void);

#endif
