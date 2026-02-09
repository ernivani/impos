#ifndef _KERNEL_SOCKET_H
#define _KERNEL_SOCKET_H

#include <stdint.h>
#include <stddef.h>

#define SOCK_STREAM 1  /* TCP */
#define SOCK_DGRAM  2  /* UDP */

#define MAX_SOCKETS 16

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

#endif
