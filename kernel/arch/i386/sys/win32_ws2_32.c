#include <kernel/win32_types.h>
#include <kernel/socket.h>
#include <kernel/dns.h>
#include <kernel/hostname.h>
#include <kernel/endian.h>
#include <kernel/net.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Winsock Constants ───────────────────────────────────────── */

#define AF_INET         2
#define WS_SOCK_STREAM  1
#define WS_SOCK_DGRAM   2
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

#define SOL_SOCKET      0xFFFF
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_BROADCAST    0x0020
#define SO_LINGER       0x0080
#define SO_SNDBUF       0x1001
#define SO_RCVBUF       0x1002
#define SO_RCVTIMEO     0x1006
#define SO_SNDTIMEO     0x1005
#define TCP_NODELAY     0x0001

#define FIONBIO         0x8004667E

#define INVALID_SOCKET  (~0u)
#define SOCKET_ERROR    (-1)

/* WSA error codes */
#define WSABASEERR         10000
#define WSAEINTR           10004
#define WSAEBADF           10009
#define WSAEACCES          10013
#define WSAEFAULT          10014
#define WSAEINVAL          10022
#define WSAEMFILE          10024
#define WSAEWOULDBLOCK     10035
#define WSAEINPROGRESS     10036
#define WSAEALREADY        10037
#define WSAENOTSOCK        10038
#define WSAEDESTADDRREQ    10039
#define WSAEMSGSIZE        10040
#define WSAEPROTOTYPE      10041
#define WSAENOPROTOOPT     10042
#define WSAEPROTONOSUPPORT 10043
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP      10045
#define WSAEAFNOSUPPORT    10047
#define WSAEADDRINUSE      10048
#define WSAEADDRNOTAVAIL   10049
#define WSAENETDOWN        10050
#define WSAENETUNREACH     10051
#define WSAECONNABORTED    10053
#define WSAECONNRESET      10054
#define WSAENOBUFS         10055
#define WSAEISCONN         10056
#define WSAENOTCONN        10057
#define WSAETIMEDOUT       10060
#define WSAECONNREFUSED    10061
#define WSANOTINITIALISED  10093

/* ── Winsock Structures ──────────────────────────────────────── */

typedef struct {
    int16_t  sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char     sin_zero[8];
} ws_sockaddr_in_t;

typedef struct {
    int16_t  sa_family;
    char     sa_data[14];
} ws_sockaddr_t;

typedef struct {
    char    *h_name;
    char   **h_aliases;
    int16_t  h_addrtype;
    int16_t  h_length;
    char   **h_addr_list;
} ws_hostent_t;

typedef struct ws_addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    int              ai_addrlen;
    char            *ai_canonname;
    ws_sockaddr_in_t *ai_addr;
    struct ws_addrinfo *ai_next;
} ws_addrinfo_t;

typedef struct {
    uint32_t fd_count;
    uint32_t fd_array[16];
} ws_fd_set_t;

typedef struct {
    int32_t tv_sec;
    int32_t tv_usec;
} ws_timeval_t;

typedef struct {
    WORD    wVersion;
    WORD    wHighVersion;
    char    szDescription[257];
    char    szSystemStatus[129];
    uint16_t iMaxSockets;
    uint16_t iMaxUdpDg;
    char    *lpVendorInfo;
} WSADATA;

/* ── Handle Mapping ──────────────────────────────────────────── */

#define SOCK_HANDLE_BASE 0x100

static uint32_t fd_to_sock(int fd) {
    return (uint32_t)(fd + SOCK_HANDLE_BASE);
}

static int sock_to_fd(uint32_t s) {
    return (int)(s - SOCK_HANDLE_BASE);
}

/* ── Error State ─────────────────────────────────────────────── */

static int wsa_last_error = 0;
static int wsa_initialized = 0;

/* ── WSA Init/Cleanup ────────────────────────────────────────── */

static int WINAPI shim_WSAStartup(WORD wVersionRequested, WSADATA *lpWSAData) {
    (void)wVersionRequested;
    if (lpWSAData) {
        memset(lpWSAData, 0, sizeof(WSADATA));
        lpWSAData->wVersion     = 0x0202;  /* 2.2 */
        lpWSAData->wHighVersion = 0x0202;
        strcpy(lpWSAData->szDescription, "ImposOS Winsock 2.2");
        strcpy(lpWSAData->szSystemStatus, "Running");
        lpWSAData->iMaxSockets = MAX_SOCKETS;
        lpWSAData->iMaxUdpDg   = 1472;
        lpWSAData->lpVendorInfo = NULL;
    }
    wsa_initialized = 1;
    wsa_last_error = 0;
    return 0;
}

