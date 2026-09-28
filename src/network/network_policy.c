#include "network_wrapper.h"
#include "core/json_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

void network_policy_parse(NetworkPolicy *policy, const char *value)
{
    memset(policy, 0, sizeof(NetworkPolicy));

    if (!value || value[0] == '\0')
        return;

    while (*value && isspace((unsigned char)*value))
        value++;

    if (strcmp(value, "*") == 0)
    {
        policy->allow_all = true;
        return;
    }

    const char *cursor = value;
    while (*cursor && policy->domain_count < NETWORK_MAX_DOMAINS)
    {
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ','))
            cursor++;
        if (!*cursor)
            break;

        const char *start = cursor;
        while (*cursor && *cursor != ',' && !isspace((unsigned char)*cursor))
            cursor++;

        size_t length = (size_t)(cursor - start);
        if (length > 0 && length < sizeof(policy->domains[0]))
        {
            memcpy(policy->domains[policy->domain_count], start, length);
            policy->domains[policy->domain_count][length] = '\0';
            policy->domain_count++;
        }
    }
}

static bool policy_extract_host(const char *url, char *host, size_t host_size)
{
    const char *cursor = url;
    if (strncmp(cursor, "https://", 8) == 0)
        cursor += 8;
    else if (strncmp(cursor, "http://", 7) == 0)
        cursor += 7;
    else
        return false;

    const char *host_start = cursor;
    while (*cursor && *cursor != ':' && *cursor != '/' && *cursor != '?')
        cursor++;
    size_t host_len = (size_t)(cursor - host_start);
    if (host_len == 0 || host_len >= host_size)
        return false;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    return true;
}

static bool policy_domain_matches(const char *host, const char *allowed)
{
    size_t host_len = strlen(host);
    size_t allowed_len = strlen(allowed);
    if (host_len == allowed_len)
        return strcasecmp(host, allowed) == 0;
    if (host_len > allowed_len + 1)
    {
        const char *suffix = host + host_len - allowed_len;
        return suffix[-1] == '.' && strcasecmp(suffix, allowed) == 0;
    }
    return false;
}

bool network_policy_allows_url(const NetworkPolicy *policy, const char *url,
                               char *error, size_t error_size)
{
    if (!policy)
        return false;
    if (policy->allow_all)
        return true;
    if (policy->domain_count == 0)
    {
        if (error && error_size > 0)
            snprintf(error, error_size,
                     "Network access denied: no network policy configured");
        return false;
    }

    char host[256];
    if (!url || !policy_extract_host(url, host, sizeof(host)))
    {
        if (error && error_size > 0)
            snprintf(error, error_size, "Invalid URL: cannot extract hostname");
        return false;
    }
    for (int i = 0; i < policy->domain_count; i++)
        if (policy_domain_matches(host, policy->domains[i]))
            return true;

    if (error && error_size > 0)
        snprintf(error, error_size,
                 "Network access denied: host '%s' is not in the allowed domain list",
                 host);
    return false;
}

static const char *policy_json_string_end(const char *value)
{
    if (!value || *value != '"')
        return NULL;
    for (value++; *value && *value != '"'; value++)
    {
        if ((unsigned char)*value < 0x20)
            return NULL;
        if (*value == '\\')
        {
            value++;
            if (!strchr("\"\\/bfnrtu", *value))
                return NULL;
            if (*value == 'u')
            {
                for (int i = 0; i < 4; i++)
                {
                    if (!isxdigit((unsigned char)value[1]))
                        return NULL;
                    value++;
                }
            }
        }
    }
    return *value == '"' ? value + 1 : NULL;
}

static const char *policy_json_skip_ws(const char *value)
{
    while (*value && isspace((unsigned char)*value))
        value++;
    return value;
}

