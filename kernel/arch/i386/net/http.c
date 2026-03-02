#include <kernel/http.h>
#include <kernel/dns.h>
#include <kernel/socket.h>
#include <kernel/tls.h>
#include <kernel/net.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_MAX_REDIRECTS 5

/* ── Verbose Mode ───────────────────────────────────────────────── */

static int http_verbose = 0;

void http_set_verbose(int v) { http_verbose = v; }

#define VPRINT(...) do { if (http_verbose) printf(__VA_ARGS__); } while(0)

/* ── TLS cipher name helper ─────────────────────────────────────── */

static const char *tls_cipher_name(uint16_t suite) {
    switch (suite) {
        case 0xC02B: return "ECDHE-ECDSA-AES128-GCM-SHA256";
        case 0xC027: return "ECDHE-RSA-AES128-CBC-SHA256";
        case 0x009C: return "RSA-AES128-GCM-SHA256";
        case 0x003C: return "RSA-AES128-CBC-SHA256";
        default:     return "unknown";
    }
}

/* ── URL Parser ──────────────────────────────────────────────────── */

int http_parse_url(const char *url, char *host, size_t host_len,
                   uint16_t *port, char *path, size_t path_len,
                   int *is_https) {
    if (!url || !host || !port || !path || !is_https) return -1;

    /* Detect scheme */
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        *is_https = 1;
        *port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        *is_https = 0;
        *port = 80;
        p += 7;
    } else {
        *is_https = 0;
        *port = 80;
    }

    /* Extract host (and optional :port) */
    const char *host_start = p;
    const char *host_end = p;

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
        if (*port == 0) *port = *is_https ? 443 : 80;
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

/* ── Case-insensitive header search ─────────────────────────────── */

/* Find a header value in the raw header block (between first \r\n and \r\n\r\n).
 * Returns pointer to value start (after ": "), or NULL. */