static int WINAPI shim_WSACleanup(void) {
    wsa_initialized = 0;
    return 0;
}

static int WINAPI shim_WSAGetLastError(void) {
    return wsa_last_error;
}

static void WINAPI shim_WSASetLastError(int iError) {
    wsa_last_error = iError;
}

/* ── Socket Operations ───────────────────────────────────────── */

static uint32_t WINAPI shim_socket(int af, int type, int protocol) {
    (void)af;
    (void)protocol;

    int imp_type;
    if (type == WS_SOCK_STREAM)
        imp_type = SOCK_STREAM;
    else if (type == WS_SOCK_DGRAM)
        imp_type = SOCK_DGRAM;
    else {
        wsa_last_error = WSAESOCKTNOSUPPORT;
        return INVALID_SOCKET;
    }

    int fd = socket_create(imp_type);
    if (fd < 0) {
        wsa_last_error = WSAEMFILE;
        return INVALID_SOCKET;
    }
    return fd_to_sock(fd);
}

static int WINAPI shim_closesocket(uint32_t s) {
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    socket_close(fd);
    return 0;
}

static int WINAPI shim_bind(uint32_t s, const ws_sockaddr_in_t *addr, int namelen) {
    (void)namelen;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    if (!addr) {
        wsa_last_error = WSAEFAULT;
        return SOCKET_ERROR;
    }
    uint16_t port = ntohs(addr->sin_port);
    int ret = socket_bind(fd, port);
    if (ret < 0) {
        wsa_last_error = WSAEADDRINUSE;
        return SOCKET_ERROR;
    }
    return 0;
}

static int WINAPI shim_listen(uint32_t s, int backlog) {
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    int ret = socket_listen(fd, backlog);
    if (ret < 0) {
        wsa_last_error = WSAEINVAL;
        return SOCKET_ERROR;
    }
    return 0;
}

static uint32_t WINAPI shim_accept(uint32_t s, ws_sockaddr_in_t *addr, int *addrlen) {
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return INVALID_SOCKET;
    }
    int new_fd = socket_accept(fd);
    if (new_fd < 0) {
        wsa_last_error = WSAECONNREFUSED;
        return INVALID_SOCKET;
    }
    if (addr && addrlen && *addrlen >= (int)sizeof(ws_sockaddr_in_t)) {
        memset(addr, 0, sizeof(ws_sockaddr_in_t));
        addr->sin_family = AF_INET;
    }
    return fd_to_sock(new_fd);
}

static int WINAPI shim_connect(uint32_t s, const ws_sockaddr_in_t *addr, int namelen) {
    (void)namelen;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    if (!addr) {
        wsa_last_error = WSAEFAULT;
        return SOCKET_ERROR;
    }

    uint32_t ip_n = addr->sin_addr;
    uint8_t ip[4];
    ip[0] = (ip_n >>  0) & 0xFF;
    ip[1] = (ip_n >>  8) & 0xFF;
    ip[2] = (ip_n >> 16) & 0xFF;
    ip[3] = (ip_n >> 24) & 0xFF;
    uint16_t port = ntohs(addr->sin_port);

    int ret = socket_connect(fd, ip, port);
    if (ret < 0) {
        wsa_last_error = WSAECONNREFUSED;
        return SOCKET_ERROR;
    }
    return 0;
}

/* ── Data Transfer ───────────────────────────────────────────── */

static int WINAPI shim_send(uint32_t s, const char *buf, int len, int flags) {
    (void)flags;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    int ret = socket_send(fd, buf, (size_t)len);
    if (ret < 0) {
        wsa_last_error = WSAECONNRESET;
        return SOCKET_ERROR;
    }
    return ret;
}

static int WINAPI shim_recv(uint32_t s, char *buf, int len, int flags) {
    (void)flags;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    int ret = socket_recv(fd, buf, (size_t)len, 5000);
    if (ret < 0) {
        wsa_last_error = WSAETIMEDOUT;
        return SOCKET_ERROR;
    }
    return ret;
}

