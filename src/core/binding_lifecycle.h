#ifndef BUDO_BINDING_LIFECYCLE_H
#define BUDO_BINDING_LIFECYCLE_H

#include <stdbool.h>
#include <stdio.h>

#define BINDING_LAZY_CTX(prefix, type, create_expr)                     \
    static type *g_##prefix##_ctx = NULL;                               \
    static bool g_##prefix##_lazy_initialized = false;                  \
    static void ensure_##prefix##_ctx(void)                             \
    {                                                                   \
        if (g_##prefix##_lazy_initialized)                              \
            return;                                                     \
        g_##prefix##_lazy_initialized = true;                           \
        g_##prefix##_ctx = (create_expr);                               \
        if (!g_##prefix##_ctx)                                          \
            fprintf(stderr,                                             \
                    "Warning: Failed to create " #prefix " context\n"); \
    }

#endif