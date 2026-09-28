#ifndef BUDO_COLOR_UTIL_H
#define BUDO_COLOR_UTIL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool color_parse_hex_string(const char *str, uint32_t *out_color);

#ifdef __cplusplus
}
#endif

#endif