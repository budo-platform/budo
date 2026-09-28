#include "graphics/js_core_bindings.h"
#include "core/ts_strip.h"

#include <limits.h>

#ifdef __ANDROID__
#include <android/log.h>
#define JS_LOG_TAG "BudoJS"
#endif

static JSRuntimeContext *
js_runtime_context_from_js_context(JSContext *ctx)
{
    return ctx ? (JSRuntimeContext *)JS_GetContextOpaque(ctx) : NULL;
}

static JSRuntimeContext *js_runtime_context_from_js_runtime(JSRuntime *runtime)
{
    return runtime ? (JSRuntimeContext *)JS_GetRuntimeOpaque(runtime) : NULL;
}

#define JS_RUNTIME_CONTEXT (js_runtime_context_from_js_context(ctx))

static bool js_path_is_absolute(const char *path)
{
    if (!path || !path[0])
    {
        return false;
    }

    return path[0] == '/' || path[0] == '\\' || (strlen(path) > 1 && path[1] == ':');
}

bool js_resolve_project_path(JSContext *ctx, const char *path, char *out, size_t out_size)
{
    int written;

    if (!path || !out || out_size == 0)
    {
        return false;
    }

    if (js_path_is_absolute(path) || !JS_RUNTIME_CONTEXT || !JS_RUNTIME_CONTEXT->project_dir[0])
    {
        written = snprintf(out, out_size, "%s", path);
    }
    else
    {
        written = snprintf(out, out_size, "%s/%s", JS_RUNTIME_CONTEXT->project_dir, path);
    }

    return written > 0 && (size_t)written < out_size;
}

void js_dump_error(JSContext *ctx)
{
    JSValue exception = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exception);
    if (str)
    {
        fprintf(stderr, "Exception: %s\n", str);
        JS_FreeCString(ctx, str);
    }

    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsUndefined(stack))
    {
        const char *stack_str = JS_ToCString(ctx, stack);
        if (stack_str)
        {
            fprintf(stderr, "Stack: %s\n", stack_str);
            JS_FreeCString(ctx, stack_str);
        }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
}

char *js_strdup_local(const char *src)
{
    size_t len;
    char *copy;

    if (!src)
    {
        return NULL;
    }

    len = strlen(src);
    copy = (char *)malloc(len + 1);
    if (!copy)
    {
        return NULL;
    }

    memcpy(copy, src, len + 1);
    return copy;
}

static JSValue js_console_log(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
#ifdef __ANDROID__
    
    char buf[2048];
    int pos = 0;
    for (int i = 0; i < argc; i++)
    {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str)
        {
            if (i > 0 && pos < (int)sizeof(buf) - 1)
                buf[pos++] = ' ';
            int len = snprintf(buf + pos, sizeof(buf) - pos, "%s", str);
            if (len > 0)
                pos += len;
            JS_FreeCString(ctx, str);
        }
    }
    buf[pos < (int)sizeof(buf) ? pos : (int)sizeof(buf) - 1] = '\0';
    __android_log_print(ANDROID_LOG_INFO, JS_LOG_TAG, "%s", buf);
#else
    for (int i = 0; i < argc; i++)
    {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str)
        {
            printf("%s", str);
            if (i < argc - 1)
                printf(" ");
            JS_FreeCString(ctx, str);
        }
    }
    printf("\n");
#endif
    return JS_UNDEFINED;
}

static int js_add_timer(JSContext *ctx, JSValue callback, double delay_ms, double interval_ms)
{
    JSRuntimeContext *jctx = JS_RUNTIME_CONTEXT;
    if (!jctx)
        return -1;

    if (jctx->timer_count >= jctx->timer_capacity)
    {
        int new_cap = jctx->timer_capacity == 0 ? 8 : jctx->timer_capacity * 2;
        JSTimerEntry *new_timers = (JSTimerEntry *)realloc(jctx->timers,
                                                           new_cap * sizeof(JSTimerEntry));
        if (!new_timers)
            return -1;
        jctx->timers = new_timers;
        jctx->timer_capacity = new_cap;
    }

    int id = ++jctx->next_timer_id;
    JSTimerEntry *entry = &jctx->timers[jctx->timer_count++];
    entry->id = id;
    entry->callback = JS_DupValue(ctx, callback);
    entry->fire_time = jctx->current_timestamp_ms + (delay_ms > 0 ? delay_ms : 0);
    entry->interval_ms = interval_ms;
    entry->active = true;

    return id;
}

