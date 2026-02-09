#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void* calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0)
        return NULL;

    /* Overflow check */
    size_t total = nmemb * size;
    if (total / nmemb != size)
        return NULL;

    void* ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}
