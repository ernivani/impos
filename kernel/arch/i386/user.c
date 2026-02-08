#include <kernel/user.h>
#include <kernel/fs.h>
#include <kernel/env.h>
#include <kernel/hash.h>
#include <kernel/hostname.h>
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
        memset(users[i].password_salt, 0, HASH_SALT_SIZE);
        memset(users[i].password_hash, 0, HASH_OUTPUT_SIZE);
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
    
    /* Parse passwd file format: username:salt_hex:hash_hex:uid:gid:home */
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
            
            /* Parse line: username:salt_hex:hash_hex:uid:gid:home */
            char username[MAX_USERNAME];
            char salt_hex[HASH_SALT_SIZE * 2 + 1];
            char hash_hex[HASH_OUTPUT_SIZE * 2 + 1];
            char home[MAX_HOME];
            int uid, gid;
            
            char* p = line;
            int field = 0;
            char* field_start = p;
            
            username[0] = salt_hex[0] = hash_hex[0] = home[0] = '\0';
            uid = gid = 0;
            
            while (*p) {
                if (*p == ':') {
                    *p = '\0';
                    if (field == 0) {
                        strncpy(username, field_start, MAX_USERNAME - 1);
                        username[MAX_USERNAME - 1] = '\0';
                    } else if (field == 1) {
                        strncpy(salt_hex, field_start, HASH_SALT_SIZE * 2);
                        salt_hex[HASH_SALT_SIZE * 2] = '\0';
                    } else if (field == 2) {
                        strncpy(hash_hex, field_start, HASH_OUTPUT_SIZE * 2);
                        hash_hex[HASH_OUTPUT_SIZE * 2] = '\0';
                    } else if (field == 3) {
                        uid = 0;
                        for (char* d = field_start; *d >= '0' && *d <= '9'; d++) {
                            uid = uid * 10 + (*d - '0');
                        }
                    } else if (field == 4) {
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
            if (field == 5) {
                strncpy(home, field_start, MAX_HOME - 1);
                home[MAX_HOME - 1] = '\0';
            }
            
            /* Create user with hashed password */
            if (username[0] && salt_hex[0] && hash_hex[0]) {
                /* Find free slot */
                for (int i = 0; i < MAX_USERS; i++) {
                    if (!users[i].active) {
                        users[i].active = 1;
                        users[i].uid = uid;
                        users[i].gid = gid;
                        strncpy(users[i].username, username, MAX_USERNAME - 1);
                        users[i].username[MAX_USERNAME - 1] = '\0';
                        strncpy(users[i].home, home, MAX_HOME - 1);
                        users[i].home[MAX_HOME - 1] = '\0';
                        
                        /* Convert hex to binary */
                        hex_to_hash(salt_hex, users[i].password_salt, HASH_SALT_SIZE);
                        hex_to_hash(hash_hex, users[i].password_hash, HASH_OUTPUT_SIZE);
                        
                        user_count++;
                        break;
                    }
                }
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
    
    int user_count = 0;
    for (int i = 0; i < MAX_USERS && pos < sizeof(buffer) - 256; i++) {
        if (users[i].active) {
            /* Convert salt and hash to hex */
            char salt_hex[HASH_SALT_SIZE * 2 + 1];
            char hash_hex[HASH_OUTPUT_SIZE * 2 + 1];
            
            hash_to_hex(users[i].password_salt, HASH_SALT_SIZE, salt_hex, sizeof(salt_hex));
            hash_to_hex(users[i].password_hash, HASH_OUTPUT_SIZE, hash_hex, sizeof(hash_hex));
            
            /* Format: username:salt_hex:hash_hex:uid:gid:home\n */
            int written = snprintf(buffer + pos, sizeof(buffer) - pos,
                           "%s:%s:%s:%d:%d:%s\n",
                           users[i].username,
                           salt_hex,
                           hash_hex,
                           users[i].uid,
                           users[i].gid,
                           users[i].home);
            
            pos += written;
            user_count++;
        }
    }
    
    if (user_count == 0) {
        return -1;
    }
    
    /* Create /etc if it doesn't exist */
    fs_create_file("/etc", 1);
    
    /* Save to file */
    fs_create_file("/etc/passwd", 0);
    int result = fs_write_file("/etc/passwd", (uint8_t*)buffer, pos);
    
    return result;
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
            strncpy(users[i].home, home, MAX_HOME - 1);
            users[i].home[MAX_HOME - 1] = '\0';
            
            /* Generate salt and hash password */
            hash_generate_salt(users[i].password_salt, HASH_SALT_SIZE);
            hash_password(password, users[i].password_salt, users[i].password_hash);
            
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

user_t* user_authenticate(const char* username, const char* password) {
    user_t* user = user_get(username);
    if (!user) {
        return NULL;
    }
    
    /* Verify password hash */
    if (hash_verify(password, user->password_salt, user->password_hash) == 0) {
        return user;
    }
    
    return NULL;
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
        
        /* Build PS1 prompt: user@hostname:dir$ or # for root */
        char ps1[128];
        snprintf(ps1, sizeof(ps1), "%s@%s:\\w%s ", 
                 username, 
                 hostname_get(),
                 user->uid == 0 ? "#" : "$");
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
