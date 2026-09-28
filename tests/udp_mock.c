#include "tests/udp_mock.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    bool present;
    char host[UDP_MAX_HOST];
    int port;
    uint8_t *data;
    int length;
} MockDatagram;

struct UdpContext
{
    int id;
    bool active[UDP_MAX_SOCKETS];
    int local_ports[UDP_MAX_SOCKETS];
    MockDatagram pending[UDP_MAX_SOCKETS];
    char last_send_host[UDP_MAX_HOST];
    int last_send_port;
    uint8_t *last_send_data;
    int last_send_length;
    int send_count;
    char error[256];
};

static int next_context_id;
static int live_contexts;
static int destroyed_contexts;
static UdpContext *live_context_ptrs[32];

static void mock_datagram_clear(MockDatagram *datagram)
{
    if (!datagram)
        return;
    free(datagram->data);
    memset(datagram, 0, sizeof(*datagram));
}

static void mock_set_error(UdpContext *ctx, const char *message)
{
    if (!ctx || !message)
        return;
    strncpy(ctx->error, message, sizeof(ctx->error) - 1);
    ctx->error[sizeof(ctx->error) - 1] = '\0';
}

void udp_mock_reset(void)
{
    assert(live_contexts == 0);
    next_context_id = 0;
    destroyed_contexts = 0;
    memset(live_context_ptrs, 0, sizeof(live_context_ptrs));
}

UdpContext *udp_create(void)
{
    UdpContext *ctx = (UdpContext *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->id = ++next_context_id;
    for (size_t index = 0;
         index < sizeof(live_context_ptrs) / sizeof(live_context_ptrs[0]);
         index++)
    {
        if (!live_context_ptrs[index])
        {
            live_context_ptrs[index] = ctx;
            break;
        }
    }
    live_contexts++;
    return ctx;
}

void udp_destroy(UdpContext *ctx)
{
    if (!ctx)
        return;
    for (size_t index = 0;
         index < sizeof(live_context_ptrs) / sizeof(live_context_ptrs[0]);
         index++)
    {
        if (live_context_ptrs[index] == ctx)
        {
            live_context_ptrs[index] = NULL;
            break;
        }
    }
    for (int handle = 0; handle < UDP_MAX_SOCKETS; handle++)
        mock_datagram_clear(&ctx->pending[handle]);
    free(ctx->last_send_data);
    free(ctx);
    live_contexts--;
    destroyed_contexts++;
}

int udp_bind(UdpContext *ctx, int port)
{
    if (!ctx || port < 0 || port > 65535)
        return -1;
    for (int handle = 0; handle < UDP_MAX_SOCKETS; handle++)
    {
        if (ctx->active[handle])
            continue;
        ctx->active[handle] = true;
        ctx->local_ports[handle] = port == 0
                                       ? 10000 + ctx->id * 100 + handle
                                       : port;
        return handle;
    }
    mock_set_error(ctx, "No free UDP socket slots");
    return -1;
}

int udp_get_port(UdpContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS ||
        !ctx->active[handle])
        return -1;
    return ctx->local_ports[handle];
}

bool udp_send(UdpContext *ctx, int handle, const char *host, int port,
              const uint8_t *data, int length)
{
    uint8_t *copy;

    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS ||
        !ctx->active[handle] || !host || port < 1 || port > 65535 ||
        !data || length <= 0 || length > UDP_MAX_PACKET)
        return false;
    copy = (uint8_t *)malloc((size_t)length);
    if (!copy)
        return false;
    memcpy(copy, data, (size_t)length);
    free(ctx->last_send_data);
    ctx->last_send_data = copy;
    ctx->last_send_length = length;
    ctx->last_send_port = port;
    strncpy(ctx->last_send_host, host, sizeof(ctx->last_send_host) - 1);
    ctx->last_send_host[sizeof(ctx->last_send_host) - 1] = '\0';
    ctx->send_count++;
    return true;
}

int udp_recv(UdpContext *ctx, int handle, uint8_t *buf, int buf_len,
             UdpDatagram *dgram)
{
    MockDatagram *pending;
    int length;

    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS ||
        !ctx->active[handle] || !buf || buf_len <= 0)
        return -1;
    pending = &ctx->pending[handle];
    if (!pending->present)
        return 0;
    if (pending->length > buf_len)
    {
        mock_set_error(ctx, "UDP receive buffer is too small; datagram was not consumed");
        return UDP_RECV_TRUNCATED;
    }

    length = pending->length;
    memcpy(buf, pending->data, (size_t)length);
    if (dgram)
    {
        strncpy(dgram->host, pending->host, sizeof(dgram->host) - 1);
        dgram->host[sizeof(dgram->host) - 1] = '\0';
        dgram->port = pending->port;
        dgram->data = buf;
        dgram->length = length;
    }
    mock_datagram_clear(pending);
    return length;
}

void udp_close(UdpContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS)
        return;
    ctx->active[handle] = false;
    ctx->local_ports[handle] = 0;
    mock_datagram_clear(&ctx->pending[handle]);
}

const char *udp_get_error(UdpContext *ctx)
{
    return ctx ? ctx->error : "Invalid context";
}

bool udp_mock_enqueue(UdpContext *ctx, int handle, const char *host, int port,
                      const uint8_t *data, int length)
{
    MockDatagram *pending;

    if (!ctx || handle < 0 || handle >= UDP_MAX_SOCKETS ||
        !ctx->active[handle] || !host || !data || length <= 0 ||
        length > UDP_MAX_PACKET)
        return false;
    pending = &ctx->pending[handle];
    if (pending->present)
        return false;
    pending->data = (uint8_t *)malloc((size_t)length);
    if (!pending->data)
        return false;
    memcpy(pending->data, data, (size_t)length);
    strncpy(pending->host, host, sizeof(pending->host) - 1);
    pending->host[sizeof(pending->host) - 1] = '\0';
    pending->port = port;
    pending->length = length;
    pending->present = true;
    return true;
}

int udp_mock_live_count(void)
{
    return live_contexts;
}

int udp_mock_destroy_count(void)
{
    return destroyed_contexts;
}

bool udp_mock_is_live(UdpContext *ctx)
{
    for (size_t index = 0;
         index < sizeof(live_context_ptrs) / sizeof(live_context_ptrs[0]);
         index++)
    {
        if (live_context_ptrs[index] == ctx)
            return true;
    }
    return false;
}

int udp_mock_send_count(UdpContext *ctx)
{
    return ctx ? ctx->send_count : 0;
}

bool udp_mock_last_send_equals(UdpContext *ctx, const char *host, int port,
                               const uint8_t *data, int length)
{
    return ctx && host && data && length == ctx->last_send_length &&
           port == ctx->last_send_port &&
           strcmp(host, ctx->last_send_host) == 0 &&
           memcmp(data, ctx->last_send_data, (size_t)length) == 0;
}