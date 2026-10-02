#include "udp_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET udp_socket_t;
#define UDP_INVALID_SOCKET INVALID_SOCKET
#define UDP_SOCKET_ERROR SOCKET_ERROR
static bool g_winsock_initialized = false;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
typedef int udp_socket_t;
#define UDP_INVALID_SOCKET (-1)
#define UDP_SOCKET_ERROR (-1)
#endif

typedef struct
{
    bool active;
    udp_socket_t sock;
    int local_port;
} UdpSocket;

struct UdpContext
{
    UdpSocket sockets[UDP_MAX_SOCKETS];
    uint8_t peek_buffer[UDP_MAX_PACKET];
    char error_msg[256];
};

static void set_error(UdpContext *ctx, const char *msg)
{
    if (ctx && msg)
    {
        strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
        ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
    }
}

static bool set_nonblocking(udp_socket_t sock)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void close_socket(udp_socket_t sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

UdpContext *udp_create(void)
{
#ifdef _WIN32
    if (!g_winsock_initialized)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return NULL;
        g_winsock_initialized = true;
    }
#endif

    UdpContext *ctx = (UdpContext *)calloc(1, sizeof(UdpContext));
    if (!ctx)
        return NULL;

    for (int i = 0; i < UDP_MAX_SOCKETS; i++)
    {
        ctx->sockets[i].sock = UDP_INVALID_SOCKET;
    }

    return ctx;
}

void udp_destroy(UdpContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < UDP_MAX_SOCKETS; i++)
    {
        if (ctx->sockets[i].active)
        {
            close_socket(ctx->sockets[i].sock);
            ctx->sockets[i].active = false;
        }
    }

    free(ctx);
}

int udp_bind(UdpContext *ctx, int port)
{
    if (!ctx)
        return -1;
    if (port < 0 || port > 65535)
    {
        set_error(ctx, "Invalid port number");
        return -1;
    }

    int handle = -1;
    for (int i = 0; i < UDP_MAX_SOCKETS; i++)
    {
        if (!ctx->sockets[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
    {
        set_error(ctx, "No free UDP socket slots");
        return -1;
    }

    udp_socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == UDP_INVALID_SOCKET)
    {
        set_error(ctx, "Failed to create UDP socket");
        return -1;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        set_error(ctx, "Failed to bind UDP socket");
        close_socket(sock);
        return -1;
    }

    if (port == 0)
    {
        struct sockaddr_in bound_addr;
        socklen_t addr_len = sizeof(bound_addr);
        if (getsockname(sock, (struct sockaddr *)&bound_addr, &addr_len) == 0)
        {
            port = ntohs(bound_addr.sin_port);
        }
    }

    if (!set_nonblocking(sock))
    {
        set_error(ctx, "Failed to set non-blocking mode");
        close_socket(sock);
        return -1;
    }

    UdpSocket *s = &ctx->sockets[handle];
    s->active = true;
    s->sock = sock;
    s->local_port = port;

    return handle;
}

int udp_get_port(UdpContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS)
        return -1;
    if (!ctx->sockets[handle].active)
        return -1;
    return ctx->sockets[handle].local_port;
}

bool udp_send(UdpContext *ctx, int handle, const char *host, int port,
              const uint8_t *data, int length)
{
    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS)
        return false;
    if (!ctx->sockets[handle].active)
        return false;
    if (!host || !data || length <= 0 || length > UDP_MAX_PACKET)
        return false;
    if (port < 1 || port > 65535)
        return false;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0)
    {
        set_error(ctx, "Failed to resolve host");
        return false;
    }

    int sent = sendto(ctx->sockets[handle].sock, (const char *)data, length, 0,
                      result->ai_addr, (socklen_t)result->ai_addrlen);
    freeaddrinfo(result);

    if (sent != length)
    {
        set_error(ctx, "sendto failed");
        return false;
    }

    return true;
}

int udp_recv(UdpContext *ctx, int handle, uint8_t *buf, int buf_len,
             UdpDatagram *dgram)
{
    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS)
        return -1;
    if (!ctx->sockets[handle].active)
        return -1;
    if (!buf || buf_len <= 0)
        return -1;

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    memset(&from_addr, 0, sizeof(from_addr));

    int pending_length = recvfrom(ctx->sockets[handle].sock,
                                  (char *)ctx->peek_buffer, UDP_MAX_PACKET, MSG_PEEK,
                                  (struct sockaddr *)&from_addr, &from_len);
    if (pending_length < 0)
    {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
#endif
        set_error(ctx, "Failed to inspect pending UDP datagram");
        return -1;
    }
    if (pending_length > buf_len)
    {
        set_error(ctx, "UDP receive buffer is too small; datagram was not consumed");
        return UDP_RECV_TRUNCATED;
    }

    from_len = sizeof(from_addr);

    int received = recvfrom(ctx->sockets[handle].sock, (char *)buf, buf_len, 0,
                            (struct sockaddr *)&from_addr, &from_len);

    if (received < 0)
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
            return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
#endif
        set_error(ctx, "UDP receive failed");
        return -1;
    }

    if (dgram)
    {
        inet_ntop(AF_INET, &from_addr.sin_addr, dgram->host, UDP_MAX_HOST);
        dgram->port = ntohs(from_addr.sin_port);
        dgram->data = buf;
        dgram->length = received;
    }

    return received;
}

void udp_close(UdpContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS)
        return;

    UdpSocket *s = &ctx->sockets[handle];
    if (!s->active)
        return;

    close_socket(s->sock);
    s->active = false;
    s->sock = UDP_INVALID_SOCKET;
}

const char *udp_get_error(UdpContext *ctx)
{
    return ctx ? ctx->error_msg : "Invalid context";
}