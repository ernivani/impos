/* Stub <sys/stat.h> for ImposOS doom port */
#ifndef _DOOM_SYS_STAT_H
#define _DOOM_SYS_STAT_H

#include <stdint.h>

typedef uint32_t mode_t;

static inline int mkdir(const char *path, mode_t mode) {
    (void)path; (void)mode;
    return -1;
}

#endif
