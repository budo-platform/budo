#include "tests/network_mock.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NETWORK_MOCK_MAX_REQUESTS 128

struct NetworkContext
{
    NetworkPolicy policy;
    int id;
    int next_request_id;
    uint32_t generation;
    bool shutting_down;
    char error[512];
};

typedef struct MockNetworkRequest
{
    bool active;
    bool canceled;
    bool completed;
    int context_id;
    uint32_t context_generation;
    int request_id;
    char url[NETWORK_MAX_URL_SIZE];
    NetworkAsyncCallback callback;
    void *user_data;
    NetworkResponse *response;
    char error[512];
} MockNetworkRequest;

static MockNetworkRequest requests[NETWORK_MOCK_MAX_REQUESTS];
static int next_context_id;
static int live_contexts;
static int destroyed_contexts;
static int discarded_completions;

static char *mock_strdup(const char *value)
{
    size_t length;
    char *copy;

    if (!value)
        return NULL;
    length = strlen(value);
    copy = (char *)malloc(length + 1);
    if (copy)
        memcpy(copy, value, length + 1);
    return copy;
}

void network_response_free(NetworkResponse *response)
{
    if (!response)
        return;
    free(response->status_text);
    free(response->url);
    free(response->body);
    for (int index = 0; index < response->header_count; index++)
    {
        free(response->headers[index].name);
        free(response->headers[index].value);
    }
    free(response);
}

static NetworkResponse *mock_response_create(const MockNetworkRequest *request,
                                             int status, const char *body)
{
    NetworkResponse *response =
        (NetworkResponse *)calloc(1, sizeof(NetworkResponse));
    size_t body_length = body ? strlen(body) : 0;

    if (!response)
        return NULL;
    response->status = status;
    response->status_text = mock_strdup(status >= 200 && status < 300 ? "OK" : "Error");
    response->url = mock_strdup(request->url);
    if (body_length > 0)
    {
        response->body = (uint8_t *)malloc(body_length);
        if (response->body)
            memcpy(response->body, body, body_length);
    }
    response->body_len = response->body ? body_length : 0;
    response->headers[0].name = mock_strdup("x-mock");
    response->headers[0].value = mock_strdup(body ? body : "");
    response->header_count = 1;
    if (!response->status_text || !response->url ||
        (body_length > 0 && !response->body) ||
        !response->headers[0].name || !response->headers[0].value)
    {
        network_response_free(response);
        return NULL;
    }
    return response;
}

void network_mock_reset(void)
{
    assert(live_contexts == 0);
    for (int index = 0; index < NETWORK_MOCK_MAX_REQUESTS; index++)
        network_response_free(requests[index].response);
    memset(requests, 0, sizeof(requests));
    next_context_id = 0;
    destroyed_contexts = 0;
    discarded_completions = 0;
}

