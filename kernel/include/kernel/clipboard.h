#ifndef _KERNEL_CLIPBOARD_H
#define _KERNEL_CLIPBOARD_H

#include <stddef.h>

#define CLIPBOARD_MAX 4096

/* Copy text to clipboard */
void clipboard_copy(const char *text, size_t len);

/* Get clipboard contents (returns pointer to internal buffer, sets *len) */
const char *clipboard_get(size_t *len);

/* Check if clipboard has content */
int clipboard_has_content(void);

/* Clear clipboard */
void clipboard_clear(void);

#endif
