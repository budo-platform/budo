#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char *json_util_read_file(const char *path, size_t max_size);

const char *json_skip_ws(const char *p);

const char *json_find_key(const char *body, const char *key);

bool json_read_string(const char *p, char *dst, size_t dst_size);

const char *json_skip_value(const char *p);

char *json_object_body(const char *p);

bool json_get_string(const char *body, const char *key,
                     char *dst, size_t dst_size, const char *defval);

int json_get_int(const char *body, const char *key, int defval);

bool json_get_bool(const char *body, const char *key, bool defval);

bool json_get_nested_string(const char *body, const char *parent, const char *child,
                            char *dst, size_t dst_size, const char *defval);

int json_get_string_array(const char *body, const char *key,
                          char *dst, size_t max_items, size_t item_len);

#ifdef __cplusplus
}
#endif

#endif