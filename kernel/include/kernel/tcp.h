#ifndef _KERNEL_TCP_H
#define _KERNEL_TCP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset; /* upper 4 bits = offset in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/* TCP flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* TCP states */
typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
    TCP_CLOSING
} tcp_state_t;

#define TCP_MAX_CONNECTIONS 8
#define TCP_BUFFER_SIZE     4096
#define TCP_MSS             1400
#define TCP_MAX_RETRIES     5
#define TCP_RTO_INIT        100  /* 1 second in ticks (100Hz) */

typedef struct {
    uint8_t  buf[TCP_BUFFER_SIZE];
    uint16_t head, tail, count;
} tcp_ring_t;

typedef struct {
    tcp_state_t state;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t  remote_ip[4];
    uint32_t snd_una;     /* oldest unacked seq */
    uint32_t snd_nxt;     /* next seq to send */
    uint32_t snd_wnd;     /* send window */
    uint32_t rcv_nxt;     /* next expected seq from peer */
    uint32_t rcv_wnd;     /* receive window */
    tcp_ring_t rx_ring;   /* received data buffer */
    tcp_ring_t tx_ring;   /* data awaiting send */
    uint32_t rto_ticks;   /* retransmission timeout */
    uint32_t last_send_tick;
    int      retries;
    int      is_listen;   /* passive open */
    int      backlog_conn; /* TCB index of accepted connection, -1 = none */
} tcb_t;

void tcp_initialize(void);
int  tcp_open(uint16_t local_port, int listen);
int  tcp_connect(int tcb_idx, const uint8_t dst_ip[4], uint16_t dst_port);
int  tcp_send(int tcb_idx, const uint8_t* data, size_t len);
int  tcp_recv(int tcb_idx, uint8_t* buf, size_t len, uint32_t timeout_ms);
int  tcp_accept(int listen_idx);
void tcp_close(int tcb_idx);
tcp_state_t tcp_get_state(int tcb_idx);
void tcp_handle_packet(const uint8_t* data, size_t len, const uint8_t src_ip[4]);
void tcp_timer_tick(void);

#endif
