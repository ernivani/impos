#include <kernel/tcp.h>
#include <kernel/ip.h>
#include <kernel/net.h>
#include <kernel/idt.h>
#include <kernel/endian.h>
#include <string.h>
#include <stdio.h>

static tcb_t tcbs[TCP_MAX_CONNECTIONS];
static uint16_t next_ephemeral_port = 49152;

/* Ring buffer helpers */
static int ring_write(tcp_ring_t* r, const uint8_t* data, size_t len) {
    size_t written = 0;
    while (written < len && r->count < TCP_BUFFER_SIZE) {
        r->buf[r->head] = data[written++];
        r->head = (r->head + 1) % TCP_BUFFER_SIZE;
        r->count++;
    }
    return written;
}

static int ring_read(tcp_ring_t* r, uint8_t* buf, size_t len) {
    size_t nread = 0;
    while (nread < len && r->count > 0) {
        buf[nread++] = r->buf[r->tail];
        r->tail = (r->tail + 1) % TCP_BUFFER_SIZE;
        r->count--;
    }
    return nread;
}

static int ring_peek(tcp_ring_t* r, uint8_t* buf, size_t len, size_t offset) {
    size_t nread = 0;
    size_t pos = (r->tail + offset) % TCP_BUFFER_SIZE;
    size_t avail = r->count > offset ? r->count - offset : 0;
    while (nread < len && nread < avail) {
        buf[nread++] = r->buf[pos];
        pos = (pos + 1) % TCP_BUFFER_SIZE;
    }
    return nread;
}

/* TCP checksum with pseudo-header */
static uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                              const uint8_t* tcp_pkt, size_t tcp_len) {
    uint32_t sum = 0;
    sum += (src_ip[0] << 8) | src_ip[1];
    sum += (src_ip[2] << 8) | src_ip[3];
    sum += (dst_ip[0] << 8) | dst_ip[1];
    sum += (dst_ip[2] << 8) | dst_ip[3];
    sum += 6; /* TCP protocol */
    sum += tcp_len;

    const uint16_t* words = (const uint16_t*)tcp_pkt;
    size_t remaining = tcp_len;
    while (remaining > 1) {
        sum += *words++;
        remaining -= 2;
    }
    if (remaining == 1)
        sum += *(const uint8_t*)words;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t result = ~sum;
    return result == 0 ? 0xFFFF : result;
}

/* Send a TCP segment */
static int tcp_send_segment(tcb_t* tcb, uint8_t flags, const uint8_t* data, size_t data_len) {
    uint8_t packet[sizeof(tcp_header_t) + TCP_MSS];
    tcp_header_t* hdr = (tcp_header_t*)packet;
    size_t total = sizeof(tcp_header_t) + data_len;

    memset(hdr, 0, sizeof(tcp_header_t));
    hdr->src_port    = htons(tcb->local_port);
    hdr->dst_port    = htons(tcb->remote_port);
    hdr->seq_num     = htonl(tcb->snd_nxt);
    hdr->ack_num     = htonl(tcb->rcv_nxt);
    hdr->data_offset = (sizeof(tcp_header_t) / 4) << 4;
    hdr->flags       = flags;
    hdr->window      = htons(TCP_BUFFER_SIZE - tcb->rx_ring.count);
    hdr->checksum    = 0;
    hdr->urgent_ptr  = 0;

    if (data_len > 0)
        memcpy(packet + sizeof(tcp_header_t), data, data_len);

    net_config_t* cfg = net_get_config();
    hdr->checksum = tcp_checksum(cfg->ip, tcb->remote_ip, packet, total);

    tcb->last_send_tick = pit_get_ticks();

    return ip_send_packet(tcb->remote_ip, IP_PROTOCOL_TCP, packet, total);
}

void tcp_initialize(void) {
    memset(tcbs, 0, sizeof(tcbs));
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcbs[i].state = TCP_CLOSED;
        tcbs[i].backlog_conn = -1;
    }
}

