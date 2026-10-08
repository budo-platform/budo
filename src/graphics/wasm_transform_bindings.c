#include "wasm_transform_bindings.h"

#include "wasm_canvas_bindings.h"

#include <string.h>
#include <wasm.h>

static SkiaCanvas *transform_canvas(void *environment)
{
    return wasm_canvas_current_canvas((WasmCanvasContext *)environment);
}

static wasm_trap_t *transform_save(void *environment, wasmtime_caller_t *caller,
                                   const wasmtime_val_t *arguments,
                                   size_t argument_count,
                                   wasmtime_val_t *results,
                                   size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)arguments;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_save(canvas);
    return NULL;
}

static wasm_trap_t *transform_restore(void *environment, wasmtime_caller_t *caller,
                                      const wasmtime_val_t *arguments,
                                      size_t argument_count,
                                      wasmtime_val_t *results,
                                      size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)arguments;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_restore(canvas);
    return NULL;
}

static wasm_trap_t *transform_translate(void *environment, wasmtime_caller_t *caller,
                                        const wasmtime_val_t *arguments,
                                        size_t argument_count,
                                        wasmtime_val_t *results,
                                        size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_translate(canvas, arguments[0].of.f32, arguments[1].of.f32);
    return NULL;
}

static wasm_trap_t *transform_rotate(void *environment, wasmtime_caller_t *caller,
                                     const wasmtime_val_t *arguments,
                                     size_t argument_count,
                                     wasmtime_val_t *results,
                                     size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_rotate(canvas, arguments[0].of.f32);
    return NULL;
}

static wasm_trap_t *transform_rotate_around(void *environment,
                                            wasmtime_caller_t *caller,
                                            const wasmtime_val_t *arguments,
                                            size_t argument_count,
                                            wasmtime_val_t *results,
                                            size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_rotate_around(canvas, arguments[0].of.f32,
                                  arguments[1].of.f32, arguments[2].of.f32);
    return NULL;
}

static wasm_trap_t *transform_scale(void *environment, wasmtime_caller_t *caller,
                                    const wasmtime_val_t *arguments,
                                    size_t argument_count,
                                    wasmtime_val_t *results,
                                    size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_scale(canvas, arguments[0].of.f32, arguments[1].of.f32);
    return NULL;
}

static wasm_trap_t *transform_skew(void *environment, wasmtime_caller_t *caller,
                                   const wasmtime_val_t *arguments,
                                   size_t argument_count,
                                   wasmtime_val_t *results,
                                   size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_skew(canvas, arguments[0].of.f32, arguments[1].of.f32);
    return NULL;
}

static wasm_trap_t *transform_reset(void *environment, wasmtime_caller_t *caller,
                                    const wasmtime_val_t *arguments,
                                    size_t argument_count,
                                    wasmtime_val_t *results,
                                    size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)arguments;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_reset_transform(canvas);
    return NULL;
}

static wasm_trap_t *transform_clip_rect(void *environment,
                                        wasmtime_caller_t *caller,
                                        const wasmtime_val_t *arguments,
                                        size_t argument_count,
                                        wasmtime_val_t *results,
                                        size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    float x = arguments[0].of.f32;
    float y = arguments[1].of.f32;
    float width = arguments[2].of.f32;
    float height = arguments[3].of.f32;
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_clip_rect(canvas, x, y, x + width, y + height);
    return NULL;
}

static wasm_trap_t *transform_clip_round_rect(void *environment,
                                              wasmtime_caller_t *caller,
                                              const wasmtime_val_t *arguments,
                                              size_t argument_count,
                                              wasmtime_val_t *results,
                                              size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    float x = arguments[0].of.f32;
    float y = arguments[1].of.f32;
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_clip_round_rect(canvas, x, y, x + arguments[2].of.f32, y + arguments[3].of.f32,
                                    arguments[4].of.f32, arguments[5].of.f32);
    return NULL;
}

