#include "network_wrapper.h"
#include "network_request_core.h"
#if !defined(BUDO_ANDROID) && !defined(BUDO_WEB)
#include "network_desktop_transport.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#if !defined(BUDO_WEB)
#include "core/platform_thread.h"
#endif

#ifdef BUDO_ANDROID
#include "android_network.h"
#elif !defined(BUDO_WEB)
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strdup _strdup
#else
#include <strings.h>
#endif
#endif 

#if defined(_MSC_VER)
#define BUDO_THREAD_LOCAL __declspec(thread)
#else
#define BUDO_THREAD_LOCAL _Thread_local
#endif

#if !defined(BUDO_ANDROID) && !defined(BUDO_WEB) && defined(_WIN32)
static char *network_strndup(const char *text, size_t length)
{
    char *copy = malloc(length + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}
#define strndup network_strndup

static char *network_strcasestr(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    if (length == 0)
        return (char *)text;
    for (; *text; text++)
        if (_strnicmp(text, needle, length) == 0)
            return (char *)text;
    return NULL;
}
#define strcasestr network_strcasestr
#endif

typedef struct
{
    
    char method[16];
    char *url;
    NetworkHeader *headers;
    int header_count;
    uint8_t *body;
    size_t body_len;

    bool active;
    int id;

#if !defined(BUDO_WEB)
    BudoThread thread;
    BudoMutex mutex;
#endif

    volatile bool completed;
    bool cancel_requested;
    intptr_t active_socket;
    NetworkResponse *response;
    char error[512];

    NetworkAsyncCallback callback;
    void *user_data;

    struct NetworkContext *ctx;
} AsyncSlot;

struct NetworkContext
{
    NetworkPolicy policy;
    char error[512];
    AsyncSlot async_slots[NETWORK_MAX_ASYNC];
    int next_async_id;
    bool shutting_down;
#ifdef BUDO_WEB
    uint32_t web_generation;
#endif
};

static BUDO_THREAD_LOCAL char *g_request_error = NULL;
static BUDO_THREAD_LOCAL size_t g_request_error_size = 0;
#if !defined(BUDO_WEB)
static BUDO_THREAD_LOCAL uint64_t g_request_deadline_ms = 0;
static BUDO_THREAD_LOCAL AsyncSlot *g_request_slot = NULL;

static uint64_t network_monotonic_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static uint32_t network_remaining_ms(void)
{
    uint64_t now = network_monotonic_ms();
    if (g_request_deadline_ms == 0 || now == 0)
        return NETWORK_REQUEST_TIMEOUT_MS;
    if (now >= g_request_deadline_ms)
        return 0;
    uint64_t remaining = g_request_deadline_ms - now;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

static bool network_request_cancelled(void)
{
    if (!g_request_slot)
        return false;
    bool cancelled;
    budo_mutex_lock(&g_request_slot->mutex);
    cancelled = g_request_slot->cancel_requested;
    budo_mutex_unlock(&g_request_slot->mutex);
    return cancelled;
}

#if !defined(BUDO_ANDROID)
static uint32_t desktop_transport_remaining_ms(void *user_data)
{
    (void)user_data;
    return network_remaining_ms();
}

static bool desktop_transport_cancelled(void *user_data)
{
    (void)user_data;
    return network_request_cancelled();
}

static void desktop_transport_publish_socket(void *user_data, intptr_t socket_handle)
{
    AsyncSlot *slot = user_data;
    if (!slot)
        return;
    budo_mutex_lock(&slot->mutex);
    slot->active_socket = socket_handle;
    budo_mutex_unlock(&slot->mutex);
}

static bool desktop_transport_release_socket(void *user_data, intptr_t socket_handle)
{
    AsyncSlot *slot = user_data;
    if (!slot)
        return true;
    bool owned = false;
    budo_mutex_lock(&slot->mutex);
    if (slot->active_socket == socket_handle)
    {
        slot->active_socket = -1;
        owned = true;
    }
    budo_mutex_unlock(&slot->mutex);
    return owned;
}
#endif
#endif

static char *network_error_buffer(NetworkContext *ctx, size_t *size)
{
    if (g_request_error)
    {
        *size = g_request_error_size;
        return g_request_error;
    }
    *size = sizeof(ctx->error);
    return ctx->error;
}

#define NETWORK_SET_ERROR(ctx, ...)                                     \
    do                                                                  \
    {                                                                   \
        size_t error_size__;                                            \
        char *error_buf__ = network_error_buffer((ctx), &error_size__); \
        snprintf(error_buf__, error_size__, __VA_ARGS__);               \
    } while (0)

static bool extract_host(const char *url, char *host, size_t host_size,
                         int *port, bool *is_https)
{
    *is_https = false;
    *port = 80;

    const char *p = url;

    if (strncmp(p, "https://", 8) == 0)
    {
        *is_https = true;
        *port = 443;
        p += 8;
    }
    else if (strncmp(p, "http://", 7) == 0)
    {
        p += 7;
    }
    else
    {
        return false;
    }

    const char *host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '?')
        p++;

    size_t host_len = (size_t)(p - host_start);
    if (host_len == 0 || host_len >= host_size)
        return false;

    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    if (*p == ':')
    {
        p++;
        *port = atoi(p);
        if (*port <= 0 || *port > 65535)
            return false;
    }

    return true;
}

static bool is_url_allowed(NetworkContext *ctx, const char *url)
{
    if (!ctx)
        return false;
    size_t error_size;
    char *error = network_error_buffer(ctx, &error_size);
    return network_policy_allows_url(&ctx->policy, url, error, error_size);
}

static bool checked_add_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b)
        return false;
    *out = a + b;
    return true;
}