int tcp_open(uint16_t local_port, int listen) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (tcbs[i].state == TCP_CLOSED) {
            memset(&tcbs[i], 0, sizeof(tcb_t));
            tcbs[i].local_port = local_port;
            tcbs[i].state = listen ? TCP_LISTEN : TCP_CLOSED;
            tcbs[i].is_listen = listen;
            tcbs[i].backlog_conn = -1;
            tcbs[i].rto_ticks = TCP_RTO_INIT;
            tcbs[i].rcv_wnd = TCP_BUFFER_SIZE;
            tcbs[i].snd_wnd = TCP_BUFFER_SIZE;
            /* Simple ISN based on tick counter */
            tcbs[i].snd_nxt = pit_get_ticks() * 64;
            tcbs[i].snd_una = tcbs[i].snd_nxt;
            return i;
        }
    }
    return -1;
}

int tcp_connect(int idx, const uint8_t dst_ip[4], uint16_t dst_port) {
    if (idx < 0 || idx >= TCP_MAX_CONNECTIONS) return -1;
    tcb_t* tcb = &tcbs[idx];
    if (tcb->state != TCP_CLOSED) return -1;

    memcpy(tcb->remote_ip, dst_ip, 4);
    tcb->remote_port = dst_port;
    if (tcb->local_port == 0)
        tcb->local_port = next_ephemeral_port++;

    /* Send SYN */
    tcb->state = TCP_SYN_SENT;
    tcp_send_segment(tcb, TCP_SYN, NULL, 0);
    tcb->snd_nxt++; /* SYN consumes one seq */

    /* Wait for SYN-ACK */
    uint32_t start = pit_get_ticks();
    while (tcb->state == TCP_SYN_SENT) {
        net_process_packets();
        if (pit_get_ticks() - start > 500) /* 5 second timeout */
            return -1;
    }

    return tcb->state == TCP_ESTABLISHED ? 0 : -1;
}

int tcp_send(int idx, const uint8_t* data, size_t len) {
    if (idx < 0 || idx >= TCP_MAX_CONNECTIONS) return -1;
    tcb_t* tcb = &tcbs[idx];
    if (tcb->state != TCP_ESTABLISHED && tcb->state != TCP_CLOSE_WAIT)
        return -1;

    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        tcp_send_segment(tcb, TCP_ACK | TCP_PSH, data + sent, chunk);
        tcb->snd_nxt += chunk;
        sent += chunk;

        /* Brief poll for ACKs between segments */
        net_process_packets();
    }
    return sent;
}

int tcp_recv(int idx, uint8_t* buf, size_t len, uint32_t timeout_ms) {
    if (idx < 0 || idx >= TCP_MAX_CONNECTIONS) return -1;
    tcb_t* tcb = &tcbs[idx];

    uint32_t start = pit_get_ticks();
    uint32_t timeout_ticks = timeout_ms / 10;

    while (1) {
        net_process_packets();

        if (tcb->rx_ring.count > 0) {
            return ring_read(&tcb->rx_ring, buf, len);
        }

        /* Connection closed by peer */
        if (tcb->state == TCP_CLOSE_WAIT || tcb->state == TCP_CLOSED ||
            tcb->state == TCP_TIME_WAIT)
            return 0;

        if (pit_get_ticks() - start >= timeout_ticks)
            return -1; /* timeout */
    }
}

int tcp_accept(int listen_idx) {
    if (listen_idx < 0 || listen_idx >= TCP_MAX_CONNECTIONS) return -1;
    tcb_t* ltcb = &tcbs[listen_idx];
    if (ltcb->state != TCP_LISTEN) return -1;

    /* Poll until a connection arrives */
    while (1) {
        net_process_packets();

        if (ltcb->backlog_conn >= 0) {
            int idx = ltcb->backlog_conn;
            ltcb->backlog_conn = -1;
            if (tcbs[idx].state == TCP_ESTABLISHED)
                return idx;
        }

        /* Don't busy-wait too hard */
        __asm__ volatile ("hlt");
    }
}

