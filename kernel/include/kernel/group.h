#ifndef _KERNEL_GROUP_H
#define _KERNEL_GROUP_H

#include <stddef.h>
#include <stdint.h>

#define MAX_GROUPS      32
#define MAX_GROUP_NAME  32
#define MAX_MEMBERS     16

typedef struct {
    uint16_t gid;
    char name[MAX_GROUP_NAME];
    char members[MAX_MEMBERS][MAX_GROUP_NAME];
    uint8_t num_members;
    int active;
} group_t;

void group_initialize(void);
int group_load(void);
int group_save(void);

int group_create(const char* name, uint16_t gid);
int group_delete(uint16_t gid);
group_t* group_get_by_gid(uint16_t gid);
group_t* group_get_by_name(const char* name);
group_t* group_get_by_index(int index);

int group_add_member(uint16_t gid, const char* username);
int group_remove_member(uint16_t gid, const char* username);
int group_is_member(uint16_t gid, const char* username);

#endif
