#include <kernel/hostname.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>

static char system_hostname[MAX_HOSTNAME] = "imposos";

void hostname_initialize(void) {
    /* Try to load from disk */
    if (hostname_load() != 0) {
        /* No hostname file, use default */
        strcpy(system_hostname, "imposos");
    }
}

const char* hostname_get(void) {
    return system_hostname;
}

int hostname_set(const char* name) {
    if (!name || !name[0] || strlen(name) >= MAX_HOSTNAME) {
        return -1;
    }
    
    strncpy(system_hostname, name, MAX_HOSTNAME - 1);
    system_hostname[MAX_HOSTNAME - 1] = '\0';
    
    return 0;
}

int hostname_load(void) {
    uint8_t buffer[MAX_HOSTNAME + 1];
    size_t len = sizeof(buffer);
    
    if (fs_read_file("/etc/hostname", buffer, &len) != 0) {
        return -1;
    }
    
    /* Remove trailing newline if present */
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else {
        buffer[len] = '\0';
    }
    
    strncpy(system_hostname, (char*)buffer, MAX_HOSTNAME - 1);
    system_hostname[MAX_HOSTNAME - 1] = '\0';
    
    return 0;
}

int hostname_save(void) {
    size_t len = strlen(system_hostname);
    
    if (len == 0) {
        return -1;
    }
    
    /* Create /etc if it doesn't exist */
    fs_create_file("/etc", 1);
    
    /* Save hostname with newline */
    char buffer[MAX_HOSTNAME + 2];
    snprintf(buffer, sizeof(buffer), "%s\n", system_hostname);
    
    fs_create_file("/etc/hostname", 0);
    return fs_write_file("/etc/hostname", (uint8_t*)buffer, strlen(buffer));
}
