#ifndef _KERNEL_SHELL_H
#define _KERNEL_SHELL_H

#include <stddef.h>

void shell_initialize(void);
void shell_process_command(char* command);
size_t shell_autocomplete(char* buffer, size_t buffer_pos, size_t buffer_size);

#endif