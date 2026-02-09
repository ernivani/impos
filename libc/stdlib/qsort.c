#include <stdlib.h>
#include <string.h>

/* Insertion sort — no extra allocation needed, kernel-safe */
void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    if (nmemb < 2 || size == 0)
        return;

    char swap[256];
    unsigned char* arr = (unsigned char*)base;

    for (size_t i = 1; i < nmemb; i++) {
        /* Save element i */
        memcpy(swap, arr + i * size, size);

        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, swap) > 0) {
            memcpy(arr + j * size, arr + (j - 1) * size, size);
            j--;
        }
        memcpy(arr + j * size, swap, size);
    }
}
