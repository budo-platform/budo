#ifndef BUDO_LUA_LLAMACPP_BINDINGS_H
#define BUDO_LUA_LLAMACPP_BINDINGS_H

typedef struct LuaLlamaCppContext LuaLlamaCppContext;
typedef struct FileContext FileContext;
#ifdef __cplusplus
extern "C"
{
#endif
    LuaLlamaCppContext *lua_llamacpp_init(void *lua_state, FileContext *files);
    void lua_llamacpp_poll(LuaLlamaCppContext *state);
    
    bool lua_llamacpp_has_pending_work(LuaLlamaCppContext *state);
    void lua_llamacpp_cancel_all(LuaLlamaCppContext *state);
    void lua_llamacpp_cleanup(LuaLlamaCppContext *state);
#ifdef __cplusplus
}
#endif
#endif