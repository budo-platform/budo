#include "lua_network_bindings.h"
#include "network_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#define MAX_LUA_PENDING_FETCHES 64

typedef struct LuaFetchPending LuaFetchPending;

struct LuaNetworkContext
{
    NetworkContext *network_ctx;
    lua_State *lua_state;
    int token_ref;
    uint32_t generation;
    bool shutting_down;
    LuaFetchPending *pending;
};

static int json_parse_value(lua_State *L, const char *s, int pos, int len);

static int json_skip_whitespace(const char *s, int pos, int len)
{
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        pos++;
    return pos;
}

static int json_parse_string(lua_State *L, const char *s, int pos, int len)
{
    if (pos >= len || s[pos] != '"')
        return -1;
    pos++;

    luaL_Buffer b;
    luaL_buffinit(L, &b);

    while (pos < len && s[pos] != '"')
    {
        if (s[pos] == '\\' && pos + 1 < len)
        {
            pos++;
            switch (s[pos])
            {
            case '"':
                luaL_addchar(&b, '"');
                break;
            case '\\':
                luaL_addchar(&b, '\\');
                break;
            case '/':
                luaL_addchar(&b, '/');
                break;
            case 'n':
                luaL_addchar(&b, '\n');
                break;
            case 'r':
                luaL_addchar(&b, '\r');
                break;
            case 't':
                luaL_addchar(&b, '\t');
                break;
            case 'b':
                luaL_addchar(&b, '\b');
                break;
            case 'f':
                luaL_addchar(&b, '\f');
                break;
            default:
                luaL_addchar(&b, s[pos]);
                break;
            }
        }
        else
        {
            luaL_addchar(&b, s[pos]);
        }
        pos++;
    }

    if (pos >= len)
        return -1;
    pos++; 

    luaL_pushresult(&b);
    return pos;
}

static int json_parse_number(lua_State *L, const char *s, int pos, int len)
{
    int start = pos;
    bool is_float = false;

    if (pos < len && s[pos] == '-')
        pos++;
    while (pos < len && s[pos] >= '0' && s[pos] <= '9')
        pos++;
    if (pos < len && s[pos] == '.')
    {
        is_float = true;
        pos++;
        while (pos < len && s[pos] >= '0' && s[pos] <= '9')
            pos++;
    }
    if (pos < len && (s[pos] == 'e' || s[pos] == 'E'))
    {
        is_float = true;
        pos++;
        if (pos < len && (s[pos] == '+' || s[pos] == '-'))
            pos++;
        while (pos < len && s[pos] >= '0' && s[pos] <= '9')
            pos++;
    }

    if (pos == start)
        return -1;

    char buf[64];
    int num_len = pos - start;
    if (num_len >= (int)sizeof(buf))
        num_len = (int)sizeof(buf) - 1;
    memcpy(buf, s + start, num_len);
    buf[num_len] = '\0';

    if (is_float)
        lua_pushnumber(L, atof(buf));
    else
        lua_pushinteger(L, atoll(buf));

    return pos;
}

static int json_parse_array(lua_State *L, const char *s, int pos, int len)
{
    pos++; 
    lua_newtable(L);
    int idx = 1;

    pos = json_skip_whitespace(s, pos, len);
    if (pos < len && s[pos] == ']')
        return pos + 1;

    while (pos < len)
    {
        pos = json_skip_whitespace(s, pos, len);
        pos = json_parse_value(L, s, pos, len);
        if (pos < 0)
            return -1;

        lua_rawseti(L, -2, idx++);

        pos = json_skip_whitespace(s, pos, len);
        if (pos < len && s[pos] == ',')
            pos++;
        else if (pos < len && s[pos] == ']')
            return pos + 1;
        else
            return -1;
    }
    return -1;
}

static int json_parse_object(lua_State *L, const char *s, int pos, int len)
{
    pos++; 
    lua_newtable(L);

    pos = json_skip_whitespace(s, pos, len);
    if (pos < len && s[pos] == '}')
        return pos + 1;

    while (pos < len)
    {
        pos = json_skip_whitespace(s, pos, len);
        
        pos = json_parse_string(L, s, pos, len);
        if (pos < 0)
            return -1;

        pos = json_skip_whitespace(s, pos, len);
        if (pos >= len || s[pos] != ':')
        {
            lua_pop(L, 1); 
            return -1;
        }
        pos++;

        pos = json_skip_whitespace(s, pos, len);
        pos = json_parse_value(L, s, pos, len);
        if (pos < 0)
        {
            lua_pop(L, 1); 
            return -1;
        }

        lua_settable(L, -3);

        pos = json_skip_whitespace(s, pos, len);
        if (pos < len && s[pos] == ',')
            pos++;
        else if (pos < len && s[pos] == '}')
            return pos + 1;
        else
            return -1;
    }
    return -1;
}

