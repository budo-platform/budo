#ifndef TS_STRIP_H
#define TS_STRIP_H

#include <stdbool.h>
#include <stddef.h>

bool ts_strip_types(const char *ts_src, size_t ts_len,
                    char **out_js, size_t *out_len);

bool ts_is_typescript(const char *filename);

#endif