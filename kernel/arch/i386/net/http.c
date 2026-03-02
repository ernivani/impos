#include <kernel/http.h>
#include <kernel/dns.h>
#include <kernel/tcp.h>
#include <kernel/net.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── URL Parser ──────────────────────────────────────────────────── */

int http_parse_url(const char *url, char *host, size_t host_len,
                   uint16_t *port, char *path, size_t path_len) {
    if (!url || !host || !port || !path) return -1;

    /* Skip "http://" prefix */
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;

    /* Extract host (and optional :port) */
    const char *host_start = p;
    const char *host_end = p;
    *port = 80;

    while (*host_end && *host_end != '/' && *host_end != ':')
        host_end++;

    size_t hlen = (size_t)(host_end - host_start);
    if (hlen == 0 || hlen >= host_len) return -1;
    memcpy(host, host_start, hlen);
    host[hlen] = '\0';

    if (*host_end == ':') {
        host_end++;
        *port = 0;
        while (*host_end >= '0' && *host_end <= '9') {
            *port = *port * 10 + (*host_end - '0');
            host_end++;
        }
        if (*port == 0) *port = 80;
    }

    /* Remaining is the path */
    if (*host_end == '/') {
        size_t plen = strlen(host_end);
        if (plen >= path_len) plen = path_len - 1;
        memcpy(path, host_end, plen);
        path[plen] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }

    return 0;
}

/* ── HTTP GET ────────────────────────────────────────────────────── */

int http_get(const char *url, http_response_t *resp) {
    if (!url || !resp) return -1;

    memset(resp, 0, sizeof(*resp));

    char host[128];
    char path[256];
    uint16_t port;

    if (http_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0)
        return -1;

    /* DNS resolve */
    uint8_t ip[4];
    if (dns_resolve(host, ip) < 0) {
        DBG("[HTTP] DNS resolve failed for '%s'", host);
        return -2;
    }

    /* TCP connect */
    int tcb = tcp_open(0, 0);  /* ephemeral port, active open */
    if (tcb < 0) {
        DBG("[HTTP] Failed to allocate TCP connection");
        return -3;
    }

    if (tcp_connect(tcb, ip, port) < 0) {
        DBG("[HTTP] TCP connect failed to %d.%d.%d.%d:%d",
            ip[0], ip[1], ip[2], ip[3], port);
        tcp_close(tcb);
        return -4;
    }

    /* Build and send HTTP/1.0 request */
    char req[512];
    int rlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: ImposOS/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    if (tcp_send(tcb, (const uint8_t *)req, (size_t)rlen) < 0) {
        DBG("[HTTP] Failed to send request");
        tcp_close(tcb);
        return -5;
    }

    /* Read response into a growing buffer */
    uint32_t buf_cap = 4096;
    uint32_t buf_len = 0;
    char *buf = malloc(buf_cap);
    if (!buf) {
        tcp_close(tcb);
        return -6;
    }

    while (1) {
        if (buf_len + 1024 > buf_cap) {
            buf_cap *= 2;
            if (buf_cap > 1024 * 1024) break;  /* 1MB limit */
            char *newbuf = realloc(buf, buf_cap);
            if (!newbuf) break;
            buf = newbuf;
        }

        int n = tcp_recv(tcb, (uint8_t *)(buf + buf_len),
                         buf_cap - buf_len, 5000);
        if (n <= 0) break;  /* EOF or timeout */
        buf_len += (uint32_t)n;
    }

    tcp_close(tcb);

    if (buf_len == 0) {
        free(buf);
        return -7;
    }

    /* Parse status line: "HTTP/1.x NNN reason\r\n" */
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) {
        free(buf);
        return -8;
    }

    /* Find status code */
    char *sp = strchr(buf, ' ');
    if (sp && sp < line_end) {
        resp->status_code = atoi(sp + 1);
    }

    /* Find header/body separator */
    char *body_sep = strstr(buf, "\r\n\r\n");
    if (!body_sep) {
        /* No body separator found — treat everything as body */
        resp->body = buf;
        resp->body_len = buf_len;
        return 0;
    }

    /* Parse headers for Content-Type */
    char *hdr_start = line_end + 2;
    char *hdr_end = body_sep;

    char *ct = hdr_start;
    while (ct < hdr_end) {
        char *nl = strstr(ct, "\r\n");
        if (!nl) break;

        if (strncmp(ct, "Content-Type:", 13) == 0 ||
            strncmp(ct, "content-type:", 13) == 0) {
            const char *val = ct + 13;
            while (*val == ' ') val++;
            size_t vlen = (size_t)(nl - val);
            if (vlen >= sizeof(resp->content_type))
                vlen = sizeof(resp->content_type) - 1;
            memcpy(resp->content_type, val, vlen);
            resp->content_type[vlen] = '\0';
        }
        ct = nl + 2;
    }

    /* Extract body */
    char *body_start = body_sep + 4;
    resp->body_len = buf_len - (uint32_t)(body_start - buf);

    /* Move body to start of buffer to save memory */
    memmove(buf, body_start, resp->body_len);
    buf[resp->body_len] = '\0';
    resp->body = buf;

    return 0;
}

void http_response_free(http_response_t *resp) {
    if (resp && resp->body) {
        free(resp->body);
        resp->body = NULL;
        resp->body_len = 0;
    }
}
