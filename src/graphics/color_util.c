#include "graphics/color_util.h"
#include "graphics/skia_wrapper.h"

#include <stdio.h>
#include <string.h>

bool color_parse_hex_string(const char *str, uint32_t *out_color)
{
    if (!str || !out_color || str[0] != '#')
        return false;

    size_t len = strlen(str);
    if (len == 7)
    {
        unsigned r, g, b;
        if (sscanf(str + 1, "%02x%02x%02x", &r, &g, &b) == 3)
        {
            *out_color = SKIA_COLOR_RGB(r, g, b);
            return true;
        }
    }
    else if (len == 9)
    {
        unsigned r, g, b, a;
        if (sscanf(str + 1, "%02x%02x%02x%02x", &r, &g, &b, &a) == 4)
        {
            *out_color = SKIA_COLOR_ARGB(a, r, g, b);
            return true;
        }
    }
    return false;
}