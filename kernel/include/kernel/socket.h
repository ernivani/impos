#ifndef _KERNEL_SOCKET_H
#define _KERNEL_SOCKET_H

#include <stdint.h>
#include <stddef.h>

#define SOCK_STREAM 1  /* TCP */
#define SOCK_DGRAM  2  /* UDP */

#define MAX_SOCKETS 16

/* Linux address families */
#define AF_UNIX  1
#define AF_INET  2

struct linux_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;     /* network byte order */
    uint32_t sin_addr;     /* network byte order */
    uint8_t  sin_zero[8];
} __attribute__((packed));

void socket_initialize(void);
int  socket_create(int type);
int  socket_bind(int fd, uint16_t port);
int  socket_listen(int fd, int backlog);
int  socket_accept(int fd);
int  socket_connect(int fd, const uint8_t ip[4], uint16_t port);
int  socket_send(int fd, const void* data, size_t len);
int  socket_recv(int fd, void* buf, size_t len, uint32_t timeout_ms);
int  socket_sendto(int fd, const void* data, size_t len,
                    const uint8_t ip[4], uint16_t port);
int  socket_recvfrom(int fd, void* buf, size_t* len,
                      uint8_t src_ip[4], uint16_t* src_port,
                      uint32_t timeout_ms);
void socket_close(int fd);

/* Accessors for syscall layer */
int  socket_set_nonblock(int fd, int on);
int  socket_get_nonblock(int fd);
int  socket_is_listening(int fd);
int  socket_get_type(int fd);
int  socket_get_proto_idx(int fd);
int  socket_get_remote(int fd, uint8_t ip[4], uint16_t *port);
int  socket_poll_query(int fd);    /* returns POLLIN/POLLOUT/POLLHUP bitmask */
int  socket_recv_nb(int fd, void *buf, size_t len); /* -2 = EAGAIN */

/* Non-blocking accept: returns new socket fd, -2 = EAGAIN */
int  socket_accept_nb(int fd);

#endif
