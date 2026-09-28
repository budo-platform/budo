#include "graphics/js_core_bindings.h"
#include "quickjs.h"

#ifndef JS_TOOLS_H
#define JS_TOOLS_H

bool js_resolve_project_path(JSContext *ctx, const char *path, char *out, size_t out_size);
char *js_strdup_local(const char *src);
void js_dump_error(JSContext *ctx);

#endif