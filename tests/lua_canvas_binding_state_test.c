#include "graphics/lua_canvas_bindings.h"

#include "lauxlib.h"

#include <assert.h>
#include <stdio.h>

static void run_script(LuaCanvasContext *ctx, const char *script)
{
    lua_State *state = (lua_State *)ctx->L;
    int result = luaL_dostring(state, script);
    if (result != LUA_OK)
        fprintf(stderr, "Lua canvas state test failed: %s\n", lua_tostring(state, -1));
    assert(result == LUA_OK);
}

static void load_script(LuaCanvasContext *ctx, const char *root,
                        const char *script)
{
    char path[1024];
    int written = snprintf(path, sizeof(path), "%s/isolation.lua", root);
    assert(written > 0 && (size_t)written < sizeof(path));

    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(script, file) >= 0);
    assert(fclose(file) == 0);
    assert(lua_canvas_load_file(ctx, path));
    assert(remove(path) == 0);
}

static double read_number(LuaCanvasContext *ctx, const char *name)
{
    lua_State *state = (lua_State *)ctx->L;
    lua_getglobal(state, name);
    assert(lua_isnumber(state, -1));
    double value = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return value;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    LuaCanvasContext *first = lua_canvas_create(argv[1]);
    LuaCanvasContext *second = lua_canvas_create(argv[2]);
    assert(first && second);

    lua_canvas_set_context(first, NULL, NULL, NULL, 111, 211, 1.25f);
    lua_canvas_set_context(second, NULL, NULL, NULL, 122, 222, 2.5f);

    load_script(first, argv[1],
                "assert(sys.window.getWidth() == 111)\n"
                "assert(sys.window.getHeight() == 211)\n"
                "assert(sys.window.getDisplayDensity() == 1.25)\n"
                "assert(sys.font.load('isolation.ttf', 'isolation'))\n"
                "assert(sys.canvas.setBlendMode('src-over') == false)\n"
                "sys.animation.start(function(timestamp)\n"
                "  animation_timestamp = timestamp + 1\n"
                "end)\n");

    load_script(second, argv[2],
                "assert(sys.window.getWidth() == 122)\n"
                "assert(sys.window.getHeight() == 222)\n"
                "assert(sys.window.getDisplayDensity() == 2.5)\n"
                "assert(not sys.font.load('isolation.ttf', 'isolation'))\n"
                "assert(sys.canvas.getError() == '')\n"
                "assert(sys.canvas.setImageFilter('blur', 1) == false)\n"
                "sys.animation.start(function(timestamp)\n"
                "  animation_timestamp = timestamp + 2\n"
                "end)\n");

    run_script(first,
               "local message = sys.canvas.getError()\n"
               "assert(message:find('blend mode', 1, true))\n"
               "assert(sys.canvas.getError() == '')\n");
    run_script(second,
               "local message = sys.canvas.getError()\n"
               "assert(message:find('image filter', 1, true))\n"
               "assert(sys.canvas.getError() == '')\n");
    run_script(first, "console.log('canvas conformance')");

    assert(lua_canvas_has_animation(first));
    assert(lua_canvas_has_animation(second));
    assert(lua_canvas_call_animation(first, 10.0));
    assert(lua_canvas_call_animation(second, 20.0));
    assert(read_number(first, "animation_timestamp") == 11.0);
    assert(read_number(second, "animation_timestamp") == 22.0);

    lua_canvas_destroy(first);
    assert(lua_canvas_call_animation(second, 30.0));
    assert(read_number(second, "animation_timestamp") == 32.0);
    lua_canvas_destroy(second);

    puts("{\"runtime\":\"lua\",\"operation\":\"canvas.resourceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"window.instanceState\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"scheduling.callback\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"runtime.peerCleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"console.call\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("Lua canvas binding state isolation tests passed");
    return 0;
}