static int WINAPI shim_sendto(uint32_t s, const char *buf, int len, int flags,
                               const ws_sockaddr_in_t *to, int tolen) {
    (void)flags;
    (void)tolen;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    if (!to) {
        wsa_last_error = WSAEFAULT;
        return SOCKET_ERROR;
    }

    uint32_t ip_n = to->sin_addr;
    uint8_t ip[4];
    ip[0] = (ip_n >>  0) & 0xFF;
    ip[1] = (ip_n >>  8) & 0xFF;
    ip[2] = (ip_n >> 16) & 0xFF;
    ip[3] = (ip_n >> 24) & 0xFF;
    uint16_t port = ntohs(to->sin_port);

    int ret = socket_sendto(fd, buf, (size_t)len, ip, port);
    if (ret < 0) {
        wsa_last_error = WSAENETUNREACH;
        return SOCKET_ERROR;
    }
    return ret;
}

static int WINAPI shim_recvfrom(uint32_t s, char *buf, int len, int flags,
                                 ws_sockaddr_in_t *from, int *fromlen) {
    (void)flags;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }

    uint8_t src_ip[4] = {0};
    uint16_t src_port = 0;
    size_t recv_len = (size_t)len;

    int ret = socket_recvfrom(fd, buf, &recv_len, src_ip, &src_port, 5000);
    if (ret < 0) {
        wsa_last_error = WSAETIMEDOUT;
        return SOCKET_ERROR;
    }

    if (from && fromlen && *fromlen >= (int)sizeof(ws_sockaddr_in_t)) {
        memset(from, 0, sizeof(ws_sockaddr_in_t));
        from->sin_family = AF_INET;
        from->sin_port   = htons(src_port);
        from->sin_addr   = (uint32_t)src_ip[0]       |
                           ((uint32_t)src_ip[1] << 8) |
                           ((uint32_t)src_ip[2] << 16)|
                           ((uint32_t)src_ip[3] << 24);
    }
    return (int)recv_len;
}

/* ── select (simplified) ─────────────────────────────────────── */

static int WINAPI shim_select(int nfds, ws_fd_set_t *readfds,
                               ws_fd_set_t *writefds,
                               ws_fd_set_t *exceptfds,
                               const ws_timeval_t *timeout) {
    (void)nfds;
    (void)exceptfds;

    /* Simplified: report all fds as ready */
    int ready = 0;
    if (readfds)  ready += (int)readfds->fd_count;
    if (writefds) ready += (int)writefds->fd_count;

    /* If timeout is set, sleep for the duration */
    if (timeout && ready == 0) {
        /* Convert to ms; if both zero, just poll */
        uint32_t ms = (uint32_t)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
        if (ms > 0 && ms < 30000) {
            /* Small delay to avoid busy spin; actual sleep not available here */
        }
        return 0;
    }
    return ready;
}

/* ── Name Resolution ─────────────────────────────────────────── */

/* Static storage for gethostbyname result */
static ws_hostent_t  static_hostent;
static char          static_hostname[64];
static uint32_t      static_addr;
static char         *static_addr_list[2];
static char         *static_aliases[1];

static ws_hostent_t * WINAPI shim_gethostbyname(const char *name) {
    if (!name) {
        wsa_last_error = WSAEFAULT;
        return NULL;
    }

    uint8_t ip[4];
    if (dns_resolve(name, ip) < 0) {
        wsa_last_error = 11001;  /* WSAHOST_NOT_FOUND */
        return NULL;
    }

    strncpy(static_hostname, name, sizeof(static_hostname) - 1);
    static_hostname[sizeof(static_hostname) - 1] = '\0';

    static_addr = (uint32_t)ip[0]       |
                  ((uint32_t)ip[1] << 8) |
                  ((uint32_t)ip[2] << 16)|
                  ((uint32_t)ip[3] << 24);

    static_addr_list[0] = (char *)&static_addr;
    static_addr_list[1] = NULL;
    static_aliases[0]   = NULL;

    static_hostent.h_name      = static_hostname;
    static_hostent.h_aliases   = static_aliases;
    static_hostent.h_addrtype  = AF_INET;
    static_hostent.h_length    = 4;
    static_hostent.h_addr_list = static_addr_list;

    return &static_hostent;
}

