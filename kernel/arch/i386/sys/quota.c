#include <kernel/quota.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static quota_entry_t quotas[MAX_QUOTAS];

void quota_initialize(void) {
    memset(quotas, 0, sizeof(quotas));
    quota_load();
}

int quota_set(uint16_t uid, uint16_t max_inodes, uint16_t max_blocks) {
    /* Update existing */
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            quotas[i].max_inodes = max_inodes;
            quotas[i].max_blocks = max_blocks;
            quota_save();
            return 0;
        }
    }
    /* New entry */
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (!quotas[i].active) {
            quotas[i].uid = uid;
            quotas[i].max_inodes = max_inodes;
            quotas[i].max_blocks = max_blocks;
            quotas[i].used_inodes = 0;
            quotas[i].used_blocks = 0;
            quotas[i].active = 1;
            quota_save();
            return 0;
        }
    }
    return -1;
}

int quota_check_inode(uint16_t uid) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            if (quotas[i].max_inodes > 0 &&
                quotas[i].used_inodes >= quotas[i].max_inodes)
                return -1; /* quota exceeded */
            return 0;
        }
    }
    return 0; /* no quota = unlimited */
}

int quota_check_block(uint16_t uid, uint16_t blocks_needed) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            if (quotas[i].max_blocks > 0 &&
                quotas[i].used_blocks + blocks_needed > quotas[i].max_blocks)
                return -1;
            return 0;
        }
    }
    return 0;
}

void quota_add_inode(uint16_t uid) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            quotas[i].used_inodes++;
            return;
        }
    }
}

void quota_remove_inode(uint16_t uid) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            if (quotas[i].used_inodes > 0) quotas[i].used_inodes--;
            return;
        }
    }
}

void quota_add_blocks(uint16_t uid, uint16_t count) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            quotas[i].used_blocks += count;
            return;
        }
    }
}

void quota_remove_blocks(uint16_t uid, uint16_t count) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid) {
            if (quotas[i].used_blocks >= count)
                quotas[i].used_blocks -= count;
            else
                quotas[i].used_blocks = 0;
            return;
        }
    }
}

quota_entry_t* quota_get(uint16_t uid) {
    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (quotas[i].active && quotas[i].uid == uid)
            return &quotas[i];
    }
    return NULL;
}

void quota_save(void) {
    /* Serialize to /etc/quota as text lines: uid max_inodes max_blocks used_inodes used_blocks */
    char buf[1024];
    size_t pos = 0;

    for (int i = 0; i < MAX_QUOTAS; i++) {
        if (!quotas[i].active) continue;
        int n = snprintf(buf + pos, sizeof(buf) - pos, "%d %d %d %d %d\n",
                         quotas[i].uid, quotas[i].max_inodes,
                         quotas[i].max_blocks, quotas[i].used_inodes,
                         quotas[i].used_blocks);
        if (n > 0) pos += n;
    }

    /* Save cwd, cd to /etc, write, restore */
    uint32_t saved_cwd = fs_get_cwd_inode();
    fs_change_directory("/etc");
    fs_write_file("quota", (const uint8_t*)buf, pos);
    fs_change_directory_by_inode(saved_cwd);
}

void quota_load(void) {
    uint32_t saved_cwd = fs_get_cwd_inode();
    fs_change_directory("/etc");

    uint8_t buf[1024];
    size_t size;
    if (fs_read_file("quota", buf, &size) != 0) {
        fs_change_directory_by_inode(saved_cwd);
        return;
    }
    fs_change_directory_by_inode(saved_cwd);

    buf[size < sizeof(buf) ? size : sizeof(buf) - 1] = '\0';

    char* line = (char*)buf;
    int idx = 0;
    while (*line && idx < MAX_QUOTAS) {
        int uid, mi, mb, ui, ub;
        int fields = 0;
        uid = mi = mb = ui = ub = 0;

        /* Simple integer parsing */
        while (*line == ' ' || *line == '\n') line++;
        if (!*line) break;

        /* Parse 5 integers */
        char* p = line;
        uid = atoi(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
        mi  = atoi(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
        mb  = atoi(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
        ui  = atoi(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
        ub  = atoi(p);
        fields = 5;

        /* Skip to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
        line = p;

        if (fields == 5) {
            quotas[idx].uid = uid;
            quotas[idx].max_inodes = mi;
            quotas[idx].max_blocks = mb;
            quotas[idx].used_inodes = ui;
            quotas[idx].used_blocks = ub;
            quotas[idx].active = 1;
            idx++;
        }
    }
}
