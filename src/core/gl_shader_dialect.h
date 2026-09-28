#ifndef BUDO_GL_SHADER_DIALECT_H
#define BUDO_GL_SHADER_DIALECT_H

#include <stdbool.h>

typedef enum
{
    BUDO_GLSL_TARGET_DESKTOP_GL150 = 0,
    BUDO_GLSL_TARGET_ES300 = 1
} BudoGLSLTarget;

char *budo_glsl_prepare(const char *source,
                            BudoGLSLTarget target,
                            bool require_app_dialect,
                            const char **error_message);

#endif