static const char *http_find_header(const char *headers, size_t hdr_len,
                                    const char *name) {
    size_t name_len = strlen(name);
    const char *p = headers;
    const char *end = headers + hdr_len;

    while (p < end) {
        const char *nl = strstr(p, "\r\n");
        if (!nl || nl > end) break;

        /* Case-insensitive prefix match: "Name:" */
        size_t line_len = (size_t)(nl - p);
        if (line_len > name_len && p[name_len] == ':') {
            int match = 1;
            for (size_t i = 0; i < name_len; i++) {
                char a = p[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (match) {
                const char *val = p + name_len + 1;
                while (*val == ' ') val++;
                return val;
            }
        }
        p = nl + 2;
    }
    return NULL;
}

/* ── HTTP GET ────────────────────────────────────────────────────── */

int http_get(const char *url, http_response_t *resp) {
    if (!url || !resp) return -1;

    /* Working copy of URL for redirect loop — heap-allocated to save stack */
    char *cur_url = malloc(512);
    if (!cur_url) return -6;

    size_t ulen = strlen(url);
    if (ulen >= 512) { free(cur_url); return -1; }
    memcpy(cur_url, url, ulen + 1);

    int rc = -10;  /* default: too many redirects (if loop exhausts) */

    for (int attempt = 0; attempt <= HTTP_MAX_REDIRECTS; attempt++) {
        memset(resp, 0, sizeof(*resp));

        char host[128];
        char path[256];
        uint16_t port;
        int is_https;

        if (http_parse_url(cur_url, host, sizeof(host), &port, path, sizeof(path), &is_https) < 0) {
            rc = -1;
            break;
        }

        /* DNS resolve */
        VPRINT("* Resolving %s...\n", host);
        uint8_t ip[4];
        if (dns_resolve(host, ip) < 0) {
            VPRINT("* DNS resolve failed\n");
            DBG("[HTTP] DNS resolve failed for '%s'", host);
            rc = -2;
            break;
        }
        VPRINT("* Resolved to %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

        /* TCP connect */
        VPRINT("* Connecting to %d.%d.%d.%d:%d...\n", ip[0], ip[1], ip[2], ip[3], port);
        int sock = socket_create(SOCK_STREAM);
        if (sock < 0) {
            DBG("[HTTP] Failed to create socket");
            rc = -3;
            break;
        }

        if (socket_connect(sock, ip, port) < 0) {
            VPRINT("* Connection failed\n");
            DBG("[HTTP] TCP connect failed to %d.%d.%d.%d:%d",
                ip[0], ip[1], ip[2], ip[3], port);
            socket_close(sock);
            rc = -4;
            break;
        }
        VPRINT("* Connected\n");

        /* TLS handshake if HTTPS */
        tls_conn_t *tls = NULL;
        if (is_https) {
            VPRINT("* TLS handshake with %s...\n", host);
            tls = malloc(sizeof(tls_conn_t));
            if (!tls) {
                socket_close(sock);
                rc = -5;
                break;
            }
            int tls_rc = tls_connect(tls, sock, host);
            if (tls_rc < 0) {
                static const char *tls_step[] = {
                    "ClientHello send", "ServerHello/Cert recv",
                    "ClientFinish send", "ServerFinish recv"
                };
                int step = (-tls_rc) - 1;
                if (step >= 0 && step < 4)
                    VPRINT("* TLS handshake failed at: %s\n", tls_step[step]);
                else
                    VPRINT("* TLS handshake failed (code %d)\n", tls_rc);
                DBG("[HTTP] TLS handshake failed (step %d)", -tls_rc);
                free(tls);
                socket_close(sock);
                rc = -5;
                break;
            }
            VPRINT("* TLS 1.2, cipher: %s\n", tls_cipher_name(tls->cipher_suite));
            VPRINT("* TLS handshake complete\n");
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

        if (http_verbose) {
            VPRINT("> GET %s HTTP/1.0\n", path);
            VPRINT("> Host: %s\n", host);
            VPRINT("> User-Agent: ImposOS/1.0\n");
            VPRINT("> Connection: close\n");
            VPRINT(">\n");
        }

        int send_ok;
        if (tls)
            send_ok = tls_send(tls, (const uint8_t *)req, (size_t)rlen);
        else
            send_ok = socket_send(sock, req, (size_t)rlen);

        if (send_ok < 0) {
            DBG("[HTTP] Failed to send request");
            if (tls) { tls_close(tls); free(tls); }
            socket_close(sock);
            rc = -5;
            break;
        }

        /* Read response into a growing buffer */
        uint32_t buf_cap = 4096;
        uint32_t buf_len = 0;
        char *buf = malloc(buf_cap);
        if (!buf) {
            if (tls) { tls_close(tls); free(tls); }
            socket_close(sock);
            rc = -6;
            break;
        }

        while (1) {
            if (buf_len + 1024 > buf_cap) {
                buf_cap *= 2;
                if (buf_cap > 1024 * 1024) break;  /* 1MB limit */
                char *newbuf = realloc(buf, buf_cap);
                if (!newbuf) break;
                buf = newbuf;
            }

            int n;
            if (tls)
                n = tls_recv(tls, (uint8_t *)(buf + buf_len), buf_cap - buf_len);
            else
                n = socket_recv(sock, buf + buf_len, buf_cap - buf_len, 5000);
            if (n <= 0) break;  /* EOF or timeout */
            buf_len += (uint32_t)n;
        }

        if (tls) { tls_close(tls); free(tls); }
        socket_close(sock);

        if (buf_len == 0) {
            free(buf);
            rc = -7;
            break;
        }

        VPRINT("* Received %u bytes\n", buf_len);

        /* Parse status line: "HTTP/1.x NNN reason\r\n" */
        char *line_end = strstr(buf, "\r\n");
        if (!line_end) {
            free(buf);
            rc = -8;
            break;
        }

        /* Verbose: print status line (reuse req[] as scratch — already sent) */
        if (http_verbose) {
            size_t slen = (size_t)(line_end - buf);
            if (slen >= sizeof(req)) slen = sizeof(req) - 1;
            memcpy(req, buf, slen);
            req[slen] = '\0';
            VPRINT("< %s\n", req);
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
            rc = 0;
            break;
        }

        /* Header region: from after status line to body separator */
        char *hdr_start = line_end + 2;
        char *hdr_end = body_sep;
        size_t hdr_len = (size_t)(hdr_end - hdr_start);

        /* Verbose: print response headers (reuse req[] as scratch) */
        if (http_verbose) {
            char *hp = hdr_start;
            while (hp < hdr_end) {
                char *nl = strstr(hp, "\r\n");
                if (!nl || nl > hdr_end) break;
                size_t ll = (size_t)(nl - hp);
                if (ll >= sizeof(req)) ll = sizeof(req) - 1;
                memcpy(req, hp, ll);
                req[ll] = '\0';
                VPRINT("< %s\n", req);
                hp = nl + 2;
            }
            VPRINT("<\n");
        }

        /* Check for redirect: 301, 302, 303, 307, 308 */
        int sc = resp->status_code;
        if (sc == 301 || sc == 302 || sc == 303 || sc == 307 || sc == 308) {
            const char *loc = http_find_header(hdr_start, hdr_len, "Location");
            if (loc) {
                /* Extract Location value into heap buffer (reuse cur_url's
                 * 512 bytes via a temporary heap alloc to avoid stack bloat) */
                char *redir = malloc(512);
                if (!redir) { free(buf); rc = -6; break; }
                int j = 0;
                while (*loc && *loc != '\r' && *loc != '\n' && j < 511)
                    redir[j++] = *loc++;
                redir[j] = '\0';

                VPRINT("* Following redirect (%d) -> %s\n", sc, redir);
                free(buf);

                /* Build next URL in cur_url */
                if (redir[0] == '/') {
                    /* Relative path — prepend scheme://host[:port] */
                    if (port == (uint16_t)(is_https ? 443 : 80)) {
                        snprintf(cur_url, 512, "%s://%s%s",
                                 is_https ? "https" : "http", host, redir);
                    } else {
                        snprintf(cur_url, 512, "%s://%s:%d%s",
                                 is_https ? "https" : "http", host, port, redir);
                    }
                } else {
                    /* Absolute URL */
                    size_t rlen2 = strlen(redir);
                    if (rlen2 >= 512) rlen2 = 511;
                    memcpy(cur_url, redir, rlen2);
                    cur_url[rlen2] = '\0';
                }
                free(redir);
                continue;  /* next iteration of redirect loop */
            }
        }

        /* No redirect — parse Content-Type and extract body */
        const char *ct_val = http_find_header(hdr_start, hdr_len, "Content-Type");
        if (ct_val) {
            const char *ct_end = strstr(ct_val, "\r\n");
            if (ct_end) {
                size_t vlen = (size_t)(ct_end - ct_val);
                if (vlen >= sizeof(resp->content_type))
                    vlen = sizeof(resp->content_type) - 1;
                memcpy(resp->content_type, ct_val, vlen);
                resp->content_type[vlen] = '\0';
            }
        }

        char *body_start = body_sep + 4;
        resp->body_len = buf_len - (uint32_t)(body_start - buf);

        /* Move body to start of buffer to save memory */
        memmove(buf, body_start, resp->body_len);
        buf[resp->body_len] = '\0';
        resp->body = buf;

        rc = 0;
        break;
    }

    free(cur_url);
    if (rc == -10) {
        VPRINT("* Too many redirects\n");
        DBG("[HTTP] Too many redirects");
    }
    return rc;
}

void http_response_free(http_response_t *resp) {
    if (resp && resp->body) {
        free(resp->body);
        resp->body = NULL;
        resp->body_len = 0;
    }
}
