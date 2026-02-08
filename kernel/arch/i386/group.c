#include <kernel/group.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>

static group_t groups[MAX_GROUPS];
static int groups_initialized = 0;

void group_initialize(void) {
    if (groups_initialized) return;

    for (int i = 0; i < MAX_GROUPS; i++) {
        groups[i].active = 0;
    }

    groups_initialized = 1;

    /* Try to load from disk */
    if (group_load() == 0) return;

    /* Create default root group */
    group_create("root", 0);
    group_add_member(0, "root");
}

int group_load(void) {
    uint8_t buffer[4096];
    size_t len = sizeof(buffer);

    if (fs_read_file("/etc/group", buffer, &len) != 0) {
        return -1;
    }

    /* Parse: name:gid:member1,member2,...\n */
    char* line = (char*)buffer;
    char* end = (char*)buffer + len;
    int count = 0;

    while (line < end && *line && count < MAX_GROUPS) {
        char* line_end = line;
        while (line_end < end && *line_end && *line_end != '\n') line_end++;
        if (line_end > line) {
            *line_end = '\0';

            char name[MAX_GROUP_NAME] = {0};
            uint16_t gid = 0;
            char* p = line;
            int field = 0;
            char* field_start = p;

            while (*p) {
                if (*p == ':') {
                    *p = '\0';
                    if (field == 0) {
                        strncpy(name, field_start, MAX_GROUP_NAME - 1);
                    } else if (field == 1) {
                        for (char* d = field_start; *d >= '0' && *d <= '9'; d++)
                            gid = gid * 10 + (*d - '0');
                    }
                    field++;
                    field_start = p + 1;
                }
                p++;
            }

            /* Last field: members */
            if (name[0] && field >= 2) {
                int gi = -1;
                for (int i = 0; i < MAX_GROUPS; i++) {
                    if (!groups[i].active) { gi = i; break; }
                }
                if (gi < 0) break;

                groups[gi].active = 1;
                groups[gi].gid = gid;
                strncpy(groups[gi].name, name, MAX_GROUP_NAME - 1);
                groups[gi].name[MAX_GROUP_NAME - 1] = '\0';
                groups[gi].num_members = 0;

                /* Parse comma-separated members */
                char* m = field_start;
                while (*m && groups[gi].num_members < MAX_MEMBERS) {
                    char* mend = m;
                    while (*mend && *mend != ',') mend++;
                    if (mend > m) {
                        size_t mlen = (size_t)(mend - m);
                        if (mlen >= MAX_GROUP_NAME) mlen = MAX_GROUP_NAME - 1;
                        strncpy(groups[gi].members[groups[gi].num_members], m, mlen);
                        groups[gi].members[groups[gi].num_members][mlen] = '\0';
                        groups[gi].num_members++;
                    }
                    m = *mend ? mend + 1 : mend;
                }
                count++;
            }
        }
        line = line_end + 1;
    }

    return count > 0 ? 0 : -1;
}

int group_save(void) {
    char buffer[4096];
    size_t pos = 0;

    for (int i = 0; i < MAX_GROUPS && pos < sizeof(buffer) - 256; i++) {
        if (!groups[i].active) continue;

        int written = snprintf(buffer + pos, sizeof(buffer) - pos,
                               "%s:%d:", groups[i].name, groups[i].gid);
        pos += written;

        for (int m = 0; m < groups[i].num_members; m++) {
            if (m > 0 && pos < sizeof(buffer) - 1) buffer[pos++] = ',';
            written = snprintf(buffer + pos, sizeof(buffer) - pos, "%s",
                               groups[i].members[m]);
            pos += written;
        }

        if (pos < sizeof(buffer) - 1) buffer[pos++] = '\n';
    }

    fs_create_file("/etc", 1);
    fs_create_file("/etc/group", 0);
    return fs_write_file("/etc/group", (uint8_t*)buffer, pos);
}

int group_create(const char* name, uint16_t gid) {
    /* Check if exists */
    if (group_get_by_name(name)) return -1;
    if (group_get_by_gid(gid)) return -1;

    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!groups[i].active) {
            groups[i].active = 1;
            groups[i].gid = gid;
            strncpy(groups[i].name, name, MAX_GROUP_NAME - 1);
            groups[i].name[MAX_GROUP_NAME - 1] = '\0';
            groups[i].num_members = 0;
            return 0;
        }
    }
    return -1;
}

int group_delete(uint16_t gid) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].active && groups[i].gid == gid) {
            groups[i].active = 0;
            return 0;
        }
    }
    return -1;
}

group_t* group_get_by_gid(uint16_t gid) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].active && groups[i].gid == gid) return &groups[i];
    }
    return NULL;
}

group_t* group_get_by_name(const char* name) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups[i].active && strcmp(groups[i].name, name) == 0) return &groups[i];
    }
    return NULL;
}

group_t* group_get_by_index(int index) {
    if (index < 0 || index >= MAX_GROUPS) return NULL;
    if (!groups[index].active) return NULL;
    return &groups[index];
}

int group_add_member(uint16_t gid, const char* username) {
    group_t* g = group_get_by_gid(gid);
    if (!g) return -1;

    /* Check if already member */
    for (int i = 0; i < g->num_members; i++) {
        if (strcmp(g->members[i], username) == 0) return 0;
    }

    if (g->num_members >= MAX_MEMBERS) return -1;

    strncpy(g->members[g->num_members], username, MAX_GROUP_NAME - 1);
    g->members[g->num_members][MAX_GROUP_NAME - 1] = '\0';
    g->num_members++;
    return 0;
}

int group_remove_member(uint16_t gid, const char* username) {
    group_t* g = group_get_by_gid(gid);
    if (!g) return -1;

    for (int i = 0; i < g->num_members; i++) {
        if (strcmp(g->members[i], username) == 0) {
            /* Shift remaining members */
            for (int j = i; j < g->num_members - 1; j++) {
                strcpy(g->members[j], g->members[j + 1]);
            }
            g->num_members--;
            return 0;
        }
    }
    return -1;
}

int group_is_member(uint16_t gid, const char* username) {
    group_t* g = group_get_by_gid(gid);
    if (!g) return 0;

    for (int i = 0; i < g->num_members; i++) {
        if (strcmp(g->members[i], username) == 0) return 1;
    }
    return 0;
}