void tcp_close(int idx) {
    if (idx < 0 || idx >= TCP_MAX_CONNECTIONS) return;
    tcb_t* tcb = &tcbs[idx];

    if (tcb->state == TCP_ESTABLISHED) {
        tcb->state = TCP_FIN_WAIT_1;
        tcp_send_segment(tcb, TCP_FIN | TCP_ACK, NULL, 0);
        tcb->snd_nxt++;

        /* Wait briefly for FIN-ACK */
        uint32_t start = pit_get_ticks();
        while (tcb->state != TCP_CLOSED && tcb->state != TCP_TIME_WAIT) {
            net_process_packets();
            if (pit_get_ticks() - start > 300) break; /* 3 sec */
        }
    } else if (tcb->state == TCP_CLOSE_WAIT) {
        tcb->state = TCP_LAST_ACK;
        tcp_send_segment(tcb, TCP_FIN | TCP_ACK, NULL, 0);
        tcb->snd_nxt++;

        uint32_t start = pit_get_ticks();
        while (tcb->state != TCP_CLOSED) {
            net_process_packets();
            if (pit_get_ticks() - start > 300) break;
        }
    }

    tcb->state = TCP_CLOSED;
}

tcp_state_t tcp_get_state(int idx) {
    if (idx < 0 || idx >= TCP_MAX_CONNECTIONS) return TCP_CLOSED;
    return tcbs[idx].state;
}