static bool validate_request(NetworkContext *ctx,
                             const char *method, const char *url,
                             const NetworkHeader *headers, int header_count,
                             const uint8_t *body, size_t body_len)
{
    if (!ctx)
        return false;
    size_t error_size;
    char *error = network_error_buffer(ctx, &error_size);
    return network_request_validate(method, sizeof(((AsyncSlot *)0)->method),
                                    url, headers, header_count, body, body_len,
                                    error, error_size);
}

static const char *response_header(const NetworkResponse *response, const char *name)
{
    for (int i = 0; response && i < response->header_count; i++)
        if (response->headers[i].name && strcasecmp(response->headers[i].name, name) == 0)
            return response->headers[i].value;
    return NULL;
}

#if !defined(BUDO_ANDROID) && !defined(BUDO_WEB)

typedef struct
{
    uint8_t *data;
    size_t len;
    size_t cap;
    bool limit_exceeded;
    bool allocation_failed;
} DynBuf;

static void dynbuf_init(DynBuf *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->limit_exceeded = false;
    buf->allocation_failed = false;
}

static bool dynbuf_append(DynBuf *buf, const uint8_t *data, size_t len, size_t limit)
{
    size_t required;
    if (!checked_add_size(buf->len, len, &required) || required > limit)
    {
        buf->limit_exceeded = true;
        return false;
    }
    size_t allocation_required;
    if (!checked_add_size(required, 1, &allocation_required))
    {
        buf->limit_exceeded = true;
        return false;
    }
    if (allocation_required > buf->cap)
    {
        size_t new_cap = buf->cap ? buf->cap * 2 : 4096;
        if (new_cap < buf->cap)
            new_cap = allocation_required;
        while (new_cap < allocation_required)
        {
            if (new_cap > (limit + 1) / 2)
            {
                new_cap = limit + 1;
                break;
            }
            new_cap *= 2;
        }
        uint8_t *new_data = realloc(buf->data, new_cap);
        if (!new_data)
        {
            buf->allocation_failed = true;
            return false;
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

static void dynbuf_free(DynBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->limit_exceeded = false;
    buf->allocation_failed = false;
}

static bool read_all_response(NetworkDesktopConnection *connection, DynBuf *buf)
{
    uint8_t chunk[8192];
    for (;;)
    {
        ptrdiff_t amount = network_desktop_transport_read(connection, chunk, sizeof(chunk));
        if (amount < 0)
            return false;
        if (amount == 0)
            break;
        if (!dynbuf_append(buf, chunk, (size_t)amount,
                           NETWORK_MAX_RESPONSE_HEADERS_SIZE + NETWORK_MAX_RESPONSE_BODY_SIZE))
            return false;
    }
    return true;
}

static const char *extract_path(const char *url)
{
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0)
        p += 8;
    else if (strncmp(p, "http://", 7) == 0)
        p += 7;

    const char *slash = strchr(p, '/');
    if (!slash)
        return "/";
    return slash;
}

static const uint8_t *parse_http_response(const uint8_t *data, size_t len,
                                          int *status, char *status_text, size_t status_text_size,
                                          NetworkHeader *headers, int *header_count,
                                          int max_headers)
{
    *status = 0;
    *header_count = 0;
    status_text[0] = '\0';

    const uint8_t *header_end = NULL;
    for (size_t i = 0; i + 3 < len; i++)
    {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n')
        {
            header_end = data + i + 4;
            break;
        }
    }
    if (!header_end)
        return NULL;

    const char *line = (const char *)data;
    const char *sp1 = strchr(line, ' ');
    if (!sp1)
        return NULL;
    sp1++;

    *status = atoi(sp1);

    const char *sp2 = strchr(sp1, ' ');
    if (sp2)
    {
        sp2++;
        const char *eol = strstr(sp2, "\r\n");
        if (eol)
        {
            size_t tlen = (size_t)(eol - sp2);
            if (tlen >= status_text_size)
                tlen = status_text_size - 1;
            memcpy(status_text, sp2, tlen);
            status_text[tlen] = '\0';
        }
    }

    const char *hdr = strstr(line, "\r\n");
    if (!hdr)
        return header_end;
    hdr += 2; 

    while (hdr < (const char *)header_end - 2 && *header_count < max_headers)
    {
        const char *eol = strstr(hdr, "\r\n");
        if (!eol || eol == hdr)
            break;

        const char *colon = memchr(hdr, ':', (size_t)(eol - hdr));
        if (colon)
        {
            size_t name_len = (size_t)(colon - hdr);
            const char *val = colon + 1;
            while (val < eol && *val == ' ')
                val++;
            size_t val_len = (size_t)(eol - val);

            headers[*header_count].name = strndup(hdr, name_len);
            headers[*header_count].value = strndup(val, val_len);
            (*header_count)++;
        }

        hdr = eol + 2;
    }

    return header_end;
}

static const char *find_header(NetworkHeader *headers, int count, const char *name)
{
    for (int i = 0; i < count; i++)
    {
        if (strcasecmp(headers[i].name, name) == 0)
            return headers[i].value;
    }
    return NULL;
}

static bool parse_chunked_body(const uint8_t *data, size_t len, DynBuf *out)
{
    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end)
    {
        
        char size_str[20];
        int si = 0;
        while (p < end && *p != '\r' && si < 19)
        {
            size_str[si++] = (char)*p;
            p++;
        }
        size_str[si] = '\0';

        if (p + 1 < end && *p == '\r' && *(p + 1) == '\n')
            p += 2;
        else
            break;

        unsigned long chunk_size = strtoul(size_str, NULL, 16);
        if (chunk_size == 0)
            break;

        if (chunk_size > (unsigned long)(end - p))
            return false;

        if (chunk_size > NETWORK_MAX_RESPONSE_BODY_SIZE ||
            !dynbuf_append(out, p, chunk_size, NETWORK_MAX_RESPONSE_BODY_SIZE))
            return false;

        p += chunk_size;

        if (p + 1 < end && *p == '\r' && *(p + 1) == '\n')
            p += 2;
    }

    return true;
}

#endif 

#ifdef BUDO_WEB

NetworkResponse *network_request(NetworkContext *ctx,
                                 const char *method,
                                 const char *url,
                                 const NetworkHeader *req_headers,
                                 int req_header_count,
                                 const uint8_t *req_body,
                                 size_t req_body_len)
{
    (void)method;
    (void)url;
    (void)req_headers;
    (void)req_header_count;
    (void)req_body;
    (void)req_body_len;

    if (ctx)
        NETWORK_SET_ERROR(ctx,
                          "Synchronous network_request() is not available on the web platform. "
                          "Use fetch() (async) instead.");
    return NULL;
}

#endif 

NetworkContext *network_create(const NetworkPolicy *policy)
{
#ifdef BUDO_WEB
    static uint32_t next_web_generation = 1;
#endif
    NetworkContext *ctx = calloc(1, sizeof(NetworkContext));
    if (!ctx)
        return NULL;

#if !defined(BUDO_WEB) && !defined(BUDO_ANDROID)
    if (!network_desktop_transport_init())
    {
        free(ctx);
        return NULL;
    }
#endif

    if (policy)
        ctx->policy = *policy;
#ifdef BUDO_WEB
    ctx->web_generation = next_web_generation++;
    if (ctx->web_generation == 0)
        ctx->web_generation = next_web_generation++;
#endif

    return ctx;
}

void network_destroy(NetworkContext *ctx)
{
    if (!ctx)
        return;
    network_shutdown(ctx);
    free(ctx);
}

bool network_is_enabled(NetworkContext *ctx)
{
    if (!ctx)
        return false;
    return ctx->policy.allow_all || ctx->policy.domain_count > 0;
}

#ifdef BUDO_ANDROID

static NetworkResponse *android_request_one_hop(NetworkContext *ctx,
                                                const char *method,
                                                const char *url,
                                                const NetworkHeader *req_headers,
                                                int req_header_count,
                                                const uint8_t *req_body,
                                                size_t req_body_len)
{
    size_t error_size;
    char *error_buf = network_error_buffer(ctx, &error_size);
    return android_network_perform_request(
        error_buf, error_size,
        method, url, req_headers, req_header_count,
        req_body, req_body_len, network_remaining_ms());
}

#elif !defined(BUDO_WEB) 

static NetworkResponse *desktop_request_one_hop(NetworkContext *ctx,
                                                const char *method,
                                                const char *url,
                                                const NetworkHeader *req_headers,
                                                int req_header_count,
                                                const uint8_t *req_body,
                                                size_t req_body_len)
{
    
    char host[256];
    int port;
    bool is_https;
    if (!extract_host(url, host, sizeof(host), &port, &is_https))
    {
        NETWORK_SET_ERROR(ctx, "Invalid URL format");
        return NULL;
    }

    NetworkDesktopTransportHooks hooks = {
        desktop_transport_remaining_ms,
        desktop_transport_cancelled,
        desktop_transport_publish_socket,
        desktop_transport_release_socket,
        g_request_slot};
    size_t error_size;
    char *error = network_error_buffer(ctx, &error_size);
    NetworkDesktopConnection *connection = network_desktop_transport_open(
        host, port, is_https, &hooks, error, error_size);
    if (!connection)
        return NULL;

    const char *path = extract_path(url);
    DynBuf request_buf;
    dynbuf_init(&request_buf);

    char line[2048];
    int line_len = snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\n", method, path);
    if (line_len < 0 || (size_t)line_len >= sizeof(line) ||
        !dynbuf_append(&request_buf, (const uint8_t *)line, (size_t)line_len,
                       NETWORK_MAX_REQUEST_HEADERS_SIZE))
        goto request_too_large;

    if (port == 80 || port == 443)
        line_len = snprintf(line, sizeof(line), "Host: %s\r\n", host);
    else
        line_len = snprintf(line, sizeof(line), "Host: %s:%d\r\n", host, port);
    if (line_len < 0 || (size_t)line_len >= sizeof(line) ||
        !dynbuf_append(&request_buf, (const uint8_t *)line, (size_t)line_len,
                       NETWORK_MAX_REQUEST_HEADERS_SIZE))
        goto request_too_large;

    if (!dynbuf_append(&request_buf, (const uint8_t *)"Connection: close\r\n", 19,
                       NETWORK_MAX_REQUEST_HEADERS_SIZE))
        goto request_too_large;

    if (!dynbuf_append(&request_buf, (const uint8_t *)"User-Agent: Budo/1.0\r\n", 22,
                       NETWORK_MAX_REQUEST_HEADERS_SIZE))
        goto request_too_large;

    bool has_content_type = false;
    for (int i = 0; i < req_header_count; i++)
    {
        if (strcasecmp(req_headers[i].name, "content-type") == 0)
            has_content_type = true;
        line_len = snprintf(line, sizeof(line), "%s: %s\r\n",
                            req_headers[i].name, req_headers[i].value);
        if (line_len < 0 || (size_t)line_len >= sizeof(line) ||
            !dynbuf_append(&request_buf, (const uint8_t *)line, (size_t)line_len,
                           NETWORK_MAX_REQUEST_HEADERS_SIZE))
            goto request_too_large;
    }

    if (req_body && req_body_len > 0)
    {
        line_len = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", req_body_len);
        if (line_len < 0 || (size_t)line_len >= sizeof(line) ||
            !dynbuf_append(&request_buf, (const uint8_t *)line, (size_t)line_len,
                           NETWORK_MAX_REQUEST_HEADERS_SIZE))
            goto request_too_large;
    }

    if (!dynbuf_append(&request_buf, (const uint8_t *)"\r\n", 2,
                       NETWORK_MAX_REQUEST_HEADERS_SIZE))
        goto request_too_large;

    if (!network_desktop_transport_write_all(connection, request_buf.data, request_buf.len))
    {
        NETWORK_SET_ERROR(ctx, "Failed to send request");
        dynbuf_free(&request_buf);
        network_desktop_transport_close(connection);
        return NULL;
    }
    dynbuf_free(&request_buf);

    if (req_body && req_body_len > 0)
    {
        if (!network_desktop_transport_write_all(connection, req_body, req_body_len))
        {
            NETWORK_SET_ERROR(ctx, "Failed to send request body");
            network_desktop_transport_close(connection);
            return NULL;
        }
    }

    DynBuf response_data;
    dynbuf_init(&response_data);

    if (!read_all_response(connection, &response_data))
    {
        if (response_data.limit_exceeded || response_data.allocation_failed)
        {
            NETWORK_SET_ERROR(ctx, "HTTP response exceeds configured size limits");
            dynbuf_free(&response_data);
            network_desktop_transport_close(connection);
            return NULL;
        }
        
        if (response_data.len == 0)
        {
            NETWORK_SET_ERROR(ctx, "Failed to read response");
            dynbuf_free(&response_data);
            network_desktop_transport_close(connection);
            return NULL;
        }
    }
    network_desktop_transport_close(connection);

    NetworkResponse *resp = calloc(1, sizeof(NetworkResponse));
    if (!resp)
    {
        dynbuf_free(&response_data);
        NETWORK_SET_ERROR(ctx, "Out of memory");
        return NULL;
    }

    char status_text[256];
    const uint8_t *body_start = parse_http_response(
        response_data.data, response_data.len,
        &resp->status, status_text, sizeof(status_text),
        resp->headers, &resp->header_count,
        NETWORK_MAX_HEADERS);

    if (!body_start || (size_t)(body_start - response_data.data) > NETWORK_MAX_RESPONSE_HEADERS_SIZE)
    {
        NETWORK_SET_ERROR(ctx, "Invalid or oversized HTTP response headers");
        network_response_free(resp);
        dynbuf_free(&response_data);
        return NULL;
    }
    for (int i = 0; i < resp->header_count; i++)
    {
        if (!resp->headers[i].name || !resp->headers[i].value)
        {
            NETWORK_SET_ERROR(ctx, "Out of memory while parsing HTTP response headers");
            network_response_free(resp);
            dynbuf_free(&response_data);
            return NULL;
        }
    }

    resp->status_text = strdup(status_text);
    resp->url = strdup(url);
    resp->redirected = false;
    if (!resp->status_text || !resp->url)
    {
        NETWORK_SET_ERROR(ctx, "Out of memory while parsing HTTP response");
        network_response_free(resp);
        dynbuf_free(&response_data);
        return NULL;
    }

    if (body_start)
    {
        size_t body_raw_len = response_data.len - (size_t)(body_start - response_data.data);
        const char *cl = find_header(resp->headers, resp->header_count, "Content-Length");
        if (body_raw_len > NETWORK_MAX_RESPONSE_BODY_SIZE ||
            (cl && strtoull(cl, NULL, 10) > NETWORK_MAX_RESPONSE_BODY_SIZE))
        {
            NETWORK_SET_ERROR(ctx, "HTTP response body exceeds %u bytes",
                              (unsigned)NETWORK_MAX_RESPONSE_BODY_SIZE);
            network_response_free(resp);
            dynbuf_free(&response_data);
            return NULL;
        }

        const char *te = find_header(resp->headers, resp->header_count, "Transfer-Encoding");
        if (te && strcasestr(te, "chunked"))
        {
            DynBuf decoded;
            dynbuf_init(&decoded);
            if (parse_chunked_body(body_start, body_raw_len, &decoded))
            {
                resp->body = decoded.data;
                resp->body_len = decoded.len;
            }
            else
            {
                dynbuf_free(&decoded);
                NETWORK_SET_ERROR(ctx, "Invalid or oversized chunked HTTP response body");
                network_response_free(resp);
                dynbuf_free(&response_data);
                return NULL;
            }
        }
        else
        {
            
            size_t content_len = cl ? (size_t)atol(cl) : body_raw_len;
            if (cl && content_len > body_raw_len)
            {
                NETWORK_SET_ERROR(ctx, "Truncated HTTP response body");
                network_response_free(resp);
                dynbuf_free(&response_data);
                return NULL;
            }

            resp->body = malloc(content_len);
            if (content_len > 0 && !resp->body)
            {
                NETWORK_SET_ERROR(ctx, "Out of memory while reading HTTP response body");
                network_response_free(resp);
                dynbuf_free(&response_data);
                return NULL;
            }
            if (content_len > 0)
                memcpy(resp->body, body_start, content_len);
            resp->body_len = content_len;
        }
    }

    dynbuf_free(&response_data);
    return resp;

request_too_large:
    NETWORK_SET_ERROR(ctx, "HTTP request headers exceed %u bytes",
                      (unsigned)NETWORK_MAX_REQUEST_HEADERS_SIZE);
    dynbuf_free(&request_buf);
    network_desktop_transport_close(connection);
    return NULL;
}

#endif 

#if !defined(BUDO_WEB)
static NetworkResponse *network_request_with_deadline(NetworkContext *ctx,
                                                      const char *method,
                                                      const char *url,
                                                      const NetworkHeader *req_headers,
                                                      int req_header_count,
                                                      const uint8_t *req_body,
                                                      size_t req_body_len)
{
    if (!validate_request(ctx, method, url, req_headers, req_header_count,
                          req_body, req_body_len))
        return NULL;

    NetworkRequestPlan plan;
    if (!network_request_plan_init(&plan, method, url, req_headers,
                                   req_header_count, req_body, req_body_len))
    {
        NETWORK_SET_ERROR(ctx, "HTTP URL is too long");
        return NULL;
    }

    for (int redirects = 0;; redirects++)
    {
        if (!is_url_allowed(ctx, plan.url))
            return NULL;
#ifdef BUDO_ANDROID
        NetworkResponse *response = android_request_one_hop(
#else
        NetworkResponse *response = desktop_request_one_hop(
#endif
            ctx, plan.method, plan.url, plan.headers, plan.header_count,
            plan.body, plan.body_len);
        if (!response)
            return NULL;

        const char *location = response_header(response, "Location");
        if (!network_redirect_status(response->status) || !location)
        {
            response->redirected = redirects > 0;
            return response;
        }
        if (redirects >= NETWORK_MAX_REDIRECTS)
        {
            network_response_free(response);
            NETWORK_SET_ERROR(ctx, "Too many HTTP redirects (max %d)", NETWORK_MAX_REDIRECTS);
            return NULL;
        }

        int redirect_status = response->status;
        if (!network_request_plan_redirect(&plan, redirect_status, location))
        {
            network_response_free(response);
            NETWORK_SET_ERROR(ctx, "Invalid HTTP redirect target");
            return NULL;
        }
        network_response_free(response);
    }
}

NetworkResponse *network_request(NetworkContext *ctx,
                                 const char *method,
                                 const char *url,
                                 const NetworkHeader *req_headers,
                                 int req_header_count,
                                 const uint8_t *req_body,
                                 size_t req_body_len)
{
    uint64_t previous_deadline = g_request_deadline_ms;
    uint64_t now = network_monotonic_ms();
    if (previous_deadline == 0)
        g_request_deadline_ms = now ? now + NETWORK_REQUEST_TIMEOUT_MS : 0;
    NetworkResponse *response = network_request_with_deadline(
        ctx, method, url, req_headers, req_header_count, req_body, req_body_len);
    g_request_deadline_ms = previous_deadline;
    return response;
}
#endif

void network_response_free(NetworkResponse *resp)
{
    if (!resp)
        return;

    free(resp->status_text);
    free(resp->url);
    free(resp->body);

    for (int i = 0; i < resp->header_count; i++)
    {
        free(resp->headers[i].name);
        free(resp->headers[i].value);
    }

    free(resp);
}

const char *network_get_error(NetworkContext *ctx)
{
    if (!ctx)
        return "";
    return ctx->error;
}

static void async_slot_free_inputs(AsyncSlot *slot)
{
    free(slot->url);
    slot->url = NULL;
    if (slot->headers)
    {
        for (int i = 0; i < slot->header_count; i++)
        {
            free(slot->headers[i].name);
            free(slot->headers[i].value);
        }
        free(slot->headers);
        slot->headers = NULL;
    }
    free(slot->body);
    slot->body = NULL;
    slot->body_len = 0;
}

static bool async_slot_copy_inputs(AsyncSlot *slot, const char *url,
                                   const NetworkHeader *req_headers, int req_header_count,
                                   const uint8_t *req_body, size_t req_body_len)
{
    slot->url = strdup(url);
    if (!slot->url)
        return false;

    if (req_header_count > 0 && req_headers)
    {
        slot->headers = calloc((size_t)req_header_count, sizeof(NetworkHeader));
        if (!slot->headers)
            return false;
        slot->header_count = req_header_count;
        for (int i = 0; i < req_header_count; i++)
        {
            slot->headers[i].name = strdup(req_headers[i].name);
            slot->headers[i].value = strdup(req_headers[i].value);
            if (!slot->headers[i].name || !slot->headers[i].value)
                return false;
        }
    }

    if (req_body && req_body_len > 0)
    {
        slot->body = malloc(req_body_len);
        if (!slot->body)
            return false;
        memcpy(slot->body, req_body, req_body_len);
        slot->body_len = req_body_len;
    }
    return true;
}

#if !defined(BUDO_WEB)

static BUDO_THREAD_RETURN async_thread_func(void *arg)
{
    AsyncSlot *slot = (AsyncSlot *)arg;

    g_request_error = slot->error;
    g_request_error_size = sizeof(slot->error);
    g_request_slot = slot;

    NetworkResponse *resp = network_request(
        slot->ctx, slot->method, slot->url,
        (const NetworkHeader *)slot->headers, slot->header_count,
        slot->body, slot->body_len);

    budo_mutex_lock(&slot->mutex);
    if (resp)
    {
        slot->response = resp;
    }
    slot->completed = true;
    budo_mutex_unlock(&slot->mutex);

    g_request_error = NULL;
    g_request_error_size = 0;
    g_request_slot = NULL;

    return BUDO_THREAD_RESULT;
}

int network_request_async(NetworkContext *ctx,
                          const char *method,
                          const char *url,
                          const NetworkHeader *req_headers,
                          int req_header_count,
                          const uint8_t *req_body,
                          size_t req_body_len,
                          NetworkAsyncCallback callback,
                          void *user_data)
{
    const char *effective_method = method ? method : "GET";
    if (!ctx || ctx->shutting_down ||
        !validate_request(ctx, effective_method, url, req_headers,
                          req_header_count, req_body, req_body_len))
        return -1;

    if (!is_url_allowed(ctx, url))
        return -1;

    AsyncSlot *slot = NULL;
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        if (!ctx->async_slots[i].active)
        {
            slot = &ctx->async_slots[i];
            break;
        }
    }
    if (!slot)
    {
        NETWORK_SET_ERROR(ctx, "Too many concurrent async requests (max %d)", NETWORK_MAX_ASYNC);
        return -1;
    }

    memset(slot, 0, sizeof(AsyncSlot));
    slot->active = true;
    slot->active_socket = -1;
    slot->id = ctx->next_async_id++;
    slot->ctx = ctx;
    slot->callback = callback;
    slot->user_data = user_data;
    if (!budo_mutex_init(&slot->mutex))
    {
        NETWORK_SET_ERROR(ctx, "Failed to initialize request synchronization");
        slot->active = false;
        return -1;
    }

    snprintf(slot->method, sizeof(slot->method), "%s", effective_method);

    if (!async_slot_copy_inputs(slot, url, req_headers, req_header_count,
                                req_body, req_body_len))
    {
        NETWORK_SET_ERROR(ctx, "Out of memory while copying async request");
        async_slot_free_inputs(slot);
        budo_mutex_destroy(&slot->mutex);
        slot->active = false;
        return -1;
    }

    if (!budo_thread_create(&slot->thread, async_thread_func, slot))
    {
        NETWORK_SET_ERROR(ctx, "Failed to create request thread");
        async_slot_free_inputs(slot);
        budo_mutex_destroy(&slot->mutex);
        slot->active = false;
        return -1;
    }

    return slot->id;
}

void network_async_poll(NetworkContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        AsyncSlot *slot = &ctx->async_slots[i];
        if (!slot->active)
            continue;

        bool done = false;
        budo_mutex_lock(&slot->mutex);
        done = slot->completed;
        budo_mutex_unlock(&slot->mutex);

        if (!done)
            continue;

        budo_thread_join(slot->thread);
        budo_mutex_destroy(&slot->mutex);

        NetworkAsyncCallback callback = slot->callback;
        void *user_data = slot->user_data;
        int request_id = slot->id;
        NetworkResponse *response = slot->response;
        char error[sizeof(slot->error)];
        snprintf(error, sizeof(error), "%s", slot->error);

        async_slot_free_inputs(slot);
        slot->active = false;

        if (callback)
        {
            callback(request_id, response, response ? NULL : error, user_data);
        }
        else
        {
            network_response_free(response);
        }
        return;
    }
}