static int WINAPI shim_gethostname(char *name, int namelen) {
    if (!name || namelen <= 0) {
        wsa_last_error = WSAEFAULT;
        return SOCKET_ERROR;
    }
    const char *hn = hostname_get();
    if (!hn) hn = "impospc";
    strncpy(name, hn, (size_t)namelen - 1);
    name[namelen - 1] = '\0';
    return 0;
}

static int WINAPI shim_getaddrinfo(const char *node, const char *service,
                                    const ws_addrinfo_t *hints,
                                    ws_addrinfo_t **res) {
    (void)service;
    (void)hints;

    if (!node || !res) {
        wsa_last_error = WSAEFAULT;
        return WSAEINVAL;
    }

    /* Resolve localhost specially */
    uint8_t ip[4] = {127, 0, 0, 1};
    if (strcmp(node, "localhost") != 0 && strcmp(node, "127.0.0.1") != 0) {
        if (dns_resolve(node, ip) < 0) {
            wsa_last_error = 11001;  /* WSAHOST_NOT_FOUND */
            return 11001;
        }
    }

    /* Allocate result chain: one addrinfo + one sockaddr_in */
    ws_addrinfo_t *ai = (ws_addrinfo_t *)malloc(sizeof(ws_addrinfo_t));
    if (!ai) return WSAENOBUFS;

    ws_sockaddr_in_t *sa = (ws_sockaddr_in_t *)malloc(sizeof(ws_sockaddr_in_t));
    if (!sa) { free(ai); return WSAENOBUFS; }

    memset(sa, 0, sizeof(ws_sockaddr_in_t));
    sa->sin_family = AF_INET;
    sa->sin_port   = 0;
    sa->sin_addr   = (uint32_t)ip[0]       |
                     ((uint32_t)ip[1] << 8) |
                     ((uint32_t)ip[2] << 16)|
                     ((uint32_t)ip[3] << 24);

    memset(ai, 0, sizeof(ws_addrinfo_t));
    ai->ai_flags    = 0;
    ai->ai_family   = AF_INET;
    ai->ai_socktype = WS_SOCK_STREAM;
    ai->ai_protocol = IPPROTO_TCP;
    ai->ai_addrlen  = sizeof(ws_sockaddr_in_t);
    ai->ai_addr     = sa;
    ai->ai_canonname = NULL;
    ai->ai_next     = NULL;

    *res = ai;
    return 0;
}

static void WINAPI shim_freeaddrinfo(ws_addrinfo_t *ai) {
    while (ai) {
        ws_addrinfo_t *next = ai->ai_next;
        if (ai->ai_addr)      free(ai->ai_addr);
        if (ai->ai_canonname)  free(ai->ai_canonname);
        free(ai);
        ai = next;
    }
}

/* ── Socket Options (stubs) ──────────────────────────────────── */

static int WINAPI shim_setsockopt(uint32_t s, int level, int optname,
                                   const char *optval, int optlen) {
    (void)s; (void)level; (void)optname; (void)optval; (void)optlen;
    return 0;
}

static int WINAPI shim_getsockopt(uint32_t s, int level, int optname,
                                   char *optval, int *optlen) {
    (void)s; (void)level; (void)optname;
    if (optval && optlen && *optlen >= 4) {
        memset(optval, 0, (size_t)*optlen);
    }
    return 0;
}

static int WINAPI shim_ioctlsocket(uint32_t s, int32_t cmd, uint32_t *argp) {
    (void)s; (void)cmd; (void)argp;
    return 0;
}

/* ── Byte Order Functions ────────────────────────────────────── */

static uint16_t WINAPI shim_htons(uint16_t hostshort) {
    return htons(hostshort);
}

static uint16_t WINAPI shim_ntohs(uint16_t netshort) {
    return ntohs(netshort);
}

static uint32_t WINAPI shim_htonl(uint32_t hostlong) {
    return htonl(hostlong);
}

static uint32_t WINAPI shim_ntohl(uint32_t netlong) {
    return ntohl(netlong);
}

/* ── inet_addr / inet_ntoa ───────────────────────────────────── */

