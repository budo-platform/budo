#ifndef LUA_FILE_BINDINGS_H
#define LUA_FILE_BINDINGS_H

#include <stdbool.h>
#include "file_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    FileContext *lua_file_init(void *L, const char *root_dir);

    void lua_file_cleanup(FileContext *file_ctx);

#ifdef __cplusplus
}
#endif

#endif