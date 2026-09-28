#include "core/web_server.h"
#include "core/path_util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET web_socket_t;
#define WEB_INVALID_SOCKET INVALID_SOCKET
#define WEB_SOCKET_ERROR SOCKET_ERROR
static bool g_winsock_initialized = false;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int web_socket_t;
#define WEB_INVALID_SOCKET (-1)
#define WEB_SOCKET_ERROR (-1)
#endif

#define REQUEST_MAX 16384
#define IO_BUFFER_SIZE 32768
#define CLIENT_TIMEOUT_MS 5000

static bool path_has_prefix(const char *path, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0)
        return false;
    return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static bool canonicalize_path(const char *path, char *out, size_t out_size)
{
#ifdef _WIN32
    char *resolved = _fullpath(out, path, out_size);
    if (!resolved)
        return false;
    for (char *p = out; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }
    return true;
#else
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        return false;
    if (strlen(resolved) >= out_size)
        return false;
    strcpy(out, resolved);
    return true;
#endif
}

static void close_web_socket(web_socket_t sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

static int socket_error_code(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static int init_sockets(void)
{
#ifdef _WIN32
    if (!g_winsock_initialized)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return -1;
        g_winsock_initialized = true;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    return 0;
}

static void set_client_timeouts(web_socket_t sock)
{
#ifdef _WIN32
    DWORD timeout = CLIENT_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = CLIENT_TIMEOUT_MS / 1000;
    timeout.tv_usec = (CLIENT_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

static int send_all(web_socket_t sock, const void *data, size_t len)
{
    const char *p = (const char *)data;
    while (len > 0)
    {
#ifdef _WIN32
        int chunk = (len > INT_MAX) ? INT_MAX : (int)len;
        int sent = send(sock, p, chunk, 0);
#else
        ssize_t sent = send(sock, p, len, 0);
#endif
        if (sent <= 0)
            return -1;
        p += sent;
        len -= (size_t)sent;
    }
    return 0;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return -1;
}

static bool url_decode_path(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    for (size_t i = 0; src[i] && src[i] != '?' && src[i] != '#'; i++)
    {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '%')
        {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi < 0 || lo < 0)
                return false;
            ch = (unsigned char)((hi << 4) | lo);
            i += 2;
        }
        if (ch == '\0' || ch == '\r' || ch == '\n')
            return false;
        if (out + 1 >= dst_size)
            return false;
        dst[out++] = (char)ch;
    }
    dst[out] = '\0';
    return true;
}

static bool equals_ci(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_with_ci(const char *value, const char *prefix)
{
    while (*prefix)
    {
        if (tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
            return false;
        value++;
        prefix++;
    }
    return true;
}

static const char *find_header_value(const char *request, const char *name)
{
    size_t name_len = strlen(name);
    const char *line = strstr(request, "\r\n");
    if (!line)
        line = strchr(request, '\n');
    if (!line)
        return NULL;
    line += (line[0] == '\r' && line[1] == '\n') ? 2 : 1;

    while (*line)
    {
        const char *line_end = strstr(line, "\r\n");
        if (!line_end)
            line_end = strchr(line, '\n');
        if (!line_end)
            return NULL;
        if (line_end == line)
            return NULL;

        const char *colon = memchr(line, ':', (size_t)(line_end - line));
        if (colon && (size_t)(colon - line) == name_len)
        {
            bool match = true;
            for (size_t i = 0; i < name_len; i++)
            {
                if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i]))
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                const char *value = colon + 1;
                while (value < line_end && (*value == ' ' || *value == '\t'))
                    value++;
                return value;
            }
        }

        line = line_end + ((line_end[0] == '\r' && line_end[1] == '\n') ? 2 : 1);
    }

    return NULL;
}

static bool host_header_is_loopback(const char *value)
{
    if (!value)
        return false;

    char host[256];
    size_t len = 0;
    while (value[len] && value[len] != '\r' && value[len] != '\n' &&
           value[len] != ' ' && value[len] != '\t' && len + 1 < sizeof(host))
    {
        host[len] = value[len];
        len++;
    }
    host[len] = '\0';
    if (len == 0)
        return false;

    if (equals_ci(host, "localhost") || starts_with_ci(host, "localhost:"))
        return true;
    if (strcmp(host, "127.0.0.1") == 0 || starts_with_ci(host, "127.0.0.1:"))
        return true;
    if (equals_ci(host, "[::1]") || starts_with_ci(host, "[::1]:"))
        return true;
    return false;
}

static const char *content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";
    ext++;
    if (equals_ci(ext, "html") || equals_ci(ext, "htm"))
        return "text/html; charset=utf-8";
    if (equals_ci(ext, "js") || equals_ci(ext, "mjs"))
        return "text/javascript; charset=utf-8";
    if (equals_ci(ext, "wasm"))
        return "application/wasm";
    if (equals_ci(ext, "data"))
        return "application/octet-stream";
    if (equals_ci(ext, "json"))
        return "application/json; charset=utf-8";
    if (equals_ci(ext, "css"))
        return "text/css; charset=utf-8";
    if (equals_ci(ext, "svg"))
        return "image/svg+xml";
    if (equals_ci(ext, "png"))
        return "image/png";
    if (equals_ci(ext, "jpg") || equals_ci(ext, "jpeg"))
        return "image/jpeg";
    if (equals_ci(ext, "gif"))
        return "image/gif";
    if (equals_ci(ext, "webp"))
        return "image/webp";
    if (equals_ci(ext, "ico"))
        return "image/x-icon";
    if (equals_ci(ext, "ttf"))
        return "font/ttf";
    if (equals_ci(ext, "otf"))
        return "font/otf";
    if (equals_ci(ext, "woff"))
        return "font/woff";
    if (equals_ci(ext, "woff2"))
        return "font/woff2";
    return "application/octet-stream";
}

