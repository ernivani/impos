/* doom_compat.h — ImposOS compatibility stubs for doomgeneric */
#ifndef DOOM_COMPAT_H
#define DOOM_COMPAT_H

#include <stddef.h>

/* Functions doom source expects but ImposOS doesn't provide.
   Implemented in doom_compat.c */
char *getenv(const char *name);
int remove(const char *path);
int rename(const char *old_name, const char *new_name);
double atof(const char *str);

/* vfprintf: doom's I_Error uses this on stderr */
#include <stdio.h>
#include <stdarg.h>
int vfprintf(FILE *f, const char *fmt, va_list ap);

#endif /* DOOM_COMPAT_H */
