#ifndef JS_CORE_BINDINGS_H
#define JS_CORE_BINDINGS_H

#include "quickjs.h"
#include "core/input.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        int id;             
        JSValue callback;   
        double fire_time;   
        double interval_ms; 
        bool active;        
    } JSTimerEntry;

#define JS_RUNTIME_PENDING_REJECTION_CAPACITY 32

    typedef struct
    {
        JSValue promise;
        JSValue reason;
    } JSPendingRejection;

    typedef struct
    {
        JSRuntime *runtime;
        JSContext *context;

        JSPendingRejection pending_rejections[JS_RUNTIME_PENDING_REJECTION_CAPACITY];
        int pending_rejection_count;

        bool exit_requested;
        int exit_code;

        char project_dir[1024];

        JSTimerEntry *timers;
        int timer_count;
        int timer_capacity;
        int next_timer_id;
        double current_timestamp_ms;
    } JSRuntimeContext;

    JSRuntimeContext *js_runtime_create(const char *project_dir);

    void js_runtime_destroy(JSRuntimeContext *ctx);

    bool js_runtime_load_file(JSRuntimeContext *ctx, const char *filename);

    bool js_runtime_eval(JSRuntimeContext *ctx, const char *code, const char *filename);

    void js_runtime_execute_pending_jobs(JSRuntimeContext *ctx);

    bool js_runtime_exit_requested(const JSRuntimeContext *ctx, int *code);

    bool js_runtime_has_pending_work(JSRuntimeContext *ctx);

    void js_runtime_process_timers(JSRuntimeContext *ctx, double timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif