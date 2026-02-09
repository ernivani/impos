#include <stdlib.h>

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*)) {
    const unsigned char* arr = (const unsigned char*)base;
    size_t lo = 0, hi = nmemb;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = compar(key, arr + mid * size);
        if (cmp == 0)
            return (void*)(arr + mid * size);
        else if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}
