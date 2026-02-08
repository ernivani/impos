#ifndef _KERNEL_USER_H
#define _KERNEL_USER_H

#include <stddef.h>
#include <stdint.h>

#define MAX_USERS 32
#define MAX_USERNAME 32
#define MAX_PASSWORD 64
#define MAX_HOME 128

/* User structure */
typedef struct {
    uint16_t uid;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];  /* Plain text for now - TODO: hash */
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
int user_authenticate(const char* username, const char* password);

/* Current user */
void user_set_current(const char* username);
const char* user_get_current(void);
uint16_t user_get_current_uid(void);

/* Check if system is initialized (has users) */
int user_system_initialized(void);

#endif
