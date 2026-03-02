#include <kernel/socket.h>
#include <kernel/tcp.h>
#include <kernel/udp.h>
#include <kernel/pipe.h>
#include <string.h>

typedef struct {
    int type;           /* SOCK_STREAM or SOCK_DGRAM */
    int active;
    int proto_idx;      /* TCP tcb index or UDP binding index */
    uint16_t port;      /* bound port */
    int nonblock;       /* O_NONBLOCK flag */
    int listening;      /* 1 if listen() called */
    uint8_t remote_ip[4];
    uint16_t remote_port;
} socket_t;

static socket_t sockets[MAX_SOCKETS];

void socket_initialize(void) {
    memset(sockets, 0, sizeof(sockets));
}

int socket_create(int type) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].active) {
            memset(&sockets[i], 0, sizeof(socket_t));
            sockets[i].type = type;
            sockets[i].active = 1;
            sockets[i].proto_idx = -1;
            return i;
        }
    }
    return -1;
}

int socket_bind(int fd, uint16_t port) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    s->port = port;
    if (s->type == SOCK_DGRAM) {
        if (udp_bind(port) != 0) return -1;
    }
    return 0;
}

int socket_listen(int fd, int backlog) {
    (void)backlog;
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_STREAM) return -1;
    int idx = tcp_open(s->port, 1);
    if (idx < 0) return -1;
    s->proto_idx = idx;
    s->listening = 1;
    return 0;
}

int socket_accept(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_STREAM || s->proto_idx < 0) return -1;

    int conn_idx = tcp_accept(s->proto_idx, 0xFFFFFFFF);
    if (conn_idx < 0) return -1;

    /* Create a new socket for the accepted connection */
    int new_fd = socket_create(SOCK_STREAM);
    if (new_fd < 0) {
        tcp_close(conn_idx);
        return -1;
    }
    sockets[new_fd].proto_idx = conn_idx;
    return new_fd;
}

int socket_accept_nb(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_STREAM || s->proto_idx < 0) return -1;

    int conn_idx = tcp_accept(s->proto_idx, 0);
    if (conn_idx == -2) return -2; /* EAGAIN */
    if (conn_idx < 0) return -1;

    int new_fd = socket_create(SOCK_STREAM);
    if (new_fd < 0) {
        tcp_close(conn_idx);
        return -1;
    }
    sockets[new_fd].proto_idx = conn_idx;
    return new_fd;
}

int socket_connect(int fd, const uint8_t ip[4], uint16_t port) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_STREAM) return -1;

    int idx = tcp_open(s->port, 0);
    if (idx < 0) return -1;
    s->proto_idx = idx;

    memcpy(s->remote_ip, ip, 4);
    s->remote_port = port;

    return tcp_connect(idx, ip, port);
}

int socket_send(int fd, const void* data, size_t len) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_STREAM || s->proto_idx < 0) return -1;
    return tcp_send(s->proto_idx, (const uint8_t*)data, len);
}

int socket_recv(int fd, void* buf, size_t len, uint32_t timeout_ms) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_STREAM || s->proto_idx < 0) return -1;
    return tcp_recv(s->proto_idx, (uint8_t*)buf, len, timeout_ms);
}

int socket_sendto(int fd, const void* data, size_t len,
                   const uint8_t ip[4], uint16_t port) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_DGRAM) return -1;
    return udp_send(ip, port, s->port, (const uint8_t*)data, len);
}

int socket_recvfrom(int fd, void* buf, size_t* len,
                     uint8_t src_ip[4], uint16_t* src_port,
                     uint32_t timeout_ms) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t* s = &sockets[fd];
    if (s->type != SOCK_DGRAM) return -1;
    return udp_recv(s->port, (uint8_t*)buf, len, src_ip, src_port, timeout_ms);
}

void socket_close(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return;
    socket_t* s = &sockets[fd];
    if (s->type == SOCK_STREAM && s->proto_idx >= 0)
        tcp_close(s->proto_idx);
    else if (s->type == SOCK_DGRAM && s->port > 0)
        udp_unbind(s->port);
    memset(s, 0, sizeof(socket_t));
}

/* ── Accessors for syscall layer ──────────────────────────────────── */

int socket_set_nonblock(int fd, int on) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    sockets[fd].nonblock = on ? 1 : 0;
    return 0;
}

int socket_get_nonblock(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return 0;
    return sockets[fd].nonblock;
}

int socket_is_listening(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return 0;
    return sockets[fd].listening;
}

int socket_get_type(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    return sockets[fd].type;
}

int socket_get_proto_idx(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    return sockets[fd].proto_idx;
}

int socket_get_remote(int fd, uint8_t ip[4], uint16_t *port) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    if (ip) memcpy(ip, sockets[fd].remote_ip, 4);
    if (port) *port = sockets[fd].remote_port;
    return 0;
}

int socket_poll_query(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return 0;
    socket_t *s = &sockets[fd];
    int events = 0;

    if (s->type == SOCK_STREAM && s->proto_idx >= 0) {
        tcp_state_t st = tcp_get_state(s->proto_idx);

        if (s->listening) {
            /* Listening socket: POLLIN means accept() ready */
            if (tcp_has_backlog(s->proto_idx))
                events |= PIPE_POLL_IN;
        } else {
            /* Connected socket */
            if (st == TCP_ESTABLISHED || st == TCP_CLOSE_WAIT) {
                if (tcp_rx_available(s->proto_idx) > 0)
                    events |= PIPE_POLL_IN;
                if (st == TCP_ESTABLISHED)
                    events |= PIPE_POLL_OUT;
            }
            if (st == TCP_CLOSE_WAIT || st == TCP_CLOSED ||
                st == TCP_TIME_WAIT)
                events |= PIPE_POLL_HUP;
        }
    } else if (s->type == SOCK_DGRAM) {
        extern int udp_rx_available(uint16_t port);
        if (udp_rx_available(s->port) > 0)
            events |= PIPE_POLL_IN;
        events |= PIPE_POLL_OUT; /* UDP always writable */
    }

    return events;
}

int socket_recv_nb(int fd, void *buf, size_t len) {
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].active) return -1;
    socket_t *s = &sockets[fd];
    if (s->type != SOCK_STREAM || s->proto_idx < 0) return -1;
    return tcp_recv_nb(s->proto_idx, (uint8_t *)buf, len);
}
