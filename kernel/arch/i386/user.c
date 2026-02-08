#include <kernel/user.h>
#include <kernel/fs.h>
#include <kernel/env.h>
#include <string.h>
#include <stdio.h>

static user_t users[MAX_USERS];
static char current_user[MAX_USERNAME] = {0};
static int users_initialized = 0;

void user_initialize(void) {
    if (users_initialized) {
        return;
    }
    
    /* Clear all users */
    for (int i = 0; i < MAX_USERS; i++) {
        users[i].active = 0;
        users[i].uid = 0;
        users[i].gid = 0;
        users[i].username[0] = '\0';
        users[i].password[0] = '\0';
        users[i].home[0] = '\0';
    }
    
    current_user[0] = '\0';
    users_initialized = 1;
    
    /* Try to load users from disk */
    user_load();
}

int user_load(void) {
    uint8_t buffer[4096];
    size_t len = sizeof(buffer);
    
    if (fs_read_file("/etc/passwd", buffer, &len) != 0) {
        return -1;  /* No passwd file */
    }
    
    /* Parse passwd file format: username:password:uid:gid:home */
    char* line = (char*)buffer;
    char* end = (char*)buffer + len;
    int user_count = 0;
    
    while (line < end && *line && user_count < MAX_USERS) {
        /* Find end of line */
        char* line_end = line;
        while (line_end < end && *line_end && *line_end != '\n') {
            line_end++;
        }
        
        if (line_end > line) {
            *line_end = '\0';
            
            /* Parse line: username:password:uid:gid:home */
            char username[MAX_USERNAME];
            char password[MAX_PASSWORD];
            char home[MAX_HOME];
            int uid, gid;
            
            char* p = line;
            int field = 0;
            char* field_start = p;
            
            username[0] = password[0] = home[0] = '\0';
            uid = gid = 0;
            
            while (*p) {
                if (*p == ':') {
                    *p = '\0';
                    if (field == 0) {
                        strncpy(username, field_start, MAX_USERNAME - 1);
                        username[MAX_USERNAME - 1] = '\0';
                    } else if (field == 1) {
                        strncpy(password, field_start, MAX_PASSWORD - 1);
                        password[MAX_PASSWORD - 1] = '\0';
                    } else if (field == 2) {
                        uid = 0;
                        for (char* d = field_start; *d >= '0' && *d <= '9'; d++) {
                            uid = uid * 10 + (*d - '0');
                        }
                    } else if (field == 3) {
                        gid = 0;
                        for (char* d = field_start; *d >= '0' && *d <= '9'; d++) {
                            gid = gid * 10 + (*d - '0');
                        }
                    }
                    field++;
                    field_start = p + 1;
                }
                p++;
            }
            
            /* Last field is home */
            if (field == 4) {
                strncpy(home, field_start, MAX_HOME - 1);
                home[MAX_HOME - 1] = '\0';
            }
            
            /* Create user */
            if (username[0]) {
                user_create(username, password, home, uid, gid);
                user_count++;
            }
        }
        
        line = line_end + 1;
    }
    
    return user_count > 0 ? 0 : -1;
}

int user_save(void) {
    /* Build passwd file content */
    char buffer[4096];
    size_t pos = 0;
    
    for (int i = 0; i < MAX_USERS && pos < sizeof(buffer) - 128; i++) {
        if (users[i].active) {
            /* Format: username:password:uid:gid:home\n */
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                           "%s:%s:%d:%d:%s\n",
                           users[i].username,
                           users[i].password,
                           users[i].uid,
                           users[i].gid,
                           users[i].home);
        }
    }
    
    /* Create /etc if it doesn't exist */
    fs_create_file("/etc", 1);
    
    /* Save to file */
    fs_create_file("/etc/passwd", 0);
    return fs_write_file("/etc/passwd", (uint8_t*)buffer, pos);
}

int user_create(const char* username, const char* password, const char* home, uint16_t uid, uint16_t gid) {
    if (!username || !username[0]) {
        return -1;
    }
    
    /* Check if user already exists */
    if (user_exists(username)) {
        return -1;
    }
    
    /* Find free slot */
    for (int i = 0; i < MAX_USERS; i++) {
        if (!users[i].active) {
            users[i].active = 1;
            users[i].uid = uid;
            users[i].gid = gid;
            strncpy(users[i].username, username, MAX_USERNAME - 1);
            users[i].username[MAX_USERNAME - 1] = '\0';
            strncpy(users[i].password, password, MAX_PASSWORD - 1);
            users[i].password[MAX_PASSWORD - 1] = '\0';
            strncpy(users[i].home, home, MAX_HOME - 1);
            users[i].home[MAX_HOME - 1] = '\0';
            return 0;
        }
    }
    
    return -1;  /* No free slots */
}

int user_exists(const char* username) {
    return user_get(username) != NULL;
}

user_t* user_get(const char* username) {
    if (!username || !username[0]) {
        return NULL;
    }
    
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].active && strcmp(users[i].username, username) == 0) {
            return &users[i];
        }
    }
    
    return NULL;
}

user_t* user_get_by_uid(uint16_t uid) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].active && users[i].uid == uid) {
            return &users[i];
        }
    }
    return NULL;
}

int user_authenticate(const char* username, const char* password) {
    user_t* user = user_get(username);
    if (!user) {
        return -1;
    }
    
    return strcmp(user->password, password) == 0 ? 0 : -1;
}

void user_set_current(const char* username) {
    if (!username) {
        current_user[0] = '\0';
        return;
    }
    
    strncpy(current_user, username, MAX_USERNAME - 1);
    current_user[MAX_USERNAME - 1] = '\0';
    
    /* Update environment variables */
    env_set("USER", username);
    
    user_t* user = user_get(username);
    if (user) {
        env_set("HOME", user->home);
        
        /* Build PS1 prompt: username$ or username# for root */
        char ps1[64];
        snprintf(ps1, sizeof(ps1), "%s%s ", username, user->uid == 0 ? "#" : "$");
        env_set("PS1", ps1);
    }
}

const char* user_get_current(void) {
    return current_user[0] ? current_user : NULL;
}

uint16_t user_get_current_uid(void) {
    if (!current_user[0]) {
        return 65535;  /* Nobody */
    }
    
    user_t* user = user_get(current_user);
    return user ? user->uid : 65535;
}

int user_system_initialized(void) {
    /* Check if we have at least one user */
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].active) {
            return 1;
        }
    }
    return 0;
}
