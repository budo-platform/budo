#ifndef JS_SQLITE_BINDINGS_H
#define JS_SQLITE_BINDINGS_H

#include "quickjs.h"
#include "sqlite_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    SqliteContext *js_sqlite_init(JSContext *ctx, const char *project_dir);

    void js_sqlite_cleanup(SqliteContext *sqlite_ctx);

#ifdef __cplusplus
}
#endif

#endif