void network_shutdown(NetworkContext *ctx)
{
    if (!ctx || ctx->shutting_down)
        return;

    ctx->shutting_down = true;
#ifdef BUDO_ANDROID
    android_network_cancel_all();
#endif
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        AsyncSlot *slot = &ctx->async_slots[i];
        if (!slot->active)
            continue;
        budo_mutex_lock(&slot->mutex);
        slot->cancel_requested = true;
#if !defined(BUDO_ANDROID)
        intptr_t socket_handle = slot->active_socket;
        slot->active_socket = -1;
        if (socket_handle != -1)
            network_desktop_transport_abort(socket_handle);
#endif
        budo_mutex_unlock(&slot->mutex);
    }
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        AsyncSlot *slot = &ctx->async_slots[i];
        if (!slot->active)
            continue;
        budo_thread_join(slot->thread);
        budo_mutex_destroy(&slot->mutex);
        network_response_free(slot->response);
        async_slot_free_inputs(slot);
        slot->active = false;
    }
}

#else 

extern void web_network_start_fetch(int request_id,
                                    uint32_t generation,
                                    const char *method,
                                    const char *url,
                                    const NetworkHeader *req_headers,
                                    int req_header_count,
                                    const uint8_t *req_body,
                                    size_t req_body_len);

int network_request_async(NetworkContext *ctx,
                          const char *method,
                          const char *url,
                          const NetworkHeader *req_headers,
                          int req_header_count,
                          const uint8_t *req_body,
                          size_t req_body_len,
                          NetworkAsyncCallback callback,
                          void *user_data)
{
    const char *effective_method = method ? method : "GET";
    if (!ctx || ctx->shutting_down ||
        !validate_request(ctx, effective_method, url, req_headers,
                          req_header_count, req_body, req_body_len) ||
        req_body_len > INT_MAX)
        return -1;

    if (!is_url_allowed(ctx, url))
        return -1;

    AsyncSlot *slot = NULL;
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        if (!ctx->async_slots[i].active)
        {
            slot = &ctx->async_slots[i];
            break;
        }
    }
    if (!slot)
    {
        NETWORK_SET_ERROR(ctx, "Too many concurrent async requests (max %d)", NETWORK_MAX_ASYNC);
        return -1;
    }

    memset(slot, 0, sizeof(AsyncSlot));
    slot->active = true;
    slot->id = ctx->next_async_id++;
    slot->ctx = ctx;
    slot->callback = callback;
    slot->user_data = user_data;

    web_network_start_fetch(slot->id, ctx->web_generation, effective_method, url,
                            req_headers, req_header_count,
                            req_body, req_body_len);

    return slot->id;
}

