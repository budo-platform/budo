#ifndef LUA_SQLITE_BINDINGS_H
#define LUA_SQLITE_BINDINGS_H

#include <stdbool.h>
#include "sqlite_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    SqliteContext *lua_sqlite_init(void *L, const char *project_dir);

    void lua_sqlite_cleanup(SqliteContext *sqlite_ctx);

#ifdef __cplusplus
}
#endif

#endif