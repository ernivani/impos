#include <kernel/config.h>
#include <kernel/fs.h>
#include <kernel/shell.h>
#include <kernel/user.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static system_config_t sys_config;
static int config_initialized = 0;

void config_initialize(void) {
    if (config_initialized) {
        return;
    }

    /* Set default values */
    memset(&sys_config, 0, sizeof(system_config_t));
    sys_config.keyboard_layout = KB_LAYOUT_FR;
    sys_config.datetime.year = 2026;
    sys_config.datetime.month = 2;
    sys_config.datetime.day = 7;
    sys_config.datetime.hour = 12;
    sys_config.datetime.minute = 0;
    sys_config.datetime.second = 0;
    sys_config.uptime_seconds = 0;
    strcpy(sys_config.timezone, "Europe/Paris");
    sys_config.use_24h_format = 1;
    sys_config.auto_dst = 1;

    config_initialized = 1;

    /* Try to load from disk */
    if (config_load() == 0) {
        /* Config loaded successfully */
    }

    /* Try to load history */
    if (config_load_history() == 0) {
        /* History loaded successfully */
    }
    
    /* Apply keyboard layout */
    keyboard_set_layout(sys_config.keyboard_layout);
}

int config_load(void) {
    uint8_t buffer[sizeof(system_config_t) + 64]; /* extra room for old/future formats */
    size_t size;

    /* Create /etc directory if it doesn't exist */
    fs_create_file("/etc", 1);

    if (fs_read_file(CONFIG_FILE, buffer, &size) != 0) {
        return -1;
    }

    if (size == 0) {
        return -1;
    }

    /* Load what we can — if config grew, new fields keep defaults from memset.
       If config shrunk (unlikely), just load the smaller portion. */
    size_t copy = size < sizeof(system_config_t) ? size : sizeof(system_config_t);
    memcpy(&sys_config, buffer, copy);

    /* If struct grew, re-save so disk matches current format */
    if (size != sizeof(system_config_t)) {
        config_save();
    }

    return 0;
}

int config_save(void) {
    /* Elevate to root for system config writes (config is owned by root) */
    const char *prev_user = user_get_current();
    char saved_user[64] = {0};
    if (prev_user) strncpy(saved_user, prev_user, 63);
    user_set_current("root");

    /* Create /etc directory if it doesn't exist (ignore error if exists) */
    fs_create_file("/etc", 1);

    /* Create config file if it doesn't exist */
    fs_create_file(CONFIG_FILE, 0);

    /* Write new config */
    int ret = fs_write_file(CONFIG_FILE, (const uint8_t*)&sys_config, sizeof(system_config_t));

    /* Restore original user */
    if (saved_user[0])
        user_set_current(saved_user);

    if (ret != 0)
        return -1;

    /* Force sync to disk */
    fs_sync();

    return 0;
}

system_config_t* config_get(void) {
    return &sys_config;
}

void config_set_keyboard_layout(uint8_t layout) {
    sys_config.keyboard_layout = layout;
    config_save();
}

uint8_t config_get_keyboard_layout(void) {
    return sys_config.keyboard_layout;
}

void config_get_datetime(datetime_t* dt) {
    if (dt) {
        memcpy(dt, &sys_config.datetime, sizeof(datetime_t));
    }
}

void config_set_datetime(const datetime_t* dt) {
    if (dt) {
        memcpy(&sys_config.datetime, dt, sizeof(datetime_t));
        config_save();
    }
}

void config_update_uptime(void) {
    sys_config.uptime_seconds++;
}

void config_tick_second(void) {
    sys_config.uptime_seconds++;
    
    sys_config.datetime.second++;
    if (sys_config.datetime.second >= 60) {
        sys_config.datetime.second = 0;
        sys_config.datetime.minute++;
        if (sys_config.datetime.minute >= 60) {
            sys_config.datetime.minute = 0;
            sys_config.datetime.hour++;
            if (sys_config.datetime.hour >= 24) {
                sys_config.datetime.hour = 0;
                sys_config.datetime.day++;
                if (sys_config.datetime.day > 30) {
                    sys_config.datetime.day = 1;
                    sys_config.datetime.month++;
                    if (sys_config.datetime.month > 12) {
                        sys_config.datetime.month = 1;
                        sys_config.datetime.year++;
                    }
                }
            }
        }
    }
}

const char* config_get_timezone(void) {
    return sys_config.timezone;
}

void config_set_timezone(const char* tz) {
    if (tz) {
        size_t len = strlen(tz);
        if (len >= sizeof(sys_config.timezone)) {
            len = sizeof(sys_config.timezone) - 1;
        }
        memcpy(sys_config.timezone, tz, len);
        sys_config.timezone[len] = '\0';
        config_save();
    }
}

int config_save_history(void) {
    /* Elevate to root for system file writes */
    const char *prev_user = user_get_current();
    char saved_user[64] = {0};
    if (prev_user) strncpy(saved_user, prev_user, 63);
    user_set_current("root");

    /* Create /etc directory if it doesn't exist */
    fs_create_file("/etc", 1);

    /* Create history file if doesn't exist */
    fs_create_file(HISTORY_FILE, 0);

    int count = shell_history_count();
    if (count == 0) {
        if (saved_user[0]) user_set_current(saved_user);
        return 0;
    }

    /* Create a buffer to hold all history entries */
    uint8_t buffer[SHELL_HIST_SIZE * SHELL_CMD_SIZE];
    size_t offset = 0;

    for (int i = 0; i < count; i++) {
        const char* entry = shell_history_entry(i);
        if (entry) {
            size_t len = strlen(entry);
            if (offset + len + 1 < sizeof(buffer)) {
                memcpy(buffer + offset, entry, len);
                offset += len;
                buffer[offset++] = '\n';
            }
        }
    }

    if (offset == 0) {
        if (saved_user[0]) user_set_current(saved_user);
        return 0;
    }

    /* Write history to file */
    int ret = fs_write_file(HISTORY_FILE, buffer, offset);

    /* Restore original user */
    if (saved_user[0]) user_set_current(saved_user);

    if (ret != 0) return -1;

    fs_sync();
    return 0;
}

int config_load_history(void) {
    uint8_t buffer[SHELL_HIST_SIZE * SHELL_CMD_SIZE];
    size_t size;

    if (fs_read_file(HISTORY_FILE, buffer, &size) != 0) {
        return -1;
    }

    /* Parse history file line by line */
    size_t start = 0;
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] == '\n' || i == size - 1) {
            size_t len = i - start;
            if (i == size - 1 && buffer[i] != '\n') {
                len++;
            }

            if (len > 0 && len < SHELL_CMD_SIZE) {
                char cmd[SHELL_CMD_SIZE];
                memcpy(cmd, buffer + start, len);
                cmd[len] = '\0';
                shell_history_add(cmd);
            }

            start = i + 1;
        }
    }

    return 0;
}
