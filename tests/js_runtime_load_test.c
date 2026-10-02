#include "graphics/js_core_bindings.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char captured[8192];
static int saved_stderr = -1;
static char capture_path[4096];

static const char *write_script(const char *root, const char *name,
                                const char *source)
{
    static char path[4096];
    FILE *file;

    assert(snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path));
    file = fopen(path, "wb");
    assert(file);
    assert(fputs(source, file) >= 0);
    assert(fclose(file) == 0);
    return path;
}

static void capture_begin(const char *root)
{
    FILE *file;

    assert(snprintf(capture_path, sizeof(capture_path), "%s/stderr.txt", root) <
           (int)sizeof(capture_path));
    fflush(stderr);
    saved_stderr = dup(fileno(stderr));
    assert(saved_stderr >= 0);
    file = fopen(capture_path, "wb");
    assert(file);
    assert(dup2(fileno(file), fileno(stderr)) >= 0);
    fclose(file);
}

static const char *capture_end(void)
{
    FILE *file;
    size_t length;

    fflush(stderr);
    assert(dup2(saved_stderr, fileno(stderr)) >= 0);
    close(saved_stderr);
    saved_stderr = -1;
    file = fopen(capture_path, "rb");
    assert(file);
    length = fread(captured, 1, sizeof(captured) - 1, file);
    captured[length] = '\0';
    fclose(file);
    return captured;
}

static int count_occurrences(const char *text, const char *needle)
{
    int count = 0;
    for (const char *at = strstr(text, needle); at; at = strstr(at + 1, needle))
        count++;
    return count;
}

static const char *run_capturing(const char *root, const char *name,
                                 const char *source, const char *later,
                                 bool *loaded)
{
    JSRuntimeContext *runtime = js_runtime_create(root);

    assert(runtime);
    capture_begin(root);
    *loaded = js_runtime_load_file(runtime, write_script(root, name, source));
    if (*loaded && later)
    {
        assert(js_runtime_eval(runtime, later, "later.js"));
        js_runtime_execute_pending_jobs(runtime);
    }
    js_runtime_destroy(runtime);
    return capture_end();
}

static bool load(const char *root, const char *name, const char *source)
{
    JSRuntimeContext *runtime = js_runtime_create(root);
    bool loaded;

    assert(runtime);
    loaded = js_runtime_load_file(runtime, write_script(root, name, source));
    js_runtime_destroy(runtime);
    return loaded;
}

static bool load_and_check(const char *root, const char *name,
                           const char *source, const char *check)
{
    JSRuntimeContext *runtime = js_runtime_create(root);
    bool loaded;

    assert(runtime);
    loaded = js_runtime_load_file(runtime, write_script(root, name, source)) &&
             js_runtime_eval(runtime, check, "check.js");
    js_runtime_destroy(runtime);
    return loaded;
}

int main(int argc, char **argv)
{
    const char *root;

    assert(argc == 2);
    root = argv[1];

    assert(load_and_check(root, "ok.js", "globalThis.loaded = 1;\n",
                          "if (globalThis.loaded !== 1) throw Error('not run');"));
    assert(load_and_check(root, "ok_module.js",
                          "export const value = 2;\nglobalThis.loaded = value;\n",
                          "if (globalThis.loaded !== 2) throw Error('not run');"));
    assert(load_and_check(root, "ok_await.js",
                          "globalThis.loaded = await Promise.resolve(3);\n",
                          "if (globalThis.loaded !== 3) throw Error('not run');"));

    assert(!load(root, "syntax.js", "this is not javascript(\n"));
    assert(!load(root, "throw.js", "console.log('before');\nthrow new Error('boom');\n"));
    assert(!load(root, "missing_global.js", "setTimeoutThatDoesNotExist(() => {}, 10);\n"));
    assert(!load(root, "throw_module.js", "export const a = 1;\nthrow new Error('module boom');\n"));
    assert(!load(root, "throw.ts", "const n: number = 1;\nthrow new Error('ts boom ' + n);\n"));
    assert(!load(root, "rejected_await.js", "await Promise.reject(new Error('late boom'));\n"));

    {
        bool loaded;
        const char *output = run_capturing(root, "reported_once.js",
                                           "throw new Error('once');\n", NULL, &loaded);
        assert(!loaded);
        assert(count_occurrences(output, "once") >= 1);
        assert(strstr(output, "Exception: Error: once"));
        assert(!strstr(output, "Unhandled promise rejection"));
    }

    {
        bool loaded;
        const char *output = run_capturing(root, "reported_once_primitive.js",
                                           "throw 'plain string';\n", NULL, &loaded);
        assert(!loaded);
        assert(strstr(output, "Exception: plain string"));
        assert(!strstr(output, "Unhandled promise rejection"));
    }

    {
        bool loaded;
        const char *output = run_capturing(root, "unhandled.js",
                                           "Promise.reject(new Error('lost at load'));\n",
                                           NULL, &loaded);
        assert(loaded);
        assert(count_occurrences(output, "Unhandled promise rejection: Error: lost at load") == 1);
    }

    {
        bool loaded;
        const char *output = run_capturing(
            root, "handled.js",
            "Promise.reject(new Error('caught')).catch(() => {});\n"
            "const late = Promise.reject(new Error('caught later'));\n"
            "Promise.resolve().then(() => late.catch(() => {}));\n",
            NULL, &loaded);
        assert(loaded);
        assert(!strstr(output, "Unhandled promise rejection"));
    }

    {
        bool loaded;
        const char *output = run_capturing(
            root, "callback.js",
            "globalThis.later = () => Promise.resolve().then(() => { throw new Error('lost in callback'); });\n",
            "later();", &loaded);
        assert(loaded);
        assert(count_occurrences(output, "Unhandled promise rejection: Error: lost in callback") == 1);
    }

    puts("js_runtime_load_test: ok");
    return 0;
}