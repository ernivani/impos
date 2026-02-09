#include <kernel/httpd.h>
#include <kernel/socket.h>
#include <kernel/tcp.h>
#include <kernel/fs.h>
#include <kernel/net.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_PORT 80
#define HTTP_MAX_REQUEST 2048

static int httpd_running = 0;
static int listen_fd = -1;

static const char* http_200 = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n";
static const char* http_404 = "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                               "<html><body><h1>404 Not Found</h1></body></html>";
static const char* http_index =
    "<html><head><title>ImposOS</title></head><body>"
    "<h1>Welcome to ImposOS!</h1>"
    "<p>This page is being served by ImposOS's built-in HTTP server.</p>"
    "<p>Try requesting a file from the filesystem, e.g. <code>/etc/hostname</code></p>"
    "</body></html>";

void httpd_initialize(void) {
    httpd_running = 0;
    listen_fd = -1;
}

int httpd_start(void) {
    if (httpd_running) {
        printf("httpd: already running\n");
        return -1;
    }

    listen_fd = socket_create(SOCK_STREAM);
    if (listen_fd < 0) {
        printf("httpd: failed to create socket\n");
        return -1;
    }

    if (socket_bind(listen_fd, HTTP_PORT) != 0) {
        printf("httpd: failed to bind port %d\n", HTTP_PORT);
        socket_close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    if (socket_listen(listen_fd, 1) != 0) {
        printf("httpd: failed to listen\n");
        socket_close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    httpd_running = 1;
    printf("httpd: listening on port %d\n", HTTP_PORT);
    return 0;
}

void httpd_stop(void) {
    if (!httpd_running) return;
    if (listen_fd >= 0) {
        socket_close(listen_fd);
        listen_fd = -1;
    }
    httpd_running = 0;
    printf("httpd: stopped\n");
}

static void handle_request(int client_fd) {
    char request[HTTP_MAX_REQUEST];
    int n = socket_recv(client_fd, request, sizeof(request) - 1, 3000);
    if (n <= 0) {
        socket_close(client_fd);
        return;
    }
    request[n] = '\0';

    /* Parse GET line */
    char* method = request;
    char* path = NULL;

    if (strncmp(method, "GET ", 4) == 0) {
        path = method + 4;
        char* end = strchr(path, ' ');
        if (end) *end = '\0';
    }

    if (!path) {
        socket_send(client_fd, http_404, strlen(http_404));
        socket_close(client_fd);
        return;
    }

    /* Serve / as index */
    if (strcmp(path, "/") == 0) {
        socket_send(client_fd, http_200, strlen(http_200));
        socket_send(client_fd, http_index, strlen(http_index));
        socket_close(client_fd);
        return;
    }

    /* Try to serve from filesystem */
    uint8_t* fbuf = (uint8_t*)malloc(MAX_FILE_SIZE);
    if (!fbuf) {
        socket_send(client_fd, http_404, strlen(http_404));
        socket_close(client_fd);
        return;
    }

    size_t fsize;
    if (fs_read_file(path, fbuf, &fsize) == 0) {
        socket_send(client_fd, http_200, strlen(http_200));
        socket_send(client_fd, fbuf, fsize);
    } else {
        socket_send(client_fd, http_404, strlen(http_404));
    }

    free(fbuf);
    socket_close(client_fd);
}

void httpd_poll(void) {
    if (!httpd_running || listen_fd < 0) return;

    /* Non-blocking accept: check if a connection is waiting */
    net_process_packets();

    /* Try to accept - this will block briefly */
    int client_fd = socket_accept(listen_fd);
    if (client_fd >= 0) {
        handle_request(client_fd);
    }
}

int httpd_is_running(void) {
    return httpd_running;
}