static const char *policy_json_skip_value(const char *value, int depth)
{
    if (depth > 32)
        return NULL;
    value = policy_json_skip_ws(value);
    if (*value == '"')
        return policy_json_string_end(value);
    if (*value == '{')
    {
        value = policy_json_skip_ws(value + 1);
        if (*value == '}')
            return value + 1;
        while (*value)
        {
            value = policy_json_string_end(value);
            if (!value)
                return NULL;
            value = policy_json_skip_ws(value);
            if (*value++ != ':')
                return NULL;
            value = policy_json_skip_value(value, depth + 1);
            if (!value)
                return NULL;
            value = policy_json_skip_ws(value);
            if (*value == '}')
                return value + 1;
            if (*value++ != ',')
                return NULL;
            value = policy_json_skip_ws(value);
        }
        return NULL;
    }
    if (*value == '[')
    {
        value = policy_json_skip_ws(value + 1);
        if (*value == ']')
            return value + 1;
        while (*value)
        {
            value = policy_json_skip_value(value, depth + 1);
            if (!value)
                return NULL;
            value = policy_json_skip_ws(value);
            if (*value == ']')
                return value + 1;
            if (*value++ != ',')
                return NULL;
            value = policy_json_skip_ws(value);
        }
        return NULL;
    }
    const char *literal = NULL;
    size_t literal_len = 0;
    if (strncmp(value, "true", 4) == 0)
    {
        literal = value + 4;
        literal_len = 4;
    }
    else if (strncmp(value, "false", 5) == 0)
    {
        literal = value + 5;
        literal_len = 5;
    }
    else if (strncmp(value, "null", 4) == 0)
    {
        literal = value + 4;
        literal_len = 4;
    }
    if (literal_len > 0)
        return strchr(",]} \t\r\n", *literal) ? literal : NULL;

    const char *number = value;
    if (*number == '-')
        number++;
    if (*number == '0')
        number++;
    else if (isdigit((unsigned char)*number))
        while (isdigit((unsigned char)*number))
            number++;
    else
        return NULL;
    if (*number == '.')
    {
        number++;
        if (!isdigit((unsigned char)*number))
            return NULL;
        while (isdigit((unsigned char)*number))
            number++;
    }
    if (*number == 'e' || *number == 'E')
    {
        number++;
        if (*number == '+' || *number == '-')
            number++;
        if (!isdigit((unsigned char)*number))
            return NULL;
        while (isdigit((unsigned char)*number))
            number++;
    }
    return strchr(",]} \t\r\n", *number) ? number : NULL;
}

static bool policy_json_read_domains(const char *value, NetworkPolicy *policy)
{
    if (*value == '"')
    {
        char text[512];
        if (!json_read_string(value, text, sizeof(text)))
            return false;
        network_policy_parse(policy, text);
        return true;
    }
    if (*value != '[')
        return false;

    value = policy_json_skip_ws(value + 1);
    while (*value && *value != ']')
    {
        if (*value != '"' || policy->domain_count >= NETWORK_MAX_DOMAINS ||
            !json_read_string(value, policy->domains[policy->domain_count],
                              sizeof(policy->domains[0])))
            return false;
        policy->domain_count++;
        value = policy_json_string_end(value);
        if (!value)
            return false;
        value = policy_json_skip_ws(value);
        if (*value == ',')
            value = policy_json_skip_ws(value + 1);
        else if (*value != ']')
            return false;
    }
    return *value == ']';
}

void network_policy_load_app_json(NetworkPolicy *policy, const char *project_dir)
{
    memset(policy, 0, sizeof(*policy));
    if (!project_dir)
        return;

    char path[4096];
    int path_len = snprintf(path, sizeof(path), "%s/app.json", project_dir);
    if (path_len < 0 || (size_t)path_len >= sizeof(path))
        return;

    char *json = json_util_read_file(path, 1024 * 1024);
    if (!json)
        return;

    NetworkPolicy parsed = {0};
    bool found = false;
    bool valid = true;
    const char *cursor = policy_json_skip_ws(json);
    if (*cursor++ != '{')
        valid = false;
    cursor = policy_json_skip_ws(cursor);
    while (valid && *cursor && *cursor != '}')
    {
        char key[64];
        const char *key_end = policy_json_string_end(cursor);
        if (!key_end || !json_read_string(cursor, key, sizeof(key)))
        {
            valid = false;
            break;
        }
        cursor = policy_json_skip_ws(key_end);
        if (*cursor++ != ':')
        {
            valid = false;
            break;
        }
        cursor = policy_json_skip_ws(cursor);
        const char *value_end = policy_json_skip_value(cursor, 0);
        if (!value_end)
        {
            valid = false;
            break;
        }
        if (strcmp(key, "network") == 0)
        {
            NetworkPolicy candidate = {0};
            if (found || !policy_json_read_domains(cursor, &candidate))
            {
                valid = false;
                break;
            }
            parsed = candidate;
            found = true;
        }
        cursor = policy_json_skip_ws(value_end);
        if (*cursor == ',')
            cursor = policy_json_skip_ws(cursor + 1);
        else if (*cursor != '}')
            valid = false;
    }
    if (valid && *cursor == '}')
        cursor = policy_json_skip_ws(cursor + 1);
    else
        valid = false;
    if (valid && *cursor == '\0' && found)
        *policy = parsed;
    free(json);
}