static JSValue js_timer_once(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    if (!JS_RUNTIME_CONTEXT || argc < 2)
        return JS_UNDEFINED;

    double delay = 0;
    JS_ToFloat64(ctx, &delay, argv[0]);

    if (!JS_IsFunction(ctx, argv[1]))
        return JS_UNDEFINED;

    int id = js_add_timer(ctx, argv[1], delay, 0);
    if (id < 0)
        return JS_UNDEFINED;

    return JS_NewInt32(ctx, id);
}

static JSValue js_timer_every(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    if (!JS_RUNTIME_CONTEXT || argc < 2)
        return JS_UNDEFINED;

    double interval = 0;
    JS_ToFloat64(ctx, &interval, argv[0]);
    
    if (interval < 1)
        interval = 1;

    if (!JS_IsFunction(ctx, argv[1]))
        return JS_UNDEFINED;

    int id = js_add_timer(ctx, argv[1], interval, interval);
    if (id < 0)
        return JS_UNDEFINED;

    return JS_NewInt32(ctx, id);
}

static JSValue js_timer_clear(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    if (!JS_RUNTIME_CONTEXT || argc < 1)
        return JS_UNDEFINED;

    int id;
    if (JS_ToInt32(ctx, &id, argv[0]))
        return JS_UNDEFINED;

    for (int i = 0; i < JS_RUNTIME_CONTEXT->timer_count; i++)
    {
        if (JS_RUNTIME_CONTEXT->timers[i].id == id && JS_RUNTIME_CONTEXT->timers[i].active)
        {
            JS_RUNTIME_CONTEXT->timers[i].active = false;
            JS_FreeValue(ctx, JS_RUNTIME_CONTEXT->timers[i].callback);
            JS_RUNTIME_CONTEXT->timers[i].callback = JS_UNDEFINED;
            break;
        }
    }

    return JS_UNDEFINED;
}

#undef JS_RUNTIME_CONTEXT

static char *js_module_normalizer(JSContext *ctx,
                                  const char *base_name,
                                  const char *module_name,
                                  void *opaque)
{
    JSRuntimeContext *jctx = js_runtime_context_from_js_context(ctx);
    char resolved[PATH_MAX];
    char real[PATH_MAX];
    char project_real[PATH_MAX];
    size_t project_len;

    if (!jctx)
    {
        JSRuntime *runtime = (JSRuntime *)opaque;
        jctx = js_runtime_context_from_js_runtime(runtime);
        if (jctx)
            ctx = jctx->context;
    }
    if (!ctx || !jctx)
        return NULL;

    if (!module_name || !module_name[0])
    {
        JS_ThrowReferenceError(ctx, "empty module specifier");
        return NULL;
    }

    if (!realpath(jctx->project_dir, project_real))
    {
        JS_ThrowReferenceError(ctx, "cannot resolve project directory");
        return NULL;
    }
    project_len = strlen(project_real);
    
    if (project_len > 0 && project_real[project_len - 1] != '/')
    {
        if (project_len + 1 < PATH_MAX)
        {
            project_real[project_len] = '/';
            project_real[project_len + 1] = '\0';
            project_len++;
        }
    }

    if (module_name[0] == '.' && base_name && base_name[0])
    {
        
        char base_copy[PATH_MAX];
        const char *dir;
        snprintf(base_copy, sizeof(base_copy), "%s", base_name);
        
        char *last_slash = strrchr(base_copy, '/');
        if (last_slash)
        {
            *last_slash = '\0';
            dir = base_copy;
        }
        else
        {
            dir = ".";
        }
        snprintf(resolved, sizeof(resolved), "%s/%s", dir, module_name);
    }
    else if (module_name[0] == '/')
    {
        
        JS_ThrowReferenceError(ctx, "absolute module paths are not allowed: '%s'", module_name);
        return NULL;
    }
    else
    {
        
        snprintf(resolved, sizeof(resolved), "%s/%s", jctx->project_dir, module_name);
    }

    if (!realpath(resolved, real))
    {
        
        size_t len = strlen(resolved);
        bool resolved_ok = false;
        if (len >= 3 && strcmp(resolved + len - 3, ".js") == 0)
        {
            
        }
        else if (len >= 3 && strcmp(resolved + len - 3, ".ts") == 0)
        {
            
        }
        else
        {
            char with_ext[PATH_MAX];
            snprintf(with_ext, sizeof(with_ext), "%s.js", resolved);
            if (realpath(with_ext, real))
            {
                resolved_ok = true;
            }
            else
            {
                snprintf(with_ext, sizeof(with_ext), "%s.ts", resolved);
                if (realpath(with_ext, real))
                    resolved_ok = true;
            }
        }
        if (!resolved_ok)
        {
            JS_ThrowReferenceError(ctx, "could not resolve module '%s'", module_name);
            return NULL;
        }
    }

    if (strncmp(real, project_real, project_len) != 0)
    {
        JS_ThrowReferenceError(ctx, "module '%s' is outside the project directory", module_name);
        return NULL;
    }

    char *result = js_malloc(ctx, strlen(real) + 1);
    if (!result)
        return NULL;
    strcpy(result, real);
    return result;
}

