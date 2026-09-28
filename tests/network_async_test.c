#include "network/network_wrapper.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

typedef struct
{
    int listen_fd;
    pthread_t thread;
    int connection_count;
    volatile int accepted_count;
    int port;
    int redirect_port;
} TestServer;

typedef struct
{
    int fd;
    int port;
    int redirect_port;
} Connection;

static void *handle_connection(void *opaque)
{
    Connection *connection = opaque;
    char request[4096] = {0};
    ssize_t count = read(connection->fd, request, sizeof(request) - 1);
    const char *body = "default";
    char response[512];
    int response_len;

    if (count > 0 && strstr(request, "GET /oversized "))
    {
        response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 200 OK\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                                (unsigned)NETWORK_MAX_RESPONSE_BODY_SIZE + 1);
        (void)write(connection->fd, response, (size_t)response_len);
        close(connection->fd);
        free(connection);
        return NULL;
    }
    if (count > 0 && strstr(request, "GET /redirect-ok "))
    {
        response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 302 Found\r\nLocation: /fast\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        (void)write(connection->fd, response, (size_t)response_len);
        close(connection->fd);
        free(connection);
        return NULL;
    }
    if (count > 0 && strstr(request, "GET /redirect-blocked "))
    {
        response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 302 Found\r\nLocation: http://localhost:%d/fast\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                                connection->port);
        (void)write(connection->fd, response, (size_t)response_len);
        close(connection->fd);
        free(connection);
        return NULL;
    }
    if (count > 0 && strstr(request, "GET /redirect-cross "))
    {
        response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%d/inspect-cross\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                                connection->redirect_port);
        (void)write(connection->fd, response, (size_t)response_len);
        close(connection->fd);
        free(connection);
        return NULL;
    }
    if (count > 0 && strstr(request, "POST /redirect-get "))
    {
        response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 303 See Other\r\nLocation: /inspect-get\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        (void)write(connection->fd, response, (size_t)response_len);
        close(connection->fd);
        free(connection);
        return NULL;
    }
    if (count > 0 && strstr(request, "GET /inspect-cross "))
    {
        body = strstr(request, "Authorization:") || strstr(request, "Cookie:") ||
                       strstr(request, "Proxy-Authorization:")
                   ? "leaked"
                   : "clean";
    }
    else if (count > 0 && strstr(request, "GET /inspect-get "))
    {
        body = strstr(request, "Content-Type:") || strstr(request, "Content-Language:") ||
                       strstr(request, "Expect:") || strstr(request, "Trailer:")
                   ? "leaked"
                   : "clean";
    }

    else if (count > 0 && strstr(request, "GET /slow "))
    {
        usleep(250000);
        body = "slow";
    }
    else if (count > 0 && strstr(request, "GET /destroy "))
    {
        usleep(200000);
        body = "destroy";
    }
    else if (count > 0 && strstr(request, "GET /fast "))
    {
        body = "fast";
    }

    response_len = snprintf(response, sizeof(response),
                            "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                            strlen(body), body);
    (void)write(connection->fd, response, (size_t)response_len);
    close(connection->fd);
    free(connection);
    return NULL;
}

static void *server_main(void *opaque)
{
    TestServer *server = opaque;
    pthread_t handlers[16];

    assert(server->connection_count <= 16);
    for (int i = 0; i < server->connection_count; i++)
    {
        Connection *connection = calloc(1, sizeof(*connection));
        assert(connection);
        connection->fd = accept(server->listen_fd, NULL, NULL);
        assert(connection->fd >= 0);
        server->accepted_count++;
        connection->port = server->port;
        connection->redirect_port = server->redirect_port;
        assert(pthread_create(&handlers[i], NULL, handle_connection, connection) == 0);
    }
    for (int i = 0; i < server->connection_count; i++)
        pthread_join(handlers[i], NULL);
    return NULL;
}

