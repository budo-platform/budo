#include "graphics/js_canvas_bindings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void eval_direct(JSRuntimeContext *canvas, const char *script)
{
    JSValue result = JS_Eval(canvas->context, script, strlen(script),
                             "js_canvas_binding_state_test.js",
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(canvas->context);
        const char *message = JS_ToCString(canvas->context, exception);
        fprintf(stderr, "JavaScript canvas isolation failure: %s\n",
                message ? message : "exception");
        JS_FreeCString(canvas->context, message);
        JS_FreeValue(canvas->context, exception);
        assert(false);
    }
    JS_FreeValue(canvas->context, result);
}

static void test_timer_isolation(JSRuntimeContext *canvas_a,
                                 JSRuntimeContext *canvas_b)
{
    assert(js_runtime_eval(
        canvas_a,
        "globalThis.timerFires = 0;"
        "sys.timer.once(100000, function () {});",
        "timer_a.js"));
    assert(js_runtime_eval(
        canvas_b,
        "globalThis.timerFires = 0;"
        "sys.timer.once(100000, function () {});",
        "timer_b.js"));

    js_runtime_process_timers(canvas_a, 1000.0);
    js_runtime_process_timers(canvas_b, 5000.0);
    eval_direct(canvas_a,
                "sys.timer.once(50, function () { timerFires++; });");

    assert(canvas_a->timer_count == 2);
    assert(canvas_b->timer_count == 1);
    assert(canvas_a->timers[1].fire_time == 1050.0);
    assert(canvas_b->current_timestamp_ms == 5000.0);

    js_runtime_process_timers(canvas_a, 1049.0);
    eval_direct(canvas_a,
                "if (timerFires !== 0) throw Error('timer fired early');");
    js_runtime_process_timers(canvas_a, 1050.0);
    eval_direct(canvas_a,
                "if (timerFires !== 1) throw Error('timer did not fire');");
    assert(canvas_b->current_timestamp_ms == 5000.0);
}

static void test_resource_isolation(JSRuntimeContext *canvas_a,
                                    JSRuntimeContext *canvas_b)
{
    assert(js_runtime_eval(canvas_b,
                           "let missingFontThrew = false;"
                           "try { sys.font.load('isolation.ttf', 'isolation'); } "
                           "catch (error) { missingFontThrew = true; }"
                           "if (!missingFontThrew) "
                           "throw Error('font leaked into context B');"
                           "globalThis.pathId = sys.path.create();",
                           "resource_b.js"));
    eval_direct(canvas_a,
                "if (!sys.font.load('isolation.ttf', 'isolation')) "
                "throw Error('context A did not resolve its font');"
                "globalThis.pathId = sys.path.create();");

    assert(canvas_a->font_count == 1);
    assert(canvas_b->font_count == 0);
    assert(canvas_a->path_count == 1);
    assert(canvas_b->path_count == 1);
    assert(canvas_a->paths[0] != canvas_b->paths[0]);
    eval_direct(canvas_a,
                "if (pathId !== 0) throw Error('wrong path for context A');");
    eval_direct(canvas_b,
                "if (pathId !== 0) throw Error('wrong path for context B');");
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    JSRuntimeContext *canvas_a = js_runtime_create(argv[1]);
    JSRuntimeContext *canvas_b = js_runtime_create(argv[2]);

    assert(canvas_a && canvas_b);
    assert(JS_GetContextOpaque(canvas_a->context) == canvas_a);
    assert(JS_GetContextOpaque(canvas_b->context) == canvas_b);
    assert(JS_GetRuntimeOpaque(canvas_a->runtime) == canvas_a);
    assert(JS_GetRuntimeOpaque(canvas_b->runtime) == canvas_b);

    test_timer_isolation(canvas_a, canvas_b);
    test_resource_isolation(canvas_a, canvas_b);
    eval_direct(canvas_a, "console.log('canvas conformance');");

    js_runtime_destroy(canvas_b);
    eval_direct(canvas_a,
                "if (!sys.canvas.setFont('isolation')) "
                "throw Error('font owner did not survive peer cleanup');"
                "sys.path.reset(pathId);"
                "if (sys.path.create() !== 1) "
                "throw Error('resource owner did not survive peer cleanup');");
    assert(canvas_a->path_count == 2);
    js_runtime_destroy(canvas_a);
    puts("{\"runtime\":\"javascript\",\"operation\":\"canvas.resourceIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"window.instanceState\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"scheduling.callback\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"runtime.peerCleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"javascript\",\"operation\":\"console.call\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("JavaScript canvas binding state isolation test passed");
    return 0;
}