static JSModuleDef *js_module_loader_func(JSContext *ctx,
                                          const char *module_name,
                                          void *opaque)
{
    JSRuntimeContext *jctx;
    FILE *f;
    long size;
    char *buf;
    JSValue func_val;
    JSModuleDef *m;

    (void)opaque;
    if (!ctx)
        return NULL;
    jctx = js_runtime_context_from_js_context(ctx);
    if (!jctx)
    {
        JS_ThrowInternalError(ctx, "canvas context is unavailable while loading module");
        return NULL;
    }

    f = fopen(module_name, "rb");
    if (!f)
    {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", module_name);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (char *)malloc(size + 1);
    if (!buf)
    {
        fclose(f);
        JS_ThrowReferenceError(ctx, "out of memory loading module '%s'", module_name);
        return NULL;
    }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    if (ts_is_typescript(module_name))
    {
        char *js_buf = NULL;
        size_t js_len = 0;
        if (!ts_strip_types(buf, size, &js_buf, &js_len))
        {
            free(buf);
            JS_ThrowReferenceError(ctx, "TypeScript transpilation failed for '%s'", module_name);
            return NULL;
        }
        free(buf);
        buf = js_buf;
        size = (long)js_len;
    }

    func_val = JS_Eval(ctx, buf, size, module_name,
                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(buf);

    if (JS_IsException(func_val))
        return NULL;

    m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);
    return m;
}

static const JSCFunctionListEntry js_console_funcs[] = {
    JS_CFUNC_DEF("log", 0, js_console_log),
};

static const JSCFunctionListEntry js_sys_timer_funcs[] = {
    JS_CFUNC_DEF("once", 2, js_timer_once),
    JS_CFUNC_DEF("every", 2, js_timer_every),
    JS_CFUNC_DEF("clear", 1, js_timer_clear),
};

JSRuntimeContext *js_runtime_create(const char *project_dir)
{
    JSRuntimeContext *ctx = (JSRuntimeContext *)calloc(1, sizeof(JSRuntimeContext));
    if (!ctx)
        return NULL;

    ctx->runtime = JS_NewRuntime();
    if (!ctx->runtime)
    {
        free(ctx);
        return NULL;
    }

    ctx->context = JS_NewContext(ctx->runtime);
    if (!ctx->context)
    {
        JS_FreeRuntime(ctx->runtime);
        free(ctx);
        return NULL;
    }

    JS_SetRuntimeOpaque(ctx->runtime, ctx);
    JS_SetContextOpaque(ctx->context, ctx);

    if (project_dir)
    {
        strncpy(ctx->project_dir, project_dir, sizeof(ctx->project_dir) - 1);
        ctx->project_dir[sizeof(ctx->project_dir) - 1] = '\0';
    }

    JS_SetModuleLoaderFunc(ctx->runtime, js_module_normalizer,
                           js_module_loader_func, ctx->runtime);

    JSValue global = JS_GetGlobalObject(ctx->context);

    JSValue sys_obj = JS_NewObject(ctx->context);

    JSValue timer_obj = JS_NewObject(ctx->context);
    JS_SetPropertyFunctionList(ctx->context, timer_obj, js_sys_timer_funcs,
                               sizeof(js_sys_timer_funcs) / sizeof(js_sys_timer_funcs[0]));
    JS_SetPropertyStr(ctx->context, sys_obj, "timer", timer_obj);

    JSValue log_func = JS_NewCFunction(ctx->context, js_console_log, NULL, 0);
    if (JS_IsException(log_func))
    {
        JS_FreeValue(ctx->context, log_func);
        JS_SetContextOpaque(ctx->context, NULL);
        JS_SetRuntimeOpaque(ctx->runtime, NULL);
        JS_FreeContext(ctx->context);
        JS_FreeRuntime(ctx->runtime);
        free(ctx);
        return NULL;
    }
    JS_SetPropertyStr(ctx->context, sys_obj, "log", log_func);

    JS_SetPropertyStr(ctx->context, global, "sys", sys_obj);

    JSValue console_obj = JS_NewObject(ctx->context);
    JS_SetPropertyFunctionList(ctx->context, console_obj, js_console_funcs,
                               sizeof(js_console_funcs) / sizeof(js_console_funcs[0]));
    JS_SetPropertyStr(ctx->context, global, "console", console_obj);

    JS_FreeValue(ctx->context, global);

    return ctx;
}

void js_runtime_destroy(JSRuntimeContext *ctx)
{
    if (!ctx)
        return;

    JS_SetContextOpaque(ctx->context, NULL);
    JS_SetRuntimeOpaque(ctx->runtime, NULL);

    for (int i = 0; i < ctx->timer_count; i++)
    {
        if (ctx->timers[i].active)
        {
            JS_FreeValue(ctx->context, ctx->timers[i].callback);
        }
    }
    free(ctx->timers);

    JS_FreeContext(ctx->context);
    JS_FreeRuntime(ctx->runtime);

    free(ctx);
}

bool js_runtime_load_file(JSRuntimeContext *ctx, const char *filename)
{
    if (!ctx || !filename)
        return false;

    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *code = (char *)malloc(size + 1);
    if (!code)
    {
        fclose(f);
        return false;
    }

    fread(code, 1, size, f);
    code[size] = '\0';
    fclose(f);

    if (ts_is_typescript(filename))
    {
        char *js_code = NULL;
        size_t js_len = 0;
        if (!ts_strip_types(code, size, &js_code, &js_len))
        {
            fprintf(stderr, "TypeScript transpilation failed for '%s'\n", filename);
            free(code);
            return false;
        }
        free(code);
        code = js_code;
        size = (long)js_len;
    }

    int is_module = JS_DetectModule(code, size);

    if (is_module)
    {
        
        JSValue func_val = JS_Eval(ctx->context, code, size, filename,
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        free(code);

        if (JS_IsException(func_val))
        {
            js_dump_error(ctx->context);
            JS_FreeValue(ctx->context, func_val);
            return false;
        }

        JSValue result = JS_EvalFunction(ctx->context, func_val);
        if (JS_IsException(result))
        {
            js_dump_error(ctx->context);
            JS_FreeValue(ctx->context, result);
            return false;
        }

        JS_FreeValue(ctx->context, result);
        return true;
    }

    bool result = js_runtime_eval(ctx, code, filename);
    free(code);
    return result;
}

bool js_runtime_eval(JSRuntimeContext *ctx, const char *code, const char *filename)
{
    if (!ctx || !code)
        return false;

    JSValue result = JS_Eval(ctx->context, code, strlen(code),
                             filename ? filename : "<eval>", JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result))
    {
        js_dump_error(ctx->context);
        JS_FreeValue(ctx->context, result);
        return false;
    }

    JS_FreeValue(ctx->context, result);
    return true;
}

void js_runtime_execute_pending_jobs(JSRuntimeContext *ctx)
{
    if (!ctx)
        return;

    JSContext *pctx;
    while (JS_ExecutePendingJob(ctx->runtime, &pctx) > 0)
    {
        
    }
}

void js_runtime_process_timers(JSRuntimeContext *ctx, double timestamp_ms)
{
    if (!ctx)
        return;

    ctx->current_timestamp_ms = timestamp_ms;
    if (ctx->timer_count == 0)
        return;

    for (int i = 0; i < ctx->timer_count; i++)
    {
        JSTimerEntry *entry = &ctx->timers[i];
        if (!entry->active)
            continue;
        if (timestamp_ms < entry->fire_time)
            continue;

        JSValue cb = JS_DupValue(ctx->context, entry->callback);

        JSValue global = JS_GetGlobalObject(ctx->context);
        JSValue result = JS_Call(ctx->context, cb, global, 0, NULL);
        JS_FreeValue(ctx->context, global);

        if (JS_IsException(result))
            js_dump_error(ctx->context);
        JS_FreeValue(ctx->context, result);
        JS_FreeValue(ctx->context, cb);

        if (entry->interval_ms > 0 && entry->active)
        {
            
            entry->fire_time = timestamp_ms + entry->interval_ms;
        }
        else if (entry->active)
        {
            
            entry->active = false;
            JS_FreeValue(ctx->context, entry->callback);
            entry->callback = JS_UNDEFINED;
        }
    }

    while (ctx->timer_count > 0 && !ctx->timers[ctx->timer_count - 1].active)
    {
        ctx->timer_count--;
    }
}