static const char *reason_phrase(int status)
{
    switch (status)
    {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 414:
        return "URI Too Long";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    default:
        return "Error";
    }
}

static void send_error_response(web_socket_t client, int status, const char *extra_header)
{
    char body[256];
    int body_len = snprintf(body, sizeof(body), "%d %s\n", status, reason_phrase(status));
    char header[512];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: text/plain; charset=utf-8\r\n"
                              "Content-Length: %d\r\n"
                              "X-Content-Type-Options: nosniff\r\n"
                              "Referrer-Policy: no-referrer\r\n"
                              "Connection: close\r\n"
                              "%s"
                              "\r\n",
                              status, reason_phrase(status), body_len,
                              extra_header ? extra_header : "");
    send_all(client, header, (size_t)header_len);
    send_all(client, body, (size_t)body_len);
}

static bool make_relative_path(const char *target, char *relative, size_t relative_size)
{
    char decoded[PATH_MAX];
    if (!url_decode_path(target, decoded, sizeof(decoded)))
        return false;

    char *path = decoded;
    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0)
        return false;

    while (*path == '/')
        path++;

    if (*path == '\0')
        path = (char *)"index.html";

    if (!path_is_safe_relative(path))
        return false;

    snprintf(relative, relative_size, "%s", path);
    return true;
}

static bool copy_token(char **cursor, char *dst, size_t dst_size)
{
    char *p = *cursor;
    size_t len = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0')
        return false;

    while (p[len] && p[len] != ' ' && p[len] != '\t')
        len++;
    if (len == 0 || len >= dst_size)
        return false;

    memcpy(dst, p, len);
    dst[len] = '\0';
    *cursor = p + len;
    return true;
}

static bool parse_request_line(char *request, char *method, size_t method_size,
                               char *target, size_t target_size,
                               char *version, size_t version_size)
{
    char *line_end = strstr(request, "\r\n");
    if (!line_end)
        line_end = strchr(request, '\n');
    if (!line_end)
        return false;
    *line_end = '\0';

    char *cursor = request;
    return copy_token(&cursor, method, method_size) &&
           copy_token(&cursor, target, target_size) &&
           copy_token(&cursor, version, version_size);
}

static void handle_client(web_socket_t client, const char *root_dir, const char *canonical_root)
{
    char request[REQUEST_MAX + 1];
    size_t used = 0;
    bool header_complete = false;
    while (used < REQUEST_MAX)
    {
#ifdef _WIN32
        int got = recv(client, request + used, (int)(REQUEST_MAX - used), 0);
#else
        ssize_t got = recv(client, request + used, REQUEST_MAX - used, 0);
#endif
        if (got <= 0)
            return;
        used += (size_t)got;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") || strstr(request, "\n\n"))
        {
            header_complete = true;
            break;
        }
    }
    if (!header_complete)
    {
        send_error_response(client, 431, NULL);
        return;
    }

    bool loopback_host = host_header_is_loopback(find_header_value(request, "Host"));

    char method[16];
    char target[PATH_MAX];
    char version[32];
    if (!parse_request_line(request, method, sizeof(method), target, sizeof(target),
                            version, sizeof(version)))
    {
        send_error_response(client, 400, NULL);
        return;
    }
    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0)
    {
        send_error_response(client, 400, NULL);
        return;
    }
    if (strcmp(version, "HTTP/1.1") == 0 && !loopback_host)
    {
        send_error_response(client, 403, NULL);
        return;
    }

    bool is_head = strcmp(method, "HEAD") == 0;
    if (strcmp(method, "GET") != 0 && !is_head)
    {
        send_error_response(client, 405, "Allow: GET, HEAD\r\n");
        return;
    }

    char relative[PATH_MAX];
    if (!make_relative_path(target, relative, sizeof(relative)))
    {
        send_error_response(client, 403, NULL);
        return;
    }

    char full_path[PATH_MAX];
    int n = snprintf(full_path, sizeof(full_path), "%s/%s", root_dir, relative);
    if (n < 0 || (size_t)n >= sizeof(full_path))
    {
        send_error_response(client, 414, NULL);
        return;
    }

