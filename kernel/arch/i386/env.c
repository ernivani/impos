#include <kernel/env.h>
#include <string.h>
#include <stdio.h>

static env_var_t env_vars[MAX_ENV_VARS];
static int env_initialized = 0;

void env_initialize(void) {
    if (env_initialized) {
        return;
    }
    
    /* Clear all variables */
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        env_vars[i].active = 0;
        env_vars[i].name[0] = '\0';
        env_vars[i].value[0] = '\0';
    }
    
    /* Set default environment variables */
    env_set("USER", "root");
    env_set("HOME", "/home/root");
    env_set("PATH", "/bin:/usr/bin");
    env_set("PS1", "$ ");
    env_set("SHELL", "/bin/sh");
    env_set("TERM", "impos");
    
    env_initialized = 1;
}

const char* env_get(const char* name) {
    if (!name || name[0] == '\0') {
        return NULL;
    }
    
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        if (env_vars[i].active && strcmp(env_vars[i].name, name) == 0) {
            return env_vars[i].value;
        }
    }
    
    return NULL;
}

int env_set(const char* name, const char* value) {
    if (!name || name[0] == '\0') {
        return -1;
    }
    
    /* Check if variable already exists */
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        if (env_vars[i].active && strcmp(env_vars[i].name, name) == 0) {
            /* Update existing variable */
            strncpy(env_vars[i].value, value, MAX_ENV_VALUE - 1);
            env_vars[i].value[MAX_ENV_VALUE - 1] = '\0';
            return 0;
        }
    }
    
    /* Find free slot */
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        if (!env_vars[i].active) {
            env_vars[i].active = 1;
            strncpy(env_vars[i].name, name, MAX_ENV_NAME - 1);
            env_vars[i].name[MAX_ENV_NAME - 1] = '\0';
            strncpy(env_vars[i].value, value, MAX_ENV_VALUE - 1);
            env_vars[i].value[MAX_ENV_VALUE - 1] = '\0';
            return 0;
        }
    }
    
    return -1;  /* No free slots */
}

int env_unset(const char* name) {
    if (!name || name[0] == '\0') {
        return -1;
    }
    
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        if (env_vars[i].active && strcmp(env_vars[i].name, name) == 0) {
            env_vars[i].active = 0;
            return 0;
        }
    }
    
    return -1;  /* Not found */
}

void env_list(void) {
    int count = 0;
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        if (env_vars[i].active) {
            printf("%s=%s\n", env_vars[i].name, env_vars[i].value);
            count++;
        }
    }
    if (count == 0) {
        printf("No environment variables set\n");
    }
}

int env_expand(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return -1;
    }
    
    size_t out_pos = 0;
    const char* p = input;
    
    while (*p && out_pos < output_size - 1) {
        if (*p == '$') {
            p++;  /* Skip $ */
            
            /* Handle ${VAR} or $VAR */
            int use_braces = 0;
            if (*p == '{') {
                use_braces = 1;
                p++;
            }
            
            /* Extract variable name */
            char var_name[MAX_ENV_NAME];
            int name_pos = 0;
            
            while (*p && name_pos < MAX_ENV_NAME - 1) {
                if (use_braces && *p == '}') {
                    p++;
                    break;
                }
                if (!use_braces && (*p == ' ' || *p == '/' || *p == ':' || *p == '$')) {
                    break;
                }
                var_name[name_pos++] = *p++;
            }
            var_name[name_pos] = '\0';
            
            /* Get variable value */
            const char* value = env_get(var_name);
            if (value) {
                /* Copy value to output */
                while (*value && out_pos < output_size - 1) {
                    output[out_pos++] = *value++;
                }
            }
        } else {
            output[out_pos++] = *p++;
        }
    }
    
    output[out_pos] = '\0';
    return 0;
}