static int json_parse_value(lua_State *L, const char *s, int pos, int len)
{
    pos = json_skip_whitespace(s, pos, len);
    if (pos >= len)
        return -1;

    switch (s[pos])
    {
    case '"':
        return json_parse_string(L, s, pos, len);
    case '{':
        return json_parse_object(L, s, pos, len);
    case '[':
        return json_parse_array(L, s, pos, len);
    case 't': 
        if (pos + 4 <= len && strncmp(s + pos, "true", 4) == 0)
        {
            lua_pushboolean(L, 1);
            return pos + 4;
        }
        return -1;
    case 'f': 
        if (pos + 5 <= len && strncmp(s + pos, "false", 5) == 0)
        {
            lua_pushboolean(L, 0);
            return pos + 5;
        }
        return -1;
    case 'n': 
        if (pos + 4 <= len && strncmp(s + pos, "null", 4) == 0)
        {
            lua_pushnil(L);
            return pos + 4;
        }
        return -1;
    default:
        if (s[pos] == '-' || (s[pos] >= '0' && s[pos] <= '9'))
            return json_parse_number(L, s, pos, len);
        return -1;
    }
}

struct LuaFetchPending
{
    int callback_ref; 
    LuaNetworkContext *state;
    uint32_t generation;
    bool active;
};

static LuaNetworkContext *lua_network_binding_state(lua_State *L)
{
    return (LuaNetworkContext *)lua_touserdata(L, lua_upvalueindex(1));
}

