/* Stub <assert.h> for ImposOS doom port */
#ifndef _DOOM_ASSERT_H
#define _DOOM_ASSERT_H

#include <stdio.h>

#define assert(expr) \
    do { \
        if (!(expr)) { \
            printf("ASSERT FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        } \
    } while (0)

#endif
