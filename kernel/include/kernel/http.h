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

/* Perform an HTTP/HTTPS GET request (detects scheme from URL).
 * Follows redirects (up to 5). Returns 0 on success, <0 on error.
 * Caller must call http_response_free() when done. */
int http_get(const char *url, http_response_t *resp);

/* Parse a URL into host, port, path, and scheme components.
 * Sets *is_https=1 for https:// URLs, 0 otherwise.
 * Returns 0 on success, -1 on malformed URL. */
int http_parse_url(const char *url, char *host, size_t host_len,
                   uint16_t *port, char *path, size_t path_len,
                   int *is_https);

/* Enable/disable verbose diagnostic output (curl-style). */
void http_set_verbose(int verbose);

/* Free resources allocated by http_get(). */
void http_response_free(http_response_t *resp);

#endif
