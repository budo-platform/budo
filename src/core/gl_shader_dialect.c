#include "core/gl_shader_dialect.h"

#include <stdlib.h>
#include <string.h>

static bool source_starts_with(const char *source, const char *prefix)
{
    return source && prefix && strncmp(source, prefix, strlen(prefix)) == 0;
}

static void append_text(char **cursor, const char *text)
{
    size_t len = strlen(text);
    memcpy(*cursor, text, len);
    *cursor += len;
}

static bool line_starts_with_token(const char *line, size_t line_len, const char *token)
{
    size_t token_len = strlen(token);
    size_t offset = 0;

    while (offset < line_len && (line[offset] == ' ' || line[offset] == '\t'))
        offset++;

    return line_len - offset >= token_len &&
           strncmp(line + offset, token, token_len) == 0 &&
           (line_len == offset + token_len || line[offset + token_len] == ' ' || line[offset + token_len] == '\t');
}

static char *dup_source(const char *source)
{
    size_t len;
    char *copy;

    if (!source)
        return NULL;
    len = strlen(source);
    copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, source, len + 1);
    return copy;
}

static char *transpile_es300_to_desktop_gl150(const char *source)
{
    const char *scan;
    char *out;
    char *cursor;

    out = (char *)malloc(strlen(source) * 2 + 128);
    if (!out)
        return NULL;

    cursor = out;
    append_text(&cursor, "#version 150\n");

    scan = source;
    while (*scan)
    {
        const char *line_start = scan;
        const char *line_end = strchr(scan, '\n');
        size_t line_len;

        if (line_end)
        {
            line_len = (size_t)(line_end - line_start + 1);
            scan = line_end + 1;
        }
        else
        {
            line_len = strlen(line_start);
            scan = line_start + line_len;
        }

        if (line_starts_with_token(line_start, line_len, "#version") ||
            line_starts_with_token(line_start, line_len, "precision"))
        {
            continue;
        }

        memcpy(cursor, line_start, line_len);
        cursor += line_len;
    }

    *cursor = '\0';
    return out;
}

char *budo_glsl_prepare(const char *source,
                            BudoGLSLTarget target,
                            bool require_app_dialect,
                            const char **error_message)
{
    bool is_es300;

    if (error_message)
        *error_message = NULL;
    if (!source)
    {
        if (error_message)
            *error_message = "Shader source is null";
        return NULL;
    }

    is_es300 = source_starts_with(source, "#version 300 es");
    if (require_app_dialect && !is_es300)
    {
        if (error_message)
            *error_message = "Shader source must start with #version 300 es";
        return NULL;
    }

    if (target == BUDO_GLSL_TARGET_DESKTOP_GL150)
    {
        if (is_es300)
            return transpile_es300_to_desktop_gl150(source);
        return dup_source(source);
    }

    return dup_source(source);
}