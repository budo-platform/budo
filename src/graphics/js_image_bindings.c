#include "graphics/js_image_bindings.h"
#include "graphics/js_tools.h"
#include "graphics/image_async.h"
#include "graphics/image_scheduler.h"
#include "core/window.h"
#include "file/file_wrapper.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct JsImageRequest
{
    uint32_t id;
    JSValue callbacks;
} JsImageRequest;

struct JsImageBindings
{
    ImageScheduler *scheduler;
    JsImageRequest requests[8];
};

static JsImageRequest *find_request(JsImageBindings *images, uint32_t id)
{
    for (int index = 0; index < 8; index++)
        if (images->requests[index].id == id)
            return &images->requests[index];
    return NULL;
}

static JSValue make_info(JSContext *ctx, const ImageAsyncInfo *info)
{
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "width", JS_NewInt32(ctx, info->width));
    JS_SetPropertyStr(ctx, result, "height", JS_NewInt32(ctx, info->height));
    JS_SetPropertyStr(ctx, result, "sourceWidth", JS_NewInt32(ctx, info->source_width));
    JS_SetPropertyStr(ctx, result, "sourceHeight", JS_NewInt32(ctx, info->source_height));
    return result;
}

static void call_event(JSContext *ctx, JSValue callbacks, const char *name, int argc, JSValue *argv)
{
    JSValue callback = JS_GetPropertyStr(ctx, callbacks, name);
    if (JS_IsFunction(ctx, callback))
    {
        JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, argc, argv);
        if (JS_IsException(result))
            js_dump_error(ctx);
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, callback);
}

static bool read_option(JSContext *ctx, JSValue object, const char *name, double limit, size_t *out)
{
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value))
        return false;
    if (!JS_IsUndefined(value))
    {
        double number;
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) < 0 || !isfinite(number) ||
            number < 1 || number > limit || floor(number) != number)
        {
            JS_FreeValue(ctx, value);
            JS_ThrowRangeError(ctx, "%s must be a positive integer no greater than %.0f", name, limit);
            return false;
        }
        *out = (size_t)number;
    }
    JS_FreeValue(ctx, value);
    return true;
}

JSValue js_image_load(JSContext *ctx, JSGraphicContext *graphics, int argc, JSValue *argv, bool from_buffer)
{
    if (argc < 3 || !JS_IsObject(argv[1]) || !JS_IsObject(argv[2]))
        return JS_ThrowTypeError(ctx, "Expected source, options object, and callbacks object");
    const char *names[] = {"onComplete", "onError", "onMetadata"};
    for (int index = 0; index < 3; index++)
    {
        JSValue callback = JS_GetPropertyStr(ctx, argv[2], names[index]);
        if (JS_IsException(callback))
            return JS_EXCEPTION;
        bool valid = JS_IsFunction(ctx, callback) || (index == 2 && JS_IsUndefined(callback));
        JS_FreeValue(ctx, callback);
        if (!valid)
            return JS_ThrowTypeError(ctx, "%s must be a function", names[index]);
    }
    ImageAsyncOptions options = {0};
    size_t width = 0, height = 0;
    if (!read_option(ctx, argv[1], "maxWidth", 16384, &width) ||
        !read_option(ctx, argv[1], "maxHeight", 16384, &height) ||
        !read_option(ctx, argv[1], "maxBytes", 64u * 1024u * 1024u, &options.max_bytes) ||
        !read_option(ctx, argv[1], "maxPixels", IMAGE_MAX_PIXELS, &options.max_pixels))
        return JS_EXCEPTION;
    options.max_width = (int)width;
    options.max_height = (int)height;
    if (!graphics->images)
    {
        JsImageBindings *images = calloc(1, sizeof(*images));
        if (!images)
            return JS_ThrowOutOfMemory(ctx);
        images->scheduler = image_scheduler_create(graphics->image_files ? *graphics->image_files : NULL);
        if (!images->scheduler)
        {
            free(images);
            return JS_ThrowInternalError(ctx, "Cannot start image workers");
        }
        graphics->images = images;
    }
    JsImageBindings *images = graphics->images;
    JsImageRequest *slot = find_request(images, 0);
    if (!slot)
        return JS_ThrowRangeError(ctx, "Maximum of 8 active texture loads exceeded");
    uint32_t request = 0;
    if (from_buffer)
    {
        size_t length = 0, offset = 0, element_size = 0, buffer_size = 0;
        JSValue owner = JS_UNDEFINED;
        uint8_t *bytes = NULL;
        if (JS_IsArrayBuffer(argv[0]))
            bytes = JS_GetArrayBuffer(ctx, &length, argv[0]);
        else
        {
            owner = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &length, &element_size);
            if (JS_IsException(owner))
                return JS_EXCEPTION;
            uint8_t *base = JS_GetArrayBuffer(ctx, &buffer_size, owner);
            if (base && offset <= buffer_size && length <= buffer_size - offset)
                bytes = base + offset;
        }
        if (bytes)
            request = image_scheduler_load_buffer(images->scheduler, bytes, length, &options);
        JS_FreeValue(ctx, owner);
        if (!bytes)
            return JS_ThrowTypeError(ctx, "Expected an ArrayBuffer or typed array");
    }
    else
    {
        if (!JS_IsString(argv[0]))
            return JS_ThrowTypeError(ctx, "Expected a project-relative image path");
        size_t length;
        const char *path = JS_ToCStringLen(ctx, &length, argv[0]);
        if (!path)
            return JS_EXCEPTION;
        if (length != strlen(path) || length > FILE_MAX_PATH - 8 || !file_path_is_valid(path, false))
        {
            JS_FreeCString(ctx, path);
            return JS_ThrowTypeError(ctx, "Image path must be relative and cannot escape its mount");
        }
        JSValue mount_value = JS_GetPropertyStr(ctx, argv[1], "mount");
        if (JS_IsException(mount_value))
        {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
        const char *mount = JS_IsUndefined(mount_value) ? "assets" : JS_ToCString(ctx, mount_value);
        if (!mount || (strcmp(mount, "assets") != 0 && strcmp(mount, "files") != 0))
        {
            if (mount && !JS_IsUndefined(mount_value))
                JS_FreeCString(ctx, mount);
            JS_FreeValue(ctx, mount_value);
            JS_FreeCString(ctx, path);
            return JS_ThrowTypeError(ctx, "mount must be assets or files");
        }
        char mounted_path[FILE_MAX_PATH];
        snprintf(mounted_path, sizeof(mounted_path), "%s/%s", mount, path);
        if (!JS_IsUndefined(mount_value))
            JS_FreeCString(ctx, mount);
        JS_FreeValue(ctx, mount_value);
        request = image_scheduler_load_file(images->scheduler, mounted_path, &options);
        JS_FreeCString(ctx, path);
    }
    if (!request)
        return JS_ThrowRangeError(ctx, "%s", image_scheduler_error(images->scheduler));
    slot->id = request;
    slot->callbacks = JS_DupValue(ctx, argv[2]);
    return JS_NewUint32(ctx, request);
}

