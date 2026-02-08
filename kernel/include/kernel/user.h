#ifndef _KERNEL_USER_H
#define _KERNEL_USER_H

#include <stddef.h>
#include <stdint.h>
#include <kernel/hash.h>

#define MAX_USERS 32
#define MAX_USERNAME 32
#define MAX_HOME 128

/* User structure */
typedef struct {
    uint16_t uid;
    char username[MAX_USERNAME];
    uint8_t password_salt[HASH_SALT_SIZE];
    uint8_t password_hash[HASH_OUTPUT_SIZE];
    char home[MAX_HOME];
    uint16_t gid;
    int active;
} user_t;

/* Initialize user system */
void user_initialize(void);

/* Load/save users from/to /etc/passwd */
int user_load(void);
int user_save(void);

/* User management */
int user_create(const char* username, const char* password, const char* home, uint16_t uid, uint16_t gid);
int user_exists(const char* username);
user_t* user_get(const char* username);
user_t* user_get_by_uid(uint16_t uid);

/* Authentication */
user_t* user_authenticate(const char* username, const char* password);

/* Current user */
void user_set_current(const char* username);
const char* user_get_current(void);
uint16_t user_get_current_uid(void);
uint16_t user_get_current_gid(void);

/* Delete a user */
int user_delete(const char* username);

/* Get next available UID >= 1000 */
uint16_t user_next_uid(void);

/* Check if system is initialized (has users) */
int user_system_initialized(void);

#endif
