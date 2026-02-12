#ifndef _KERNEL_TLS_H
#define _KERNEL_TLS_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/crypto.h>

/* TLS 1.2 content types */
#define TLS_CHANGE_CIPHER_SPEC 20
#define TLS_ALERT              21
#define TLS_HANDSHAKE          22
#define TLS_APPLICATION_DATA   23

/* TLS 1.2 handshake types */
#define TLS_HS_CLIENT_HELLO     1
#define TLS_HS_SERVER_HELLO     2
#define TLS_HS_CERTIFICATE      11
#define TLS_HS_SERVER_HELLO_DONE 14
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_FINISHED         20

/* TLS version */
#define TLS_VERSION_1_2  0x0303

/* Cipher suite: TLS_RSA_WITH_AES_128_CBC_SHA256 */
#define TLS_RSA_AES128_CBC_SHA256 0x003C

/* Max TLS record payload */
#define TLS_MAX_RECORD   16384
#define TLS_RECV_BUF     (TLS_MAX_RECORD + 512)

/* TLS connection state */
typedef struct {
    int      sock_fd;
    int      established;

    /* Handshake transcript hash */
    sha256_ctx_t hs_hash;

    /* Random values */
    uint8_t  client_random[32];
    uint8_t  server_random[32];

    /* Master secret */
    uint8_t  master_secret[48];

    /* Active keys (after ChangeCipherSpec) */
    uint8_t  client_write_mac_key[32];
    uint8_t  server_write_mac_key[32];
    uint8_t  client_write_key[16];
    uint8_t  server_write_key[16];
    aes128_ctx_t client_aes;
    aes128_ctx_t server_aes;

    /* Sequence numbers */
    uint64_t client_seq;
    uint64_t server_seq;

    /* Encryption active flags */
    int      client_encrypted;
    int      server_encrypted;

    /* Receive buffer for decrypted data */
    uint8_t  recv_buf[TLS_RECV_BUF];
    size_t   recv_len;
    size_t   recv_pos;

    /* Server's RSA public key */
    rsa_pubkey_t server_key;
} tls_conn_t;

/* Connect TLS over an existing TCP socket */
int tls_connect(tls_conn_t *conn, int sock_fd, const char *hostname);

/* Send/receive application data */
int tls_send(tls_conn_t *conn, const uint8_t *data, size_t len);
int tls_recv(tls_conn_t *conn, uint8_t *buf, size_t len);

/* Close TLS connection */
void tls_close(tls_conn_t *conn);

/* High-level HTTPS GET — downloads a URL into a malloc'd buffer.
 * Returns response body length, or <0 on error.
 * Caller must free(*out_body) if return > 0. */
int https_get(const char *host, uint16_t port, const char *path,
              uint8_t **out_body, size_t *out_len);

/* Async HTTPS GET — runs in a background thread so UI stays responsive */
typedef struct {
    /* Input (set by caller before launch) */
    char     host[256];
    uint16_t port;
    char     path[256];
    /* Output (set by thread on completion) */
    uint8_t *body;
    size_t   body_len;
    int      result;      /* >0 = body_len, <0 = error */
    volatile int done;    /* 0 = running, 1 = finished */
    int      tid;         /* thread task id */
} https_async_t;

/* Start async HTTPS GET. Returns 0 on success, -1 if thread creation fails.
 * Caller should poll req->done in a loop with task_yield(). */
int https_get_async(https_async_t *req);

#endif
