#include <string.h>

size_t strspn(const char* s, const char* accept) {
    size_t count = 0;
    while (*s) {
        const char* a = accept;
        int found = 0;
        while (*a) {
            if (*s == *a) {
                found = 1;
                break;
            }
            a++;
        }
        if (!found)
            return count;
        s++;
        count++;
    }
    return count;
}