static uint32_t WINAPI shim_inet_addr(const char *cp) {
    if (!cp) return INVALID_SOCKET;

    uint32_t parts[4] = {0};
    int idx = 0;
    const char *p = cp;

    while (*p && idx < 4) {
        uint32_t val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (uint32_t)(*p - '0');
            p++;
        }
        parts[idx++] = val;
        if (*p == '.') p++;
    }

    if (idx != 4) return 0xFFFFFFFF;  /* INADDR_NONE */

    return (parts[0])       |
           (parts[1] << 8)  |
           (parts[2] << 16) |
           (parts[3] << 24);
}

static char inet_ntoa_buf[16];

static char * WINAPI shim_inet_ntoa(uint32_t in_addr) {
    uint8_t *b = (uint8_t *)&in_addr;
    snprintf(inet_ntoa_buf, sizeof(inet_ntoa_buf), "%d.%d.%d.%d",
             b[0], b[1], b[2], b[3]);
    return inet_ntoa_buf;
}

/* ── Shutdown ────────────────────────────────────────────────── */

static int WINAPI shim_shutdown(uint32_t s, int how) {
    (void)how;
    int fd = sock_to_fd(s);
    if (fd < 0 || fd >= MAX_SOCKETS) {
        wsa_last_error = WSAENOTSOCK;
        return SOCKET_ERROR;
    }
    socket_close(fd);
    return 0;
}

/* ── getpeername / getsockname (stubs) ───────────────────────── */

static int WINAPI shim_getpeername(uint32_t s, ws_sockaddr_in_t *name, int *namelen) {
    (void)s;
    if (name && namelen && *namelen >= (int)sizeof(ws_sockaddr_in_t)) {
        memset(name, 0, sizeof(ws_sockaddr_in_t));
        name->sin_family = AF_INET;
    }
    return 0;
}

static int WINAPI shim_getsockname(uint32_t s, ws_sockaddr_in_t *name, int *namelen) {
    (void)s;
    if (name && namelen && *namelen >= (int)sizeof(ws_sockaddr_in_t)) {
        memset(name, 0, sizeof(ws_sockaddr_in_t));
        name->sin_family = AF_INET;
    }
    return 0;
}

/* ── Export Table ─────────────────────────────────────────────── */

static const win32_export_entry_t ws2_32_exports[] = {
    /* Init/Cleanup */
    { "WSAStartup",       (void *)shim_WSAStartup },
    { "WSACleanup",       (void *)shim_WSACleanup },
    { "WSAGetLastError",  (void *)shim_WSAGetLastError },
    { "WSASetLastError",  (void *)shim_WSASetLastError },

    /* Socket lifecycle */
    { "socket",           (void *)shim_socket },
    { "closesocket",      (void *)shim_closesocket },
    { "bind",             (void *)shim_bind },
    { "listen",           (void *)shim_listen },
    { "accept",           (void *)shim_accept },
    { "connect",          (void *)shim_connect },
    { "shutdown",         (void *)shim_shutdown },

    /* Data transfer */
    { "send",             (void *)shim_send },
    { "recv",             (void *)shim_recv },
    { "sendto",           (void *)shim_sendto },
    { "recvfrom",         (void *)shim_recvfrom },

    /* Multiplexing */
    { "select",           (void *)shim_select },

    /* Name resolution */
    { "gethostbyname",    (void *)shim_gethostbyname },
    { "gethostname",      (void *)shim_gethostname },
    { "getaddrinfo",      (void *)shim_getaddrinfo },
    { "freeaddrinfo",     (void *)shim_freeaddrinfo },

    /* Socket options */
    { "setsockopt",       (void *)shim_setsockopt },
    { "getsockopt",       (void *)shim_getsockopt },
    { "ioctlsocket",      (void *)shim_ioctlsocket },

    /* Byte order */
    { "htons",            (void *)shim_htons },
    { "ntohs",            (void *)shim_ntohs },
    { "htonl",            (void *)shim_htonl },
    { "ntohl",            (void *)shim_ntohl },

    /* Address conversion */
    { "inet_addr",        (void *)shim_inet_addr },
    { "inet_ntoa",        (void *)shim_inet_ntoa },

    /* Peer/socket name */
    { "getpeername",      (void *)shim_getpeername },
    { "getsockname",      (void *)shim_getsockname },
};

const win32_dll_shim_t win32_ws2_32 = {
    .dll_name    = "ws2_32.dll",
    .exports     = ws2_32_exports,
    .num_exports = sizeof(ws2_32_exports) / sizeof(ws2_32_exports[0]),
};