static void lua_fetch_async_callback(int request_id, NetworkResponse *response,
                                     const char *error, void *user_data)
{
    (void)request_id;
    LuaFetchPending *pending = (LuaFetchPending *)user_data;
    LuaNetworkContext *state = pending->state;

    if (!pending->active || !state || state->shutting_down ||
        state->generation != pending->generation || !state->lua_state)
    {
        network_response_free(response);
        return;
    }
    lua_State *L = state->lua_state;

    lua_rawgeti(L, LUA_REGISTRYINDEX, pending->callback_ref);

    if (response)
    {
        
        lua_newtable(L);

        lua_pushinteger(L, response->status);
        lua_setfield(L, -2, "status");
        lua_pushstring(L, response->status_text ? response->status_text : "");
        lua_setfield(L, -2, "statusText");
        lua_pushboolean(L, response->status >= 200 && response->status < 300);
        lua_setfield(L, -2, "ok");
        lua_pushstring(L, response->url ? response->url : "");
        lua_setfield(L, -2, "url");
        lua_pushboolean(L, response->redirected);
        lua_setfield(L, -2, "redirected");

        if (response->body && response->body_len > 0)
            lua_pushlstring(L, (const char *)response->body, response->body_len);
        else
            lua_pushstring(L, "");
        lua_setfield(L, -2, "body");

        lua_pushinteger(L, (lua_Integer)response->body_len);
        lua_setfield(L, -2, "bodyLen");

        lua_newtable(L);
        for (int i = 0; i < response->header_count; i++)
        {
            if (response->headers[i].name && response->headers[i].value)
            {
                lua_pushstring(L, response->headers[i].value);
                lua_setfield(L, -2, response->headers[i].name);
            }
        }
        lua_setfield(L, -2, "headers");

        network_response_free(response);

        lua_pushnil(L);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK)
        {
            fprintf(stderr, "fetch callback error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    else
    {
        
        lua_pushnil(L);
        lua_pushstring(L, error ? error : "fetch failed");
        if (lua_pcall(L, 2, 0, 0) != LUA_OK)
        {
            fprintf(stderr, "fetch callback error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    luaL_unref(L, LUA_REGISTRYINDEX, pending->callback_ref);
    pending->active = false;
}

static int l_fetch(lua_State *L)
{
    LuaNetworkContext *state = lua_network_binding_state(L);
    if (!state || state->shutting_down || !state->network_ctx ||
        state->lua_state != L)
        return luaL_error(L, "Network API not initialized");

    if (!network_is_enabled(state->network_ctx))
        return luaL_error(L, "Network access denied: no \"network\" policy in app.json");

    const char *url = luaL_checkstring(L, 1);

    int callback_idx;
    int options_idx = 0;

    if (lua_gettop(L) >= 3 && lua_istable(L, 2) && lua_isfunction(L, 3))
    {
        options_idx = 2;
        callback_idx = 3;
    }
    else if (lua_gettop(L) >= 2 && lua_isfunction(L, 2))
    {
        callback_idx = 2;
    }
    else
    {
        return luaL_error(L, "fetch requires a callback function as the last argument");
    }

    const char *method = "GET";
    NetworkHeader req_headers[64];
    int req_header_count = 0;
    const char *body_str = NULL;
    size_t body_len = 0;

    if (options_idx > 0)
    {
        
        lua_getfield(L, options_idx, "method");
        if (lua_isstring(L, -1))
            method = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "headers");
        if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0 && req_header_count < 64)
            {
                if (lua_isstring(L, -2) && lua_isstring(L, -1))
                {
                    req_headers[req_header_count].name = strdup(lua_tostring(L, -2));
                    req_headers[req_header_count].value = strdup(lua_tostring(L, -1));
                    req_header_count++;
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, options_idx, "body");
        if (lua_isstring(L, -1))
            body_str = lua_tolstring(L, -1, &body_len);
        lua_pop(L, 1);
    }

    LuaFetchPending *pending = NULL;
    for (int i = 0; i < MAX_LUA_PENDING_FETCHES; i++)
    {
        if (!state->pending[i].active)
        {
            pending = &state->pending[i];
            break;
        }
    }
    if (!pending)
    {
        for (int i = 0; i < req_header_count; i++)
        {
            free(req_headers[i].name);
            free(req_headers[i].value);
        }
        return luaL_error(L, "Too many pending fetch requests");
    }

    lua_pushvalue(L, callback_idx);
    int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    pending->callback_ref = callback_ref;
    pending->state = state;
    pending->generation = state->generation;
    pending->active = true;

    ApiError api_error;
    int req_id = network_service_request_async(
        state->network_ctx, method, url,
        (const NetworkHeader *)req_headers, req_header_count,
        (const uint8_t *)body_str, body_len,
        lua_fetch_async_callback, pending, &api_error);

    for (int i = 0; i < req_header_count; i++)
    {
        free(req_headers[i].name);
        free(req_headers[i].value);
    }

    if (req_id < 0)
    {
        
        lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);
        lua_pushnil(L);
        lua_pushstring(L, network_get_error(state->network_ctx));
        if (lua_pcall(L, 2, 0, 0) != LUA_OK)
        {
            fprintf(stderr, "fetch callback error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, callback_ref);
        pending->active = false;
    }

    return 0;
}

static int l_json_parse(lua_State *L)
{
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);

    int pos = json_parse_value(L, s, 0, (int)len);
    if (pos < 0)
        return luaL_error(L, "Failed to parse JSON");

    return 1;
}

static const luaL_Reg network_funcs[] = {
    {"fetch", l_fetch},
    {NULL, NULL}};

LuaNetworkContext *lua_network_init(void *L_void, const char *project_dir)
{
    lua_State *L = (lua_State *)L_void;
    LuaNetworkContext *state =
        (LuaNetworkContext *)lua_newuserdata(L, sizeof(*state));
    memset(state, 0, sizeof(*state));
    state->lua_state = L;
    state->token_ref = LUA_NOREF;
    state->generation = 1;
    state->pending =
        (LuaFetchPending *)calloc(MAX_LUA_PENDING_FETCHES,
                                  sizeof(*state->pending));
    if (!state->pending)
    {
        lua_pop(L, 1);
        return NULL;
    }

    NetworkPolicy policy;
    network_policy_load_app_json(&policy, project_dir);

    state->network_ctx = network_create(&policy);
    if (!state->network_ctx)
    {
        fprintf(stderr, "Failed to create network context\n");
        free(state->pending);
        state->pending = NULL;
        lua_pop(L, 1);
        return NULL;
    }

    lua_pushvalue(L, -1);
    state->token_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, state->token_ref);
    luaL_setfuncs(L, network_funcs, 1);
    lua_setfield(L, -2, "network");

    lua_pop(L, 1); 

    lua_rawgeti(L, LUA_REGISTRYINDEX, state->token_ref);
    lua_pushcclosure(L, l_fetch, 1);
    lua_setglobal(L, "fetch");

    lua_pushcfunction(L, l_json_parse);
    lua_setglobal(L, "json_parse");

    if (network_is_enabled(state->network_ctx))
    {
        if (policy.allow_all)
            printf("Network: enabled (all domains)\n");
        else
            printf("Network: enabled (%d domain(s))\n", policy.domain_count);
    }

    return state;
}

NetworkContext *lua_network_context(LuaNetworkContext *state)
{
    return state ? state->network_ctx : NULL;
}

void lua_network_poll(LuaNetworkContext *state)
{
    if (state && !state->shutting_down && state->network_ctx)
        network_async_poll(state->network_ctx);
}

void lua_network_cleanup(LuaNetworkContext *state)
{
    if (!state || state->shutting_down)
        return;

    state->shutting_down = true;
    state->generation++;

    network_shutdown(state->network_ctx);
    if (state->lua_state)
    {
        for (int i = 0; i < MAX_LUA_PENDING_FETCHES; i++)
        {
            LuaFetchPending *pending = &state->pending[i];
            if (!pending->active)
                continue;
            luaL_unref(state->lua_state, LUA_REGISTRYINDEX,
                       pending->callback_ref);
            pending->active = false;
        }
        if (state->token_ref != LUA_NOREF)
            luaL_unref(state->lua_state, LUA_REGISTRYINDEX, state->token_ref);
    }

    state->token_ref = LUA_NOREF;
    state->lua_state = NULL;
    network_destroy(state->network_ctx);
    state->network_ctx = NULL;
    free(state->pending);
    state->pending = NULL;
}