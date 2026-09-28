#include "network_request_core.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0)
        snprintf(error, error_size, "%s", message);
}

static bool checked_add_size(size_t first, size_t second, size_t *result)
{
    if (first > SIZE_MAX - second)
        return false;
    *result = first + second;
    return true;
}

static bool http_token_char(unsigned char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           character == '!' || character == '#' || character == '$' ||
           character == '%' || character == '&' || character == '\'' ||
           character == '*' || character == '+' || character == '-' ||
           character == '.' || character == '^' || character == '_' ||
           character == '`' || character == '|' || character == '~';
}

static bool transport_managed_header(const char *name)
{
    return strcasecmp(name, "Host") == 0 ||
           strcasecmp(name, "Content-Length") == 0 ||
           strcasecmp(name, "Transfer-Encoding") == 0 ||
           strcasecmp(name, "Connection") == 0;
}

bool network_request_validate(const char *method, size_t method_capacity,
                              const char *url,
                              const NetworkHeader *headers, int header_count,
                              const uint8_t *body, size_t body_len,
                              char *error, size_t error_size)
{
    if (!method || !url)
        return false;
    if (strlen(url) >= NETWORK_MAX_URL_SIZE ||
        strlen(method) >= method_capacity)
    {
        set_error(error, error_size, "HTTP URL or method is too long");
        return false;
    }
    if (!method[0])
    {
        set_error(error, error_size, "HTTP method must be a non-empty token");
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)method;
         *cursor; cursor++)
    {
        if (!http_token_char(*cursor))
        {
            set_error(error, error_size, "Invalid HTTP method");
            return false;
        }
    }
    if (!url[0])
    {
        set_error(error, error_size, "HTTP URL must be non-empty");
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)url;
         *cursor; cursor++)
    {
        if (*cursor <= 0x20 || *cursor == 0x7f)
        {
            set_error(error, error_size,
                      "Invalid control or whitespace character in HTTP URL");
            return false;
        }
    }
    if (header_count < 0 || header_count > NETWORK_MAX_HEADERS ||
        (header_count > 0 && !headers) || (body_len > 0 && !body) ||
        body_len > NETWORK_MAX_REQUEST_BODY_SIZE)
    {
        set_error(error, error_size,
                  "Invalid or oversized HTTP request body/headers");
        return false;
    }

    size_t total = 0;
    for (int i = 0; i < header_count; i++)
    {
        const NetworkHeader *header = &headers[i];
        if (!header->name || !header->value || !header->name[0])
        {
            set_error(error, error_size, "Invalid HTTP request header");
            return false;
        }
        for (const unsigned char *cursor = (const unsigned char *)header->name;
             *cursor; cursor++)
        {
            if (!http_token_char(*cursor))
            {
                set_error(error, error_size,
                          "Invalid HTTP request header name");
                return false;
            }
        }
        for (const unsigned char *cursor = (const unsigned char *)header->value;
             *cursor; cursor++)
        {
            if (*cursor < 0x20 || *cursor == 0x7f)
            {
                set_error(error, error_size,
                          "Invalid control character in HTTP request header value");
                return false;
            }
        }
        if (transport_managed_header(header->name))
        {
            if (error && error_size > 0)
                snprintf(error, error_size,
                         "HTTP header '%s' is managed by the transport",
                         header->name);
            return false;
        }

        size_t line_size;
        if (!checked_add_size(strlen(header->name), strlen(header->value),
                              &line_size) ||
            !checked_add_size(line_size, 4, &line_size) ||
            !checked_add_size(total, line_size, &total) ||
            total > NETWORK_MAX_REQUEST_HEADERS_SIZE)
        {
            if (error && error_size > 0)
                snprintf(error, error_size,
                         "HTTP request headers exceed %u bytes",
                         (unsigned)NETWORK_MAX_REQUEST_HEADERS_SIZE);
            return false;
        }
    }
    return true;
}

static bool parse_origin(const char *url, char *host, size_t host_size,
                         int *port, bool *is_https)
{
    const char *cursor = url;
    *is_https = false;
    *port = 80;
    if (strncmp(cursor, "https://", 8) == 0)
    {
        *is_https = true;
        *port = 443;
        cursor += 8;
    }
    else if (strncmp(cursor, "http://", 7) == 0)
    {
        cursor += 7;
    }
    else
    {
        return false;
    }

    const char *host_start = cursor;
    while (*cursor && *cursor != ':' && *cursor != '/' && *cursor != '?')
        cursor++;
    size_t host_len = (size_t)(cursor - host_start);
    if (host_len == 0 || host_len >= host_size)
        return false;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    if (*cursor == ':')
    {
        cursor++;
        int parsed_port = 0;
        if (*cursor < '0' || *cursor > '9')
            return false;
        while (*cursor >= '0' && *cursor <= '9')
        {
            parsed_port = parsed_port * 10 + (*cursor - '0');
            if (parsed_port > 65535)
                return false;
            cursor++;
        }
        if (parsed_port == 0)
            return false;
        *port = parsed_port;
    }
    return true;
}

