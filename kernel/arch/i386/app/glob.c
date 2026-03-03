#include <kernel/glob.h>
#include <kernel/fs.h>
#include <string.h>
#include <stdio.h>

/* ═══ Glob pattern matching ════════════════════════════════════ */

int glob_match(const char *pattern, const char *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            /* * at end matches everything */
            if (!*pattern) return 1;
            /* Try matching rest of pattern at each position */
            while (*str) {
                if (glob_match(pattern, str))
                    return 1;
                str++;
            }
            return glob_match(pattern, str);
        }
        if (*pattern == '?') {
            /* ? matches exactly one char */
            pattern++;
            str++;
            continue;
        }
        if (*pattern == '[') {
            /* Character class [abc] or [a-z] */
            pattern++; /* skip [ */
            int negated = 0;
            if (*pattern == '!' || *pattern == '^') {
                negated = 1;
                pattern++;
            }
            int matched = 0;
            char prev = 0;
            while (*pattern && *pattern != ']') {
                if (*pattern == '-' && prev && pattern[1] && pattern[1] != ']') {
                    /* Range: a-z */
                    pattern++; /* skip - */
                    if (*str >= prev && *str <= *pattern)
                        matched = 1;
                    prev = *pattern++;
                } else {
                    if (*pattern == *str)
                        matched = 1;
                    prev = *pattern++;
                }
            }
            if (*pattern == ']') pattern++;
            if (negated) matched = !matched;
            if (!matched) return 0;
            str++;
            continue;
        }
        /* Literal character */
        if (*pattern != *str)
            return 0;
        pattern++;
        str++;
    }
    /* Handle trailing *'s */
    while (*pattern == '*')
        pattern++;
    return (*pattern == '\0' && *str == '\0');
}

/* ═══ Glob expansion ══════════════════════════════════════════ */

int glob_expand(const char *pattern, char matches[][256], int max_matches) {
    if (!pattern || !matches || max_matches <= 0)
        return 0;

    /* Split pattern into directory prefix and filename pattern */
    char dir_path[256] = "";
    char file_pattern[256];
    const char *last_slash = NULL;

    /* Find the last / before any glob chars */
    for (const char *p = pattern; *p; p++) {
        if (*p == '/') last_slash = p;
        if (*p == '*' || *p == '?' || *p == '[') break;
    }

    if (last_slash) {
        int dir_len = last_slash - pattern + 1;
        if (dir_len > 255) dir_len = 255;
        memcpy(dir_path, pattern, dir_len);
        dir_path[dir_len] = '\0';
        strncpy(file_pattern, last_slash + 1, 255);
        file_pattern[255] = '\0';
    } else {
        dir_path[0] = '\0';
        strncpy(file_pattern, pattern, 255);
        file_pattern[255] = '\0';
    }

    /* If pattern is just in CWD (no directory prefix or relative),
     * we can use fs_enumerate_directory which works on CWD.
     * For other directories, we need to change CWD temporarily. */

    int count = 0;
    int saved_cwd = -1;
    uint32_t saved_cwd_inode = 0;

    if (dir_path[0]) {
        /* Save CWD and change to target directory */
        saved_cwd_inode = fs_get_cwd_inode();
        saved_cwd = 1;
        if (fs_change_directory(dir_path) < 0)
            return 0; /* directory doesn't exist */
    }

    /* Enumerate directory entries */
    fs_dir_entry_info_t entries[128];
    int n = fs_enumerate_directory(entries, 128, 0);

    for (int i = 0; i < n && count < max_matches; i++) {
        if (glob_match(file_pattern, entries[i].name)) {
            if (dir_path[0]) {
                snprintf(matches[count], 256, "%s%s", dir_path, entries[i].name);
            } else {
                strncpy(matches[count], entries[i].name, 255);
                matches[count][255] = '\0';
            }
            count++;
        }
    }

    /* Restore CWD */
    if (saved_cwd > 0) {
        fs_change_directory_by_inode(saved_cwd_inode);
    }

    /* Sort matches alphabetically (simple insertion sort) */
    for (int i = 1; i < count; i++) {
        char tmp[256];
        memcpy(tmp, matches[i], 256);
        int j = i - 1;
        while (j >= 0 && strcmp(matches[j], tmp) > 0) {
            memcpy(matches[j + 1], matches[j], 256);
            j--;
        }
        memcpy(matches[j + 1], tmp, 256);
    }

    return count;
}