static void start_server(TestServer *server, int *port, int connection_count, int redirect_port)
{
    struct sockaddr_in address = {0};
    socklen_t address_len = sizeof(address);
    int reuse = 1;

    memset(server, 0, sizeof(*server));
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(server->listen_fd >= 0);
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(server->listen_fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(getsockname(server->listen_fd, (struct sockaddr *)&address, &address_len) == 0);
    assert(listen(server->listen_fd, 4) == 0);
    *port = ntohs(address.sin_port);
    server->port = *port;
    server->redirect_port = redirect_port ? redirect_port : *port;
    server->connection_count = connection_count;
    assert(pthread_create(&server->thread, NULL, server_main, server) == 0);
}

typedef struct
{
    int count;
    int ids[2];
    char bodies[2][16];
} CallbackState;

static void record_completion(int request_id, NetworkResponse *response,
                              const char *error, void *opaque)
{
    CallbackState *state = opaque;
    assert(error == NULL);
    assert(response != NULL);
    assert(state->count < 2);
    state->ids[state->count] = request_id;
    size_t length = response->body_len < 15 ? response->body_len : 15;
    memcpy(state->bodies[state->count], response->body, length);
    state->bodies[state->count][length] = '\0';
    state->count++;
    network_response_free(response);
}

static void unexpected_completion(int request_id, NetworkResponse *response,
                                  const char *error, void *opaque)
{
    (void)request_id;
    (void)error;
    bool *called = opaque;
    *called = true;
    network_response_free(response);
}

static void assert_loaded_policy(const char *json, bool allow_all, int domain_count)
{
    char directory[] = "/tmp/budo-network-policy-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char path[256];
    snprintf(path, sizeof(path), "%s/app.json", directory);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(json, 1, strlen(json), file) == strlen(json));
    assert(fclose(file) == 0);

    NetworkPolicy policy;
    network_policy_load_app_json(&policy, directory);
    assert(policy.allow_all == allow_all);
    assert(policy.domain_count == domain_count);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    assert_loaded_policy("{\"network\":\"*\"}", true, 0);
    assert_loaded_policy("{\"network\":[\"example.com\",\"api.example.com\"]}", false, 2);
    assert_loaded_policy("{\"metadata\":{\"network\":\"*\"}}", false, 0);
    assert_loaded_policy("{\"network\":\"*\",\"network\":[]}", false, 0);
    assert_loaded_policy("{\"network\":\"*\"", false, 0);
    assert_loaded_policy("{\"network\":\"*\",\"broken\":truthy}", false, 0);
    assert_loaded_policy("{\"network\":\"*\",\"broken\":[1,]}", false, 0);

    int port;
    int cross_port;
    TestServer server;
    TestServer cross_server;
    start_server(&cross_server, &cross_port, 1, 0);
    start_server(&server, &port, 10, cross_port);
    NetworkPolicy policy;
    network_policy_parse(&policy, "127.0.0.1");

    NetworkContext *ctx = network_create(&policy);
    assert(ctx);
    CallbackState state = {0};
    char slow_url[128];
    char fast_url[128];
    snprintf(slow_url, sizeof(slow_url), "http://127.0.0.1:%d/slow", port);
    snprintf(fast_url, sizeof(fast_url), "http://127.0.0.1:%d/fast", port);
    int slow_id = network_request_async(ctx, "GET", slow_url, NULL, 0, NULL, 0,
                                        record_completion, &state);
    int fast_id = network_request_async(ctx, "GET", fast_url, NULL, 0, NULL, 0,
                                        record_completion, &state);
    assert(slow_id >= 0 && fast_id >= 0);

    for (int i = 0; i < 300 && state.count < 2; i++)
    {
        network_async_poll(ctx);
        usleep(10000);
    }
    assert(state.count == 2);
    assert(state.ids[0] == fast_id);
    assert(strcmp(state.bodies[0], "fast") == 0);
    assert(state.ids[1] == slow_id);
    assert(strcmp(state.bodies[1], "slow") == 0);
    network_destroy(ctx);

    ctx = network_create(&policy);
    assert(ctx);
    bool callback_called = false;
    char destroy_url[128];
    snprintf(destroy_url, sizeof(destroy_url), "http://127.0.0.1:%d/destroy", port);
    assert(network_request_async(ctx, "GET", destroy_url, NULL, 0, NULL, 0,
                                 unexpected_completion, &callback_called) >= 0);
    for (int i = 0; i < 1000 && server.accepted_count < 3; i++)
        usleep(1000);
    assert(server.accepted_count >= 3);
    network_destroy(ctx);
    assert(!callback_called);

    ctx = network_create(&policy);
    assert(ctx);
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/oversized", port);
    assert(network_request(ctx, "GET", url, NULL, 0, NULL, 0) == NULL);
    assert(strstr(network_get_error(ctx), "exceeds") != NULL);

    snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect-ok", port);
    NetworkResponse *redirected = network_request(ctx, "GET", url, NULL, 0, NULL, 0);
    assert(redirected != NULL);
    assert(redirected->redirected);
    assert(redirected->status == 200);
    assert(redirected->body_len == 4);
    assert(memcmp(redirected->body, "fast", 4) == 0);
    network_response_free(redirected);

    snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect-blocked", port);
    assert(network_request(ctx, "GET", url, NULL, 0, NULL, 0) == NULL);
    assert(strstr(network_get_error(ctx), "not in the allowed domain list") != NULL);
    network_destroy(ctx);

    network_policy_parse(&policy, "127.0.0.1");
    ctx = network_create(&policy);
    assert(ctx);
    NetworkHeader sensitive_headers[] = {
        {"Authorization", "Bearer secret"},
        {"Cookie", "session=secret"},
        {"Proxy-Authorization", "Basic secret"},
        {"X-Keep", "yes"}};
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect-cross", port);
    redirected = network_request(ctx, "GET", url, sensitive_headers, 4, NULL, 0);
    if (!redirected)
        fprintf(stderr, "cross-origin redirect failed: %s\n", network_get_error(ctx));
    else if (redirected->body_len != 5 || memcmp(redirected->body, "clean", 5) != 0)
        fprintf(stderr, "cross-origin redirect inspection: %.*s\n",
                (int)redirected->body_len, redirected->body);
    assert(redirected != NULL && redirected->body_len == 5);
    assert(memcmp(redirected->body, "clean", 5) == 0);
    network_response_free(redirected);

    NetworkHeader entity_headers[] = {
        {"Content-Type", "text/plain"},
        {"Content-Language", "en"},
        {"Expect", "100-continue"},
        {"Trailer", "X-Checksum"},
        {"X-Keep", "yes"}};
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/redirect-get", port);
    redirected = network_request(ctx, "POST", url, entity_headers, 5,
                                 (const uint8_t *)"body", 4);
    assert(redirected != NULL && redirected->body_len == 5);
    assert(memcmp(redirected->body, "clean", 5) == 0);
    network_response_free(redirected);

    const char *bad_methods[] = {"", "GE T", "GET\r\nX-Evil: yes"};
    for (size_t i = 0; i < sizeof(bad_methods) / sizeof(bad_methods[0]); i++)
        assert(network_request(ctx, bad_methods[i], fast_url, NULL, 0, NULL, 0) == NULL);
    const char *bad_urls[] = {
        "http://127.0.0.1/space in path",
        "http://127.0.0.1/\r\nX-Evil: yes"};
    for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++)
        assert(network_request(ctx, "GET", bad_urls[i], NULL, 0, NULL, 0) == NULL);
    NetworkHeader bad_headers[] = {
        {"Bad Name", "value"},
        {"X-Test", "safe\r\nX-Evil: yes"},
        {"Host", "attacker.invalid"},
        {"Content-Length", "999"},
        {"Transfer-Encoding", "chunked"},
        {"Connection", "keep-alive"}};
    for (size_t i = 0; i < sizeof(bad_headers) / sizeof(bad_headers[0]); i++)
        assert(network_request(ctx, "GET", fast_url, &bad_headers[i], 1, NULL, 0) == NULL);
    network_destroy(ctx);

    pthread_join(server.thread, NULL);
    close(server.listen_fd);
    pthread_join(cross_server.thread, NULL);
    close(cross_server.listen_fd);
    puts("network async tests passed");
    return 0;
}