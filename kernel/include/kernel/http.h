#ifndef _KERNEL_HTTP_H
#define _KERNEL_HTTP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int      status_code;       /* 200, 404, etc. */
    char    *body;              /* malloc'd response body */
    uint32_t body_len;
    char     content_type[64];
} http_response_t;

/* Perform an HTTP GET request. Returns 0 on success, <0 on error.
 * Caller must call http_response_free() when done. */
int http_get(const char *url, http_response_t *resp);

/* Parse a URL into host, port, and path components.
 * Returns 0 on success, -1 on malformed URL. */
int http_parse_url(const char *url, char *host, size_t host_len,
                   uint16_t *port, char *path, size_t path_len);

/* Free resources allocated by http_get(). */
void http_response_free(http_response_t *resp);

#endif
