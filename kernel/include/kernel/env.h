#ifndef _KERNEL_ENV_H
#define _KERNEL_ENV_H

#include <stddef.h>

#define MAX_ENV_VARS 64
#define MAX_ENV_NAME 64
#define MAX_ENV_VALUE 256

/* Environment variable structure */
typedef struct {
    char name[MAX_ENV_NAME];
    char value[MAX_ENV_VALUE];
    int active;
} env_var_t;

/* Initialize environment */
void env_initialize(void);

/* Get/Set environment variables */
const char* env_get(const char* name);
int env_set(const char* name, const char* value);
int env_unset(const char* name);

/* List all environment variables */
void env_list(void);

/* Expand variables in a string ($VAR or ${VAR}) */
int env_expand(const char* input, char* output, size_t output_size);

#endif
