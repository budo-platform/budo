#include "json_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *json_util_read_file(const char *path, size_t max_size)
{
    if (!path)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    if ((size_t)fsize > max_size) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';
    return buf;
}

const char *json_skip_ws(const char *p)
{
    while (p && *p && (isspace((unsigned char)*p) || *p == ','))
        p++;
    return p;
}

const char *json_find_key(const char *body, const char *key)
{
    if (!body || !key)
        return NULL;
    size_t klen = strlen(key);
    const char *p = body;
    while (*p)
    {
        if (*p == '"')
        {
            const char *start = ++p;
            while (*p && *p != '"')
            {
                if (*p == '\\' && p[1])
                    p++;
                p++;
            }
            if (!*p)
                return NULL;
            size_t slen = (size_t)(p - start);
            p++;
            const char *q = json_skip_ws(p);
            if (*q == ':' && slen == klen && memcmp(start, key, klen) == 0)
            {
                q++;
                return json_skip_ws(q);
            }
        }
        else
            p++;
    }
    return NULL;
}

bool json_read_string(const char *p, char *dst, size_t dst_size)
{
    if (!p || *p != '"' || !dst || dst_size == 0)
        return false;
    p++;
    size_t out = 0;
    while (*p && *p != '"')
    {
        if (*p == '\\' && p[1])
        {
            char c = p[1];
            switch (c)
            {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default: break;
            }
            if (out + 1 < dst_size) dst[out++] = c;
            p += 2;
        }
        else
        {
            if (out + 1 < dst_size) dst[out++] = *p;
            p++;
        }
    }
    dst[out] = '\0';
    return *p == '"';
}

const char *json_skip_value(const char *p)
{
    p = json_skip_ws(p);
    if (!p || !*p)
        return NULL;
    if (*p == '"')
    {
        p++;
        while (*p && *p != '"')
        {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        return *p == '"' ? p + 1 : NULL;
    }
    if (*p == '{' || *p == '[')
    {
        char open = *p, close = (open == '{') ? '}' : ']';
        int depth = 1;
        p++;
        while (*p && depth > 0)
        {
            if (*p == '"')
            {
                p++;
                while (*p && *p != '"')
                {
                    if (*p == '\\' && p[1]) p++;
                    p++;
                }
                if (!*p) return NULL;
                p++;
            }
            else if (*p == open) { depth++; p++; }
            else if (*p == close) { depth--; p++; }
            else p++;
        }
        return depth == 0 ? p : NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p))
        p++;
    return p;
}

char *json_object_body(const char *p)
{
    p = json_skip_ws(p);
    if (!p || *p != '{')
        return NULL;
    const char *end = json_skip_value(p);
    if (!end)
        return NULL;
    const char *body_start = p + 1;
    const char *body_end = end - 1;
    if (body_end < body_start)
        return strdup("");
    size_t len = (size_t)(body_end - body_start);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, body_start, len);
    out[len] = '\0';
    return out;
}

bool json_get_string(const char *body, const char *key,
                     char *dst, size_t dst_size, const char *defval)
{
    if (!dst || dst_size == 0)
        return false;
    const char *v = json_find_key(body, key);
    if (v && *v == '"' && json_read_string(v, dst, dst_size))
        return true;
    snprintf(dst, dst_size, "%s", defval ? defval : "");
    return false;
}

int json_get_int(const char *body, const char *key, int defval)
{
    const char *v = json_find_key(body, key);
    if (!v) return defval;
    if (*v == '"')
    {
        char buf[32];
        if (json_read_string(v, buf, sizeof(buf)))
            return atoi(buf);
        return defval;
    }
    return atoi(v);
}

bool json_get_bool(const char *body, const char *key, bool defval)
{
    const char *v = json_find_key(body, key);
    if (!v) return defval;
    if (strncmp(v, "true", 4) == 0) return true;
    if (strncmp(v, "false", 5) == 0) return false;
    return defval;
}

bool json_get_nested_string(const char *body, const char *parent, const char *child,
                            char *dst, size_t dst_size, const char *defval)
{
    const char *p = json_find_key(body, parent);
    if (!p)
    {
        if (dst && dst_size > 0) snprintf(dst, dst_size, "%s", defval ? defval : "");
        return false;
    }
    char *child_body = json_object_body(p);
    if (!child_body)
    {
        if (dst && dst_size > 0) snprintf(dst, dst_size, "%s", defval ? defval : "");
        return false;
    }
    bool ok = json_get_string(child_body, child, dst, dst_size, defval);
    free(child_body);
    return ok;
}

int json_get_string_array(const char *body, const char *key,
                          char *dst, size_t max_items, size_t item_len)
{
    const char *p = json_find_key(body, key);
    if (!p || *p != '[' || !dst || max_items == 0 || item_len == 0)
        return 0;
    p++;
    int count = 0;
    while (*p && *p != ']' && (size_t)count < max_items)
    {
        p = json_skip_ws(p);
        if (*p == ']') break;
        if (*p == '"')
        {
            char *slot = dst + (size_t)count * item_len;
            json_read_string(p, slot, item_len);
            count++;
            const char *e = json_skip_value(p);
            if (!e) break;
            p = json_skip_ws(e);
            if (*p == ',') p++;
        }
        else
        {
            const char *e = json_skip_value(p);
            if (!e) break;
            p = e;
            p = json_skip_ws(p);
            if (*p == ',') p++;
        }
    }
    return count;
}