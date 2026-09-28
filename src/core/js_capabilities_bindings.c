#include "js_capabilities_bindings.h"
#include "capabilities.h"

static JSValue make_cap_entry(JSContext *ctx, bool available)
{
    JSValue entry = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, entry, "available", JS_NewBool(ctx, available));
    return entry;
}

void js_capabilities_init(JSContext *ctx)
{
    ApiError error;
    CapabilitySnapshot snapshot;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");
    JSValue caps = JS_NewObject(ctx);

    if (!capabilities_get_snapshot(&snapshot, &error))
    {
        JS_FreeValue(ctx, caps);
        JS_FreeValue(ctx, sys_obj);
        JS_FreeValue(ctx, global);
        return;
    }

    JS_SetPropertyStr(ctx, caps, "neural",
                      make_cap_entry(ctx, snapshot.neural));
    JS_SetPropertyStr(ctx, caps, "llamacpp",
                      make_cap_entry(ctx, snapshot.llamacpp));
    JS_SetPropertyStr(ctx, caps, "midi",
                      make_cap_entry(ctx, snapshot.midi));
    JS_SetPropertyStr(ctx, caps, "udp",
                      make_cap_entry(ctx, snapshot.udp));
    JS_SetPropertyStr(ctx, caps, "http",
                      make_cap_entry(ctx, snapshot.http));
    JS_SetPropertyStr(ctx, caps, "sensors",
                      make_cap_entry(ctx, snapshot.sensors));

    JS_SetPropertyStr(ctx, sys_obj, "capabilities", caps);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);
}