void network_async_complete(NetworkContext *ctx, int request_id,
                            NetworkResponse *response, const char *error)
{
    if (!ctx)
        return;

    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        AsyncSlot *slot = &ctx->async_slots[i];
        if (slot->active && slot->id == request_id)
        {
            slot->response = response;
            if (error)
                snprintf(slot->error, sizeof(slot->error), "%s", error);
            slot->completed = true;
            return;
        }
    }

    network_response_free(response);
}

void network_async_poll(NetworkContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        AsyncSlot *slot = &ctx->async_slots[i];
        if (!slot->active || !slot->completed)
            continue;

        NetworkAsyncCallback callback = slot->callback;
        void *user_data = slot->user_data;
        int request_id = slot->id;
        NetworkResponse *response = slot->response;
        char error[sizeof(slot->error)];
        snprintf(error, sizeof(error), "%s", slot->error);

        slot->active = false;

        if (callback)
        {
            callback(request_id, response, response ? NULL : error, user_data);
        }
        else
        {
            network_response_free(response);
        }
        return;
    }
}

extern void web_network_context_destroyed(NetworkContext *ctx, uint32_t generation);

uint32_t network_web_generation(NetworkContext *ctx)
{
    return ctx ? ctx->web_generation : 0;
}

bool network_web_allow_all(NetworkContext *ctx)
{
    return ctx && ctx->policy.allow_all;
}

bool network_web_url_allowed(NetworkContext *ctx, const char *url)
{
    return is_url_allowed(ctx, url);
}

void network_shutdown(NetworkContext *ctx)
{
    if (!ctx || ctx->shutting_down)
        return;

    ctx->shutting_down = true;
    web_network_context_destroyed(ctx, ctx->web_generation);
    for (int i = 0; i < NETWORK_MAX_ASYNC; i++)
    {
        AsyncSlot *slot = &ctx->async_slots[i];
        if (!slot->active)
            continue;
        network_response_free(slot->response);
        slot->active = false;
    }
}

#endif 

#ifdef __APPLE__
#pragma clang diagnostic pop
#endif