static bool same_origin(const char *first, const char *second)
{
    char first_host[256];
    char second_host[256];
    int first_port;
    int second_port;
    bool first_https;
    bool second_https;
    return parse_origin(first, first_host, sizeof(first_host),
                        &first_port, &first_https) &&
           parse_origin(second, second_host, sizeof(second_host),
                        &second_port, &second_https) &&
           first_https == second_https && first_port == second_port &&
           strcasecmp(first_host, second_host) == 0;
}

static bool resolve_url(const char *base, const char *location,
                        char *out, size_t out_size)
{
    if (!location || !*location)
        return false;
    if (strncmp(location, "http://", 7) == 0 ||
        strncmp(location, "https://", 8) == 0)
        return snprintf(out, out_size, "%s", location) > 0 &&
               strlen(location) < out_size;

    const char *scheme_end = strstr(base, "://");
    if (!scheme_end)
        return false;
    const char *authority = scheme_end + 3;
    const char *path = strchr(authority, '/');
    size_t origin_len = path ? (size_t)(path - base) : strlen(base);

    if (location[0] == '/')
        return origin_len < out_size &&
               snprintf(out, out_size, "%.*s%s", (int)origin_len,
                        base, location) > 0 &&
               origin_len + strlen(location) < out_size;

    const char *last_slash = path ? strrchr(path, '/') : NULL;
    size_t directory_len = last_slash
                               ? (size_t)(last_slash - base + 1)
                               : origin_len + 1;
    return directory_len < out_size &&
           snprintf(out, out_size, "%.*s%s", (int)directory_len,
                    base, location) > 0 &&
           directory_len + strlen(location) < out_size;
}

static bool sensitive_header(const char *name)
{
    return strcasecmp(name, "Authorization") == 0 ||
           strcasecmp(name, "Cookie") == 0 ||
           strcasecmp(name, "Proxy-Authorization") == 0;
}

static bool entity_or_framing_header(const char *name)
{
    return strncasecmp(name, "Content-", 8) == 0 ||
           strcasecmp(name, "Expect") == 0 ||
           strcasecmp(name, "Trailer") == 0;
}

bool network_request_plan_init(NetworkRequestPlan *plan,
                               const char *method, const char *url,
                               const NetworkHeader *headers, int header_count,
                               const uint8_t *body, size_t body_len)
{
    if (!plan || !method || !url || header_count < 0 ||
        header_count > NETWORK_MAX_HEADERS ||
        (header_count > 0 && !headers) || strlen(url) >= sizeof(plan->url))
        return false;
    memset(plan, 0, sizeof(*plan));
    memcpy(plan->url, url, strlen(url) + 1);
    plan->method = method;
    plan->body = body;
    plan->body_len = body_len;
    plan->header_count = header_count;
    for (int i = 0; i < header_count; i++)
        plan->headers[i] = headers[i];
    return true;
}

bool network_redirect_status(int status)
{
    return status == 301 || status == 302 || status == 303 ||
           status == 307 || status == 308;
}

bool network_request_plan_redirect(NetworkRequestPlan *plan, int status,
                                   const char *location)
{
    if (!plan || !network_redirect_status(status))
        return false;

    char next_url[sizeof(plan->url)];
    if (!resolve_url(plan->url, location, next_url, sizeof(next_url)))
        return false;
    bool cross_origin = !same_origin(plan->url, next_url);
    bool changes_to_get = status == 303 ||
                          ((status == 301 || status == 302) &&
                           strcmp(plan->method, "POST") == 0);

    int kept_headers = 0;
    for (int i = 0; i < plan->header_count; i++)
    {
        const char *name = plan->headers[i].name;
        if ((cross_origin && sensitive_header(name)) ||
            (changes_to_get && entity_or_framing_header(name)))
            continue;
        plan->headers[kept_headers++] = plan->headers[i];
    }
    plan->header_count = kept_headers;
    memcpy(plan->url, next_url, strlen(next_url) + 1);

    if (changes_to_get)
    {
        plan->method = "GET";
        plan->body = NULL;
        plan->body_len = 0;
    }
    return true;
}