#include <string.h>



char* strtok(char* str, const char* delim) {
    static char* last;
    
    if (str) {
        last = str;
    } else if (!last) {
        return NULL;
    }
    
    // Skip leading delimiters
    while (*last && strchr(delim, *last)) {
        last++;
    }
    
    if (!*last) {
        last = NULL;
        return NULL;
    }
    
    char* token = last;
    
    // Find end of token
    while (*last && !strchr(delim, *last)) {
        last++;
    }
    
    if (*last) {
        *last++ = '\0';
    } else {
        last = NULL;
    }
    
    return token;
}