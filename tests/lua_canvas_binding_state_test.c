#include "graphics/lua_canvas_bindings.h"
#include "file/file_wrapper.h"

#include "lauxlib.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

static const unsigned char image_bytes[] = {
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 40,
    0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};

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

    FileContext *first_files = file_create(argv[1]);
    FileContext *second_files = file_create(argv[2]);
    assert(first_files && second_files);
    first->image_files = &first_files;
    second->image_files = &second_files;
    lua_pushlstring(first->L, (const char *)image_bytes, sizeof(image_bytes));
    lua_setglobal(first->L, "image_bytes");
    run_script(first,
               "assert(sys.gl.isAsyncTextureLoadingAvailable())\n"
               "assert(not pcall(sys.gl.loadTexture2DAsync, '../escape', {}, {onComplete=function() end, onError=function() end}))\n"
               "assert(not pcall(sys.gl.loadTexture2DAsync, 'image', {mount='files\\0bad'}, {onComplete=function() end, onError=function() end}))\n"
               "assert(not pcall(sys.gl.loadTexture2DFromBufferAsync, image_bytes, {maxWidth=0}, {onComplete=function() end, onError=function() end}))\n"
               "metadata = 0; terminal = 0\n"
               "request = sys.gl.loadTexture2DFromBufferAsync(image_bytes, {maxWidth=1}, {\n"
               " onMetadata=function(info) assert(info.width == 1 and info.sourceWidth == 2); metadata=metadata+1; assert(sys.gl.cancelTextureLoad(request)) end,\n"
               " onComplete=function() terminal=terminal+1 end, onError=function() terminal=terminal+1 end})\n"
               "assert(metadata == 0 and terminal == 0)\n");
    run_script(second,
               "errors=0; cancelled=0\n"
               "local callbacks={onComplete=function() error('unexpected complete') end, onError=function(err) assert(err.code == 'image.invalid_data'); errors=errors+1; error('expected callback failure') end}\n"
               "bad=sys.gl.loadTexture2DFromBufferAsync('broken', {}, callbacks)\n"
               "other=sys.gl.loadTexture2DFromBufferAsync('broken', {}, {onComplete=function() cancelled=cancelled+1 end, onError=function() cancelled=cancelled+1 end})\n"
               "assert(sys.gl.cancelTextureLoad(other)); assert(not sys.gl.cancelTextureLoad(other))\n");
    clock_t started = clock();
    while (read_number(first, "metadata") == 0 || read_number(second, "errors") == 0)
    {
        lua_image_poll(first);
        lua_image_poll(second);
        assert((double)(clock() - started) / CLOCKS_PER_SEC < 10);
    }
    run_script(first, "assert(terminal == 0 and not sys.gl.cancelTextureLoad(request))");
    run_script(second, "assert(errors == 1 and cancelled == 0 and not sys.gl.cancelTextureLoad(bad))");

    InputState input;
    input_init(&input);
    lua_canvas_set_context(first, NULL, &input, NULL, 111, 211, 1.25f);
    lua_canvas_set_context(second, NULL, NULL, NULL, 122, 222, 2.5f);
    run_script(first, "assert(sys.input.get().pinch == nil)");
    input_add_pinch(&input, 30, 45, 0.875);
    input_set_mouse_wheel(&input, 0, 2);
    run_script(first,
               "local input = sys.input.get()\n"
               "assert(input.pinch.x == 30 and input.pinch.y == 45 and input.pinch.scale == 0.875)\n"
               "assert(input.mouse.wheelY == 2 and not input.pointer.down and #input.pointers == 0)\n");
    input_begin_frame(&input);
    run_script(first, "assert(sys.input.get().pinch == nil)");

    load_script(first, argv[1],
                "assert(sys.window.getWidth() == 111)\n"
                "assert(sys.window.getHeight() == 211)\n"
                "assert(sys.window.getDisplayDensity() == 1.25)\n"
                "assert(sys.font.load('isolation.ttf', 'isolation'))\n"
                "assert(sys.canvas.setBlendMode('src-over') == true)\n"
                "sys.animation.start(function(timestamp)\n"
                "  animation_timestamp = timestamp + 1\n"
                "end)\n");

    load_script(second, argv[2],
                "assert(sys.window.getWidth() == 122)\n"
                "assert(sys.window.getHeight() == 222)\n"
                "assert(sys.window.getDisplayDensity() == 2.5)\n"
                "assert(not sys.font.load('isolation.ttf', 'isolation'))\n"
                "assert(sys.canvas.setImageFilter('blur', 1) == true)\n"
                "sys.animation.start(function(timestamp)\n"
                "  animation_timestamp = timestamp + 2\n"
                "end)\n");

    run_script(first, "console.log('canvas conformance')");

    assert(lua_canvas_has_animation(first));
    assert(lua_canvas_has_animation(second));
    assert(lua_canvas_call_animation(first, 10.0));
    assert(lua_canvas_call_animation(second, 20.0));
    assert(read_number(first, "animation_timestamp") == 11.0);
    assert(read_number(second, "animation_timestamp") == 22.0);

    run_script(first, "sys.gl.loadTexture2DFromBufferAsync('broken', {}, {onComplete=function() error('unexpected') end, onError=function() error('unexpected') end})");
    run_script(second, "peer_errors=0; sys.gl.loadTexture2DFromBufferAsync('broken', {}, {onComplete=function() error('unexpected') end, onError=function() peer_errors=peer_errors+1 end})");
    lua_canvas_destroy(first);
    file_destroy(first_files);
    started = clock();
    while (read_number(second, "peer_errors") == 0)
    {
        lua_image_poll(second);
        assert((double)(clock() - started) / CLOCKS_PER_SEC < 10);
    }
    run_script(second, "assert(peer_errors == 1)");
    assert(lua_canvas_call_animation(second, 30.0));
    assert(read_number(second, "animation_timestamp") == 32.0);
    lua_canvas_destroy(second);
    file_destroy(second_files);

    puts("{\"runtime\":\"lua\",\"operation\":\"canvas.resourceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"window.instanceState\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"scheduling.callback\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"runtime.peerCleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"lua\",\"operation\":\"console.call\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("Lua canvas binding state isolation tests passed");
    return 0;
}