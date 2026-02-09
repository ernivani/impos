#ifndef _STRING_H
#define _STRING_H 1

#include <sys/cdefs.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcmp(const void*, const void*, size_t);
void* memcpy(void* __restrict, const void* __restrict, size_t);
void* memmove(void*, const void*, size_t);
void* memset(void*, int, size_t);
void* memchr(const void* s, int c, size_t n);
size_t strlen(const char*);
size_t strnlen(const char* s, size_t maxlen);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strtok(char* str, const char* delim);
char* strchr(const char* str, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
size_t strcspn(const char* s, const char* reject);
size_t strspn(const char* s, const char* accept);
char* strpbrk(const char* s, const char* accept);

#ifdef __cplusplus
}
#endif

#endif