/* Handle incoming TCP packet */
void tcp_handle_packet(const uint8_t* data, size_t len, const uint8_t src_ip[4]) {
    if (len < sizeof(tcp_header_t)) return;

    const tcp_header_t* hdr = (const tcp_header_t*)data;
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t src_port = ntohs(hdr->src_port);
    uint32_t seq = ntohl(hdr->seq_num);
    uint32_t ack = ntohl(hdr->ack_num);
    uint8_t  flags = hdr->flags;
    uint16_t window = ntohs(hdr->window);
    size_t hdr_len = ((hdr->data_offset >> 4) & 0xF) * 4;
    const uint8_t* payload = data + hdr_len;
    size_t payload_len = len - hdr_len;

    /* Find matching TCB */
    tcb_t* tcb = NULL;
    int tcb_idx = -1;
    tcb_t* listen_tcb = NULL;

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (tcbs[i].state == TCP_CLOSED) continue;

        if (tcbs[i].state == TCP_LISTEN && tcbs[i].local_port == dst_port) {
            listen_tcb = &tcbs[i];
            continue;
        }

        if (tcbs[i].local_port == dst_port &&
            tcbs[i].remote_port == src_port &&
            memcmp(tcbs[i].remote_ip, src_ip, 4) == 0) {
            tcb = &tcbs[i];
            tcb_idx = i;
            break;
        }
    }

    /* Handle SYN on listening socket */
    if (!tcb && listen_tcb && (flags & TCP_SYN)) {
        /* Find free TCB for new connection */
        for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
            if (tcbs[i].state == TCP_CLOSED) {
                tcb = &tcbs[i];
                tcb_idx = i;
                memset(tcb, 0, sizeof(tcb_t));
                tcb->local_port = dst_port;
                tcb->remote_port = src_port;
                memcpy(tcb->remote_ip, src_ip, 4);
                tcb->rcv_nxt = seq + 1;
                tcb->snd_nxt = pit_get_ticks() * 64;
                tcb->snd_una = tcb->snd_nxt;
                tcb->snd_wnd = window;
                tcb->rcv_wnd = TCP_BUFFER_SIZE;
                tcb->rto_ticks = TCP_RTO_INIT;
                tcb->backlog_conn = -1;
                tcb->state = TCP_SYN_RECEIVED;

                /* Send SYN-ACK */
                tcp_send_segment(tcb, TCP_SYN | TCP_ACK, NULL, 0);
                tcb->snd_nxt++;

                listen_tcb->backlog_conn = tcb_idx;
                return;
            }
        }
        /* No free TCB - send RST */
        return;
    }

    if (!tcb) {
        /* No matching connection - could send RST but skip for simplicity */
        return;
    }

    /* Update send window */
    tcb->snd_wnd = window;

    switch (tcb->state) {
    case TCP_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            tcb->rcv_nxt = seq + 1;
            tcb->snd_una = ack;
            tcb->state = TCP_ESTABLISHED;
            tcp_send_segment(tcb, TCP_ACK, NULL, 0);
        }
        break;

    case TCP_SYN_RECEIVED:
        if (flags & TCP_ACK) {
            tcb->snd_una = ack;
            tcb->state = TCP_ESTABLISHED;
        }
        break;

    case TCP_ESTABLISHED:
        if (flags & TCP_ACK) {
            tcb->snd_una = ack;
        }

        /* Receive data */
        if (payload_len > 0 && seq == tcb->rcv_nxt) {
            ring_write(&tcb->rx_ring, payload, payload_len);
            tcb->rcv_nxt += payload_len;
            tcp_send_segment(tcb, TCP_ACK, NULL, 0);
        }

        if (flags & TCP_FIN) {
            tcb->rcv_nxt++;
            tcb->state = TCP_CLOSE_WAIT;
            tcp_send_segment(tcb, TCP_ACK, NULL, 0);
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            tcb->snd_una = ack;
            if (flags & TCP_FIN) {
                tcb->rcv_nxt = seq + 1;
                tcb->state = TCP_TIME_WAIT;
                tcp_send_segment(tcb, TCP_ACK, NULL, 0);
            } else {
                tcb->state = TCP_FIN_WAIT_2;
            }
        }
        /* Receive data even in FIN_WAIT_1 */
        if (payload_len > 0 && seq == tcb->rcv_nxt) {
            ring_write(&tcb->rx_ring, payload, payload_len);
            tcb->rcv_nxt += payload_len;
        }
        break;

    case TCP_FIN_WAIT_2:
        if (payload_len > 0 && seq == tcb->rcv_nxt) {
            ring_write(&tcb->rx_ring, payload, payload_len);
            tcb->rcv_nxt += payload_len;
            tcp_send_segment(tcb, TCP_ACK, NULL, 0);
        }
        if (flags & TCP_FIN) {
            tcb->rcv_nxt = seq + 1;
            tcb->state = TCP_TIME_WAIT;
            tcp_send_segment(tcb, TCP_ACK, NULL, 0);
        }
        break;

    case TCP_CLOSE_WAIT:
        /* Waiting for application to close */
        break;

    case TCP_LAST_ACK:
        if (flags & TCP_ACK) {
            tcb->state = TCP_CLOSED;
        }
        break;

    case TCP_TIME_WAIT:
        /* ACK any late FINs */
        if (flags & TCP_FIN) {
            tcp_send_segment(tcb, TCP_ACK, NULL, 0);
        }
        break;

    default:
        break;
    }
}

void tcp_timer_tick(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcb_t* tcb = &tcbs[i];

        /* TIME_WAIT expiry (2 * MSL ≈ 6 seconds) */
        if (tcb->state == TCP_TIME_WAIT) {
            if (pit_get_ticks() - tcb->last_send_tick > 600) {
                tcb->state = TCP_CLOSED;
            }
        }

        /* Retransmission for SYN_SENT, SYN_RECEIVED */
        if (tcb->state == TCP_SYN_SENT || tcb->state == TCP_SYN_RECEIVED) {
            if (pit_get_ticks() - tcb->last_send_tick > tcb->rto_ticks) {
                if (tcb->retries >= TCP_MAX_RETRIES) {
                    tcb->state = TCP_CLOSED;
                    continue;
                }
                tcb->retries++;
                tcb->rto_ticks *= 2; /* Exponential backoff */
                if (tcb->state == TCP_SYN_SENT)
                    tcp_send_segment(tcb, TCP_SYN, NULL, 0);
                else
                    tcp_send_segment(tcb, TCP_SYN | TCP_ACK, NULL, 0);
            }
        }
    }
}
