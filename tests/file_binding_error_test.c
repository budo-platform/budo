#include "file/js_file_bindings.h"
#include "file/lua_file_bindings.h"

#include "lauxlib.h"
#include "lualib.h"
#include "quickjs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_fixture(const char *root, const char *name, const char *contents)
{
    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(contents, 1, strlen(contents), file) == strlen(contents));
    assert(fclose(file) == 0);
}

static void run_javascript(const char *assets_root, const char *files_root)
{
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = JS_NewContext(runtime);
    assert(runtime && context);

    JSValue global = JS_GetGlobalObject(context);
    assert(JS_SetPropertyStr(context, global, "sys", JS_NewObject(context)) >= 0);
    JS_FreeValue(context, global);

    JsFileContext *file_state = js_file_init(context, assets_root);
    assert(file_state);
    FileContext *files = js_file_context(file_state);
    assert(files);
    file_set_write_root(files, files_root);
    const char *script =
        "if (sys.assets === undefined) throw Error('assets namespace missing');\n"
        "if (sys.assets.writeText !== undefined || sys.assets.writeBinary !== undefined) throw Error('assets writable');\n"
        "const roots = sys.files.list();\n"
        "if (roots.length !== 2 || roots[0].name !== 'assets' || roots[1].name !== 'files') throw Error('virtual roots');\n"
        "if (!sys.files.isDirectory('assets/') || !sys.files.isDirectory('files/')) throw Error('mount directories');\n"
        "if (sys.files.readText('assets/asset.txt') !== 'asset') throw Error('asset read');\n"
        "if (sys.assets.readText('asset.txt') !== 'asset') throw Error('asset wrapper read');\n"
        "if (!sys.assets.exists('asset.txt') || sys.assets.size('asset.txt') !== 5) throw Error('asset wrapper query');\n"
        "if (sys.files.readText('assets/present.txt') !== null) throw Error('asset root leak');\n"
        "if (sys.files.readText('files/missing.txt') !== null) throw Error('missing read');\n"
        "if (!sys.files.getError()) throw Error('missing error');\n"
        "let traversalThrew = false;\n"
        "try { sys.files.readText('files/../escape.txt'); } catch (e) { traversalThrew = true; }\n"
        "if (!traversalThrew) throw Error('traversal did not throw');\n"
        "if (sys.files.readText('files/present.txt') !== 'present') throw Error('successful read');\n"
        "if (sys.files.getError() !== '') throw Error('stale read error');\n"
        "if (sys.files.writeText('files/blocked', 'x') !== null) throw Error('write failure');\n"
        "if (!sys.files.getError()) throw Error('write error');\n"
        "let assetWriteThrew = false;\n"
        "try { sys.files.writeText('assets/forbidden.txt', 'x'); } catch (e) { assetWriteThrew = true; }\n"
        "if (!assetWriteThrew) throw Error('asset write did not throw');\n"
        "if (typeof sys.files.writeText('files/written-js.txt', 'ok') !== 'string') throw Error('write success');\n"
        "if (sys.files.getError() !== '') throw Error('stale write error');\n";
    JSValue result = JS_Eval(context, script, strlen(script), "file_binding_test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(context);
        const char *message = JS_ToCString(context, exception);
        fprintf(stderr, "JavaScript file binding test failed: %s\n", message ? message : "exception");
        JS_FreeCString(context, message);
        JS_FreeValue(context, exception);
        assert(false);
    }
    JS_FreeValue(context, result);
    js_file_cleanup(file_state);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

static void run_lua(const char *assets_root, const char *files_root)
{
    lua_State *state = luaL_newstate();
    assert(state);
    luaL_openlibs(state);
    lua_newtable(state);
    lua_setglobal(state, "sys");

    FileContext *files = lua_file_init(state, assets_root);
    assert(files);
    file_set_write_root(files, files_root);
    const char *script =
        "assert(sys.assets ~= nil)\n"
        "assert(sys.assets.writeText == nil and sys.assets.writeBinary == nil)\n"
        "local roots = sys.files.list()\n"
        "assert(#roots == 2 and roots[1].name == 'assets' and roots[2].name == 'files')\n"
        "assert(sys.files.isDirectory('assets/') and sys.files.isDirectory('files/'))\n"
        "assert(sys.files.readText('assets/asset.txt') == 'asset')\n"
        "assert(sys.assets.readText('asset.txt') == 'asset')\n"
        "assert(sys.assets.exists('asset.txt') and sys.assets.size('asset.txt') == 5)\n"
        "assert(sys.files.readText('assets/present.txt') == nil)\n"
        "assert(sys.files.readText('files/missing.txt') == nil)\n"
        "assert(sys.files.getError() ~= '')\n"
        "local ok = pcall(sys.files.readText, 'files/../escape.txt')\n"
        "assert(not ok)\n"
        "assert(sys.files.readText('files/present.txt') == 'present')\n"
        "assert(sys.files.getError() == '')\n"
        "assert(sys.files.writeText('files/blocked', 'x') == nil)\n"
        "assert(sys.files.getError() ~= '')\n"
        "assert(not pcall(sys.files.writeText, 'assets/forbidden.txt', 'x'))\n"
        "assert(type(sys.files.writeText('files/written-lua.txt', 'ok')) == 'string')\n"
        "assert(sys.files.getError() == '')\n";
    if (luaL_dostring(state, script) != LUA_OK)
    {
        fprintf(stderr, "Lua file binding test failed: %s\n", lua_tostring(state, -1));
        assert(false);
    }
    lua_file_cleanup(files);
    lua_close(state);
}

int main(void)
{
    char temporary[] = "/tmp/budo-file-binding-XXXXXX";
    char *base = mkdtemp(temporary);
    assert(base);

    char assets_root[FILE_MAX_PATH];
    char files_root[FILE_MAX_PATH];
    snprintf(assets_root, sizeof(assets_root), "%s/assets", base);
    snprintf(files_root, sizeof(files_root), "%s/files", base);
    assert(mkdir(assets_root, 0700) == 0);
    assert(mkdir(files_root, 0700) == 0);
    write_fixture(assets_root, "asset.txt", "asset");
    write_fixture(files_root, "present.txt", "present");

    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/blocked", files_root);
    assert(mkdir(path, 0700) == 0);

    run_javascript(assets_root, files_root);
    run_lua(assets_root, files_root);

    snprintf(path, sizeof(path), "%s/asset.txt", assets_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/present.txt", files_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/written-js.txt", files_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/written-lua.txt", files_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/blocked", files_root);
    rmdir(path);
    rmdir(assets_root);
    rmdir(files_root);
    rmdir(base);
    puts("file binding error contract tests passed");
    return 0;
}