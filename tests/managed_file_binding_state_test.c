#include "file/js_file_bindings.h"

#include "quickjs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct JsFileRuntime
{
    JSRuntime *runtime;
    JSContext *context;
    JsFileContext *file_state;
} JsFileRuntime;

static JsFileBridgeToken picker_token;
static char picker_extension[32];

bool budo_test_file_pick_text(JsFileBridgeToken token, const char *extension)
{
    picker_token = token;
    snprintf(picker_extension, sizeof(picker_extension), "%s",
             extension ? extension : "");
    return true;
}

bool budo_test_file_save_text(JsFileBridgeToken token, const char *name,
                              const char *text)
{
    (void)token;
    (void)name;
    (void)text;
    return false;
}

static void write_fixture(const char *root, const char *contents)
{
    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/identity.txt", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(contents, 1, strlen(contents), file) == strlen(contents));
    assert(fclose(file) == 0);
}

static void remove_fixture(const char *root)
{
    char path[FILE_MAX_PATH];
    snprintf(path, sizeof(path), "%s/identity.txt", root);
    assert(remove(path) == 0);
}

static void eval_ok(JSContext *context, const char *script)
{
    JSValue result = JS_Eval(context, script, strlen(script),
                             "managed_file_binding_state_test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(context);
        const char *message = JS_ToCString(context, exception);
        fprintf(stderr, "JavaScript file state test failed: %s\n",
                message ? message : "exception");
        JS_FreeCString(context, message);
        JS_FreeValue(context, exception);
        assert(false);
    }
    JS_FreeValue(context, result);
}

static void js_file_runtime_create(JsFileRuntime *runtime, const char *root)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->runtime = JS_NewRuntime();
    assert(runtime->runtime);
    runtime->context = JS_NewContext(runtime->runtime);
    assert(runtime->context);

    JSValue global = JS_GetGlobalObject(runtime->context);
    assert(JS_SetPropertyStr(runtime->context, global, "sys",
                             JS_NewObject(runtime->context)) >= 0);
    JS_FreeValue(runtime->context, global);

    runtime->file_state = js_file_init(runtime->context, root);
    assert(runtime->file_state);
}

static void js_file_runtime_destroy(JsFileRuntime *runtime)
{
    js_file_cleanup(runtime->file_state);
    JS_FreeContext(runtime->context);
    JS_FreeRuntime(runtime->runtime);
    memset(runtime, 0, sizeof(*runtime));
}

static void assert_root_contents(JsFileRuntime *runtime, const char *contents)
{
    char script[256];
    snprintf(script, sizeof(script),
             "if (sys.assets.readText('identity.txt') !== '%s') "
             "throw Error('wrong file root');",
             contents);
    eval_ok(runtime->context, script);
}

static void test_registration_failure_lifetime(const char *root)
{
    JSRuntime *runtime = JS_NewRuntime();
    assert(runtime);
    JSContext *context = JS_NewContext(runtime);
    assert(context);

    eval_ok(context,
            "globalThis.sys = new Proxy({}, {"
            "  set(target, property, value) {"
            "    if (property === 'assets') throw Error('blocked assets');"
            "    target[property] = value;"
            "    return true;"
            "  }"
            "});");
    assert(!js_file_init(context, root));
    JSValue exception = JS_GetException(context);
    assert(!JS_IsUndefined(exception));
    JS_FreeValue(context, exception);
    eval_ok(context,
            "if (typeof sys.files.readText !== 'function') "
            "throw Error('partially installed file object was corrupted');");

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

int main(int argc, char **argv)
{
    JsFileRuntime runtime_a;
    JsFileRuntime runtime_b;
    JsFileRuntime runtime_c;

    assert(argc == 4);
    write_fixture(argv[1], "root-a");
    write_fixture(argv[2], "root-b");
    write_fixture(argv[3], "root-c");
    test_registration_failure_lifetime(argv[1]);

    js_file_runtime_create(&runtime_a, argv[1]);
    js_file_runtime_create(&runtime_b, argv[2]);
    assert_root_contents(&runtime_a, "root-a");
    assert_root_contents(&runtime_b, "root-b");

    JsFileBridgeToken stale_token = js_file_bridge_token(runtime_a.file_state);
    JsFileBridgeToken surviving_token =
        js_file_bridge_token(runtime_b.file_state);
    assert(stale_token && surviving_token && stale_token != surviving_token);
    js_file_runtime_destroy(&runtime_a);
    assert_root_contents(&runtime_b, "root-b");

    js_file_runtime_create(&runtime_c, argv[3]);
    JsFileBridgeToken current_token = js_file_bridge_token(runtime_c.file_state);
    assert(current_token && current_token != stale_token);
    assert((current_token & 0xffu) == (stale_token & 0xffu));

    picker_token = 0;
    picker_extension[0] = '\0';
    eval_ok(runtime_c.context,
            "globalThis.pickerCalls = 0;"
            "globalThis.pickerResult = null;"
            "if (!sys.files.pickText((file, error) => {"
            "  if (error) throw Error(error);"
            "  pickerCalls++;"
            "  pickerResult = file;"
            "}, '.txt')) throw Error('picker did not launch');");
    assert(picker_token == current_token);
    assert(strcmp(picker_extension, ".txt") == 0);
    assert(!js_file_picker_complete(stale_token, "stale.txt", "stale", NULL));
    assert(js_file_picker_complete(current_token, "current.txt", "current",
                                   NULL));
    eval_ok(runtime_c.context,
            "if (pickerCalls !== 0 || pickerResult !== null) "
            "throw Error('picker callback ran before poll');");
    js_file_poll(runtime_c.file_state);
    eval_ok(runtime_c.context,
            "if (pickerCalls !== 1 || pickerResult.name !== 'current.txt' || "
            "pickerResult.text !== 'current') "
            "throw Error('picker callback result');");

    assert_root_contents(&runtime_b, "root-b");
    js_file_runtime_destroy(&runtime_c);
    js_file_runtime_destroy(&runtime_b);

    remove_fixture(argv[1]);
    remove_fixture(argv[2]);
    remove_fixture(argv[3]);
    puts("managed file binding state isolation test passed");
    return 0;
}