#ifndef _WIN32
    struct stat link_st;
    if (lstat(full_path, &link_st) == 0 && S_ISLNK(link_st.st_mode))
    {
        send_error_response(client, 403, NULL);
        return;
    }
#endif

    char canonical_file[PATH_MAX];
    if (!canonicalize_path(full_path, canonical_file, sizeof(canonical_file)))
    {
        send_error_response(client, 404, NULL);
        return;
    }
    if (!path_has_prefix(canonical_file, canonical_root))
    {
        send_error_response(client, 403, NULL);
        return;
    }

    struct stat st;
    if (stat(canonical_file, &st) != 0 || !S_ISREG(st.st_mode))
    {
        send_error_response(client, 404, NULL);
        return;
    }

    FILE *f = fopen(canonical_file, "rb");
    if (!f)
    {
        send_error_response(client, 404, NULL);
        return;
    }

    char header[512];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %lld\r\n"
                              "Cache-Control: no-cache\r\n"
                              "X-Content-Type-Options: nosniff\r\n"
                              "Referrer-Policy: no-referrer\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              content_type_for_path(canonical_file),
                              (long long)st.st_size);
    if (send_all(client, header, (size_t)header_len) != 0 || is_head)
    {
        fclose(f);
        return;
    }

    char *buffer = (char *)malloc(IO_BUFFER_SIZE);
    if (!buffer)
    {
        fclose(f);
        return;
    }
    size_t read_count;
    while ((read_count = fread(buffer, 1, IO_BUFFER_SIZE, f)) > 0)
    {
        if (send_all(client, buffer, read_count) != 0)
            break;
    }
    free(buffer);
    fclose(f);
}

int web_server_serve_directory(const char *root_dir, const char *host, int port)
{
    if (!root_dir || port <= 0 || port > 65535)
        return 1;

    struct in_addr listen_addr;
    const char *display_host;
    if (!host || host[0] == '\0' || strcmp(host, "localhost") == 0)
    {
        listen_addr.s_addr = htonl(INADDR_LOOPBACK);
        display_host = "localhost";
    }
    else if (strcmp(host, "*") == 0 || strcmp(host, "0.0.0.0") == 0)
    {
        listen_addr.s_addr = htonl(INADDR_ANY);
        display_host = "0.0.0.0";
    }
    else if (inet_pton(AF_INET, host, &listen_addr) == 1)
    {
        display_host = host;
    }
    else
    {
        fprintf(stderr, "Error: invalid listen address: %s\n", host);
        return 1;
    }

    char canonical_root[PATH_MAX];
    struct stat root_st;
    if (!canonicalize_path(root_dir, canonical_root, sizeof(canonical_root)) ||
        stat(canonical_root, &root_st) != 0 || !S_ISDIR(root_st.st_mode))
    {
        fprintf(stderr, "Error: HTTP root is not a readable directory: %s\n", root_dir);
        return 1;
    }

    if (init_sockets() != 0)
    {
        fprintf(stderr, "Error: cannot initialize sockets.\n");
        return 1;
    }

    web_socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == WEB_INVALID_SOCKET)
    {
        fprintf(stderr, "Error: cannot create HTTP server socket (%d).\n", socket_error_code());
        return 1;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr = listen_addr;

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == WEB_SOCKET_ERROR)
    {
        fprintf(stderr, "Error: cannot bind HTTP server to %s:%d (%d).\n",
                display_host, port, socket_error_code());
        close_web_socket(server);
        return 1;
    }

    if (listen(server, 16) == WEB_SOCKET_ERROR)
    {
        fprintf(stderr, "Error: cannot listen on %s:%d (%d).\n", display_host, port, socket_error_code());
        close_web_socket(server);
        return 1;
    }

    printf("\nServing %s on http://%s:%d\n", canonical_root, display_host, port);
    printf("Press Ctrl+C to stop.\n\n");
    fflush(stdout);

    for (;;)
    {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        web_socket_t client = accept(server, (struct sockaddr *)&client_addr, &client_len);
        if (client == WEB_INVALID_SOCKET)
        {
            int err = socket_error_code();
#ifndef _WIN32
            if (err == EINTR)
                continue;
#endif
            fprintf(stderr, "Error: HTTP accept failed (%d).\n", err);
            close_web_socket(server);
            return 1;
        }

        set_client_timeouts(client);
        handle_client(client, canonical_root, canonical_root);
        close_web_socket(client);
    }
}