static uint8_t layer_alpha(int32_t alpha)
{
    return (uint8_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
}

static wasm_trap_t *transform_save_layer(void *environment,
                                         wasmtime_caller_t *caller,
                                         const wasmtime_val_t *arguments,
                                         size_t argument_count,
                                         wasmtime_val_t *results,
                                         size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_save_layer(canvas, NULL, layer_alpha(arguments[0].of.i32), 0.0f);
    return NULL;
}

static wasm_trap_t *transform_save_layer_bounds(void *environment,
                                                wasmtime_caller_t *caller,
                                                const wasmtime_val_t *arguments,
                                                size_t argument_count,
                                                wasmtime_val_t *results,
                                                size_t result_count)
{
    SkiaCanvas *canvas = transform_canvas(environment);
    float x = arguments[0].of.f32;
    float y = arguments[1].of.f32;
    SkiaRect bounds = {x, y, x + arguments[2].of.f32, y + arguments[3].of.f32};
    (void)caller;
    (void)argument_count;
    (void)results;
    (void)result_count;
    if (canvas)
        skia_canvas_save_layer(canvas, &bounds, layer_alpha(arguments[4].of.i32), arguments[5].of.f32);
    return NULL;
}

static wasmtime_error_t *define_transform_function(
    wasmtime_linker_t *linker, WasmCanvasContext *context, const char *name,
    wasmtime_func_callback_t callback,
    const wasm_valkind_t *parameter_types, size_t parameter_count)
{
    wasm_valtype_vec_t parameters;
    wasm_valtype_vec_t results;
    wasm_functype_t *function_type;
    wasmtime_error_t *error;
    size_t index;

    wasm_valtype_vec_new_uninitialized(&parameters, parameter_count);
    for (index = 0; index < parameter_count; index++)
        parameters.data[index] = wasm_valtype_new(parameter_types[index]);
    wasm_valtype_vec_new_empty(&results);
    function_type = wasm_functype_new(&parameters, &results);
    error = wasmtime_linker_define_func(linker, "env", 3, name, strlen(name),
                                        function_type, callback, context, NULL);
    wasm_functype_delete(function_type);
    return error;
}

wasmtime_error_t *wasm_transform_register(wasmtime_linker_t *linker,
                                          WasmCanvasContext *context)
{
    const wasm_valkind_t f32[] = {WASM_F32};
    const wasm_valkind_t f32x2[] = {WASM_F32, WASM_F32};
    const wasm_valkind_t f32x3[] = {WASM_F32, WASM_F32, WASM_F32};
    const wasm_valkind_t f32x4[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32};
    const wasm_valkind_t f32x6[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_F32};
    const wasm_valkind_t i32[] = {WASM_I32};
    const wasm_valkind_t layer_bounds[] = {WASM_F32, WASM_F32, WASM_F32, WASM_F32, WASM_I32, WASM_F32};
    wasmtime_error_t *error;

    error = define_transform_function(
        linker, context, "transform_save", transform_save, NULL, 0);
    if (error)
        return error;
    error = define_transform_function(
        linker, context, "transform_restore", transform_restore, NULL, 0);
    if (error)
        return error;
    error = define_transform_function(linker, context, "transform_translate",
                                      transform_translate, f32x2, 2);
    if (error)
        return error;
    error = define_transform_function(linker, context, "transform_rotate",
                                      transform_rotate, f32, 1);
    if (error)
        return error;
    error = define_transform_function(
        linker, context, "transform_rotate_around", transform_rotate_around,
        f32x3, 3);
    if (error)
        return error;
    error = define_transform_function(linker, context, "transform_scale",
                                      transform_scale, f32x2, 2);
    if (error)
        return error;
    error = define_transform_function(linker, context, "canvas_skew",
                                      transform_skew, f32x2, 2);
    if (error)
        return error;
    error = define_transform_function(
        linker, context, "canvas_reset_transform", transform_reset, NULL, 0);
    if (error)
        return error;
    error = define_transform_function(linker, context, "canvas_clip_rect",
                                      transform_clip_rect, f32x4, 4);
    if (error)
        return error;
    error = define_transform_function(linker, context, "canvas_clip_round_rect",
                                      transform_clip_round_rect, f32x6, 6);
    if (error)
        return error;
    error = define_transform_function(linker, context, "canvas_save_layer",
                                      transform_save_layer, i32, 1);
    if (error)
        return error;
    error = define_transform_function(linker, context, "canvas_save_layer_bounds",
                                      transform_save_layer_bounds, layer_bounds, 6);
    if (error)
        return error;
    return NULL;
}