static void finish_request(JSGraphicContext *graphics, uint32_t id, int texture, const char *code,
                           const char *message, const ImageAsyncInfo *info)
{
    JSContext *ctx = graphics->context;
    JsImageRequest *slot = find_request(graphics->images, id);
    if (!slot)
        return;
    JSValue callbacks = slot->callbacks;
    slot->id = 0;
    slot->callbacks = JS_UNDEFINED;
    if (code)
    {
        JSValue error = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, error, "code", JS_NewString(ctx, code));
        JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message));
        call_event(ctx, callbacks, "onError", 1, &error);
        JS_FreeValue(ctx, error);
    }
    else
    {
        JSValue args[] = {JS_NewInt32(ctx, texture), make_info(ctx, info)};
        call_event(ctx, callbacks, "onComplete", 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    }
    JS_FreeValue(ctx, callbacks);
}

JSValue js_image_cancel(JSContext *ctx, JSGraphicContext *graphics, int argc, JSValue *argv)
{
    double number;
    if (argc < 1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(ctx, &number, argv[0]) < 0 ||
        !isfinite(number) || number < 1 || number > UINT32_MAX || floor(number) != number)
        return JS_ThrowRangeError(ctx, "Expected a positive texture-load request id");
    if (!graphics->images)
        return JS_FALSE;
    JsImageBindings *images = graphics->images;
    uint32_t id = (uint32_t)number;
    JsImageRequest *slot = find_request(images, id);
    if (!slot)
        return JS_FALSE;
    image_scheduler_cancel(images->scheduler, graphics->window, id);
    JS_FreeValue(ctx, slot->callbacks);
    slot->callbacks = JS_UNDEFINED;
    slot->id = 0;
    return JS_TRUE;
}

void js_image_poll(JSGraphicContext *graphics)
{
    if (!graphics || !graphics->images)
        return;
    JsImageBindings *images = graphics->images;
    image_scheduler_begin_poll(images->scheduler);
    ImageAsyncEvent event;
    int texture;
    for (int count = 0; count < 8 && image_scheduler_poll(images->scheduler, graphics->window, &event, &texture); count++)
    {
        JsImageRequest *slot = find_request(images, event.request);
        if (!slot)
        {
            if (texture > 0)
                window_gl_destroy_texture(graphics->window, texture);
            continue;
        }
        if (event.type == IMAGE_ASYNC_METADATA)
        {
            JSValue callbacks = JS_DupValue(graphics->context, slot->callbacks);
            JSValue info = make_info(graphics->context, &event.info);
            call_event(graphics->context, callbacks, "onMetadata", 1, &info);
            JS_FreeValue(graphics->context, info);
            JS_FreeValue(graphics->context, callbacks);
        }
        else
            finish_request(graphics, event.request, texture, event.code, event.message, &event.info);
    }
}

bool js_image_pending(JSGraphicContext *graphics)
{
    return graphics && graphics->images && image_scheduler_pending(graphics->images->scheduler);
}

void js_image_cleanup(JSGraphicContext *graphics)
{
    if (!graphics || !graphics->images)
        return;
    JsImageBindings *images = graphics->images;
    image_scheduler_destroy(images->scheduler, graphics->window);
    for (int index = 0; index < 8; index++)
        if (images->requests[index].id)
            JS_FreeValue(graphics->context, images->requests[index].callbacks);
    free(images);
    graphics->images = NULL;
}