NetworkContext *network_create(const NetworkPolicy *policy)
{
    NetworkContext *ctx = (NetworkContext *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    if (policy)
        ctx->policy = *policy;
    ctx->id = ++next_context_id;
    ctx->generation = 1;
    live_contexts++;
    return ctx;
}

void network_shutdown(NetworkContext *ctx)
{
    if (!ctx || ctx->shutting_down)
        return;
    ctx->shutting_down = true;
    for (int index = 0; index < NETWORK_MOCK_MAX_REQUESTS; index++)
    {
        MockNetworkRequest *request = &requests[index];
        if (request->active && request->context_id == ctx->id &&
            request->context_generation == ctx->generation)
            request->canceled = true;
    }
    ctx->generation++;
}

void network_destroy(NetworkContext *ctx)
{
    if (!ctx)
        return;
    network_shutdown(ctx);
    free(ctx);
    live_contexts--;
    destroyed_contexts++;
}

bool network_is_enabled(NetworkContext *ctx)
{
    return ctx && !ctx->shutting_down &&
           (ctx->policy.allow_all || ctx->policy.domain_count > 0);
}

const char *network_get_error(NetworkContext *ctx)
{
    return ctx ? ctx->error : "Invalid network context";
}

void network_policy_parse(NetworkPolicy *policy, const char *value)
{
    memset(policy, 0, sizeof(*policy));
    if (!value || !*value)
        return;
    if (strcmp(value, "*") == 0)
    {
        policy->allow_all = true;
        return;
    }
    snprintf(policy->domains[0], sizeof(policy->domains[0]), "%s", value);
    policy->domain_count = 1;
}

void network_policy_load_app_json(NetworkPolicy *policy, const char *project_dir)
{
    (void)project_dir;
    memset(policy, 0, sizeof(*policy));
    policy->allow_all = true;
}

NetworkResponse *network_request(NetworkContext *ctx, const char *method,
                                 const char *url,
                                 const NetworkHeader *req_headers,
                                 int req_header_count,
                                 const uint8_t *req_body,
                                 size_t req_body_len)
{
    (void)ctx;
    (void)method;
    (void)url;
    (void)req_headers;
    (void)req_header_count;
    (void)req_body;
    (void)req_body_len;
    return NULL;
}

int network_request_async(NetworkContext *ctx, const char *method,
                          const char *url,
                          const NetworkHeader *req_headers,
                          int req_header_count,
                          const uint8_t *req_body,
                          size_t req_body_len,
                          NetworkAsyncCallback callback,
                          void *user_data)
{
    (void)method;
    (void)req_headers;
    (void)req_header_count;
    (void)req_body;
    (void)req_body_len;
    if (!ctx || ctx->shutting_down || !url || !callback)
        return -1;
    for (int index = 0; index < NETWORK_MOCK_MAX_REQUESTS; index++)
    {
        MockNetworkRequest *request = &requests[index];
        if (request->active)
            continue;
        memset(request, 0, sizeof(*request));
        request->active = true;
        request->context_id = ctx->id;
        request->context_generation = ctx->generation;
        request->request_id = ctx->next_request_id++;
        request->callback = callback;
        request->user_data = user_data;
        snprintf(request->url, sizeof(request->url), "%s", url);
        return request->request_id;
    }
    snprintf(ctx->error, sizeof(ctx->error), "Too many mock requests");
    return -1;
}

void network_async_poll(NetworkContext *ctx)
{
    if (!ctx || ctx->shutting_down)
        return;
    for (int index = 0; index < NETWORK_MOCK_MAX_REQUESTS; index++)
    {
        MockNetworkRequest *request = &requests[index];
        if (!request->active || request->canceled || !request->completed ||
            request->context_id != ctx->id ||
            request->context_generation != ctx->generation)
            continue;
        NetworkAsyncCallback callback = request->callback;
        void *user_data = request->user_data;
        NetworkResponse *response = request->response;
        int request_id = request->request_id;
        char error[sizeof(request->error)];
        snprintf(error, sizeof(error), "%s", request->error);
        memset(request, 0, sizeof(*request));
        callback(request_id, response, response ? NULL : error, user_data);
        return;
    }
}

int network_mock_find_request(NetworkContext *ctx, const char *url)
{
    if (!ctx || !url)
        return -1;
    for (int index = 0; index < NETWORK_MOCK_MAX_REQUESTS; index++)
    {
        MockNetworkRequest *request = &requests[index];
        if (request->active && request->context_id == ctx->id &&
            request->context_generation == ctx->generation &&
            strcmp(request->url, url) == 0)
            return index + 1;
    }
    return -1;
}

bool network_mock_complete(int handle, int status, const char *body)
{
    if (handle <= 0 || handle > NETWORK_MOCK_MAX_REQUESTS)
        return false;
    MockNetworkRequest *request = &requests[handle - 1];
    if (!request->active || request->completed)
        return false;
    if (request->canceled)
    {
        memset(request, 0, sizeof(*request));
        discarded_completions++;
        return true;
    }
    request->response = mock_response_create(request, status, body);
    if (!request->response)
    {
        snprintf(request->error, sizeof(request->error), "Mock allocation failed");
    }
    request->completed = true;
    return true;
}

int network_mock_live_count(void)
{
    return live_contexts;
}

int network_mock_destroy_count(void)
{
    return destroyed_contexts;
}

int network_mock_discarded_completion_count(void)
{
    return discarded_completions;
}