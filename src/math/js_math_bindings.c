#include "js_math_bindings.h"
#include "math/math_wrapper.h"

#include <limits.h>
#include <stdint.h>

static bool js_math_get_floats(JSContext *ctx, JSValue ta, int min_count,
                               float **out_ptr, JSValue *out_ab)
{
    size_t byte_offset = 0, byte_length = 0, bpe = 0, ab_size = 0;
    ApiError error;
    uint8_t *raw;
    *out_ab = JS_UNDEFINED;
    *out_ptr = NULL;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, ta, &byte_offset, &byte_length, &bpe);
    if (JS_IsException(ab))
        return false;
    if (bpe != 4)
    {
        JS_FreeValue(ctx, ab);
        return false;
    }
    raw = JS_GetArrayBuffer(ctx, &ab_size, ab);
    if (!raw || byte_offset + byte_length > ab_size)
    {
        JS_FreeValue(ctx, ab);
        return false;
    }
    if (!math_service_validate_buffer((float *)(raw + byte_offset),
                                      byte_length / sizeof(float),
                                      (size_t)min_count, false, &error))
    {
        JS_FreeValue(ctx, ab);
        JS_ThrowTypeError(ctx, "%s: %s", error.code, error.message);
        return false;
    }
    *out_ab = ab;
    *out_ptr = (float *)(raw + byte_offset);
    return true;
}

static JSValue js_math_throw_api_error(JSContext *ctx, const ApiError *error)
{
    return JS_ThrowTypeError(ctx, "%s: %s", error->code, error->message);
}

#define BIND_M4_UNARY(name, call)                                        \
    static JSValue js_##name(JSContext *ctx, JSValue this_val, int argc, \
                             JSValue *argv)                              \
    {                                                                    \
        (void)this_val;                                                  \
        if (argc < 1)                                                    \
            return JS_EXCEPTION;                                         \
        float *o;                                                        \
        JSValue ab_o;                                                    \
        if (!js_math_get_floats(ctx, argv[0], 16, &o, &ab_o))            \
            return JS_EXCEPTION;                                         \
        call;                                                            \
        JS_FreeValue(ctx, ab_o);                                         \
        return JS_UNDEFINED;                                             \
    }

static JSValue js_mat4_identity(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    ApiError error;
    float *o;
    JSValue ab;
    if (argc < 1 || !js_math_get_floats(ctx, argv[0], 16, &o, &ab))
    {
        api_error_set(&error, API_STATUS_INVALID_ARGUMENT,
                      "math.invalid_output_buffer",
                      "Output must be a Float32Array with at least 16 values");
        return js_math_throw_api_error(ctx, &error);
    }
    if (!math_service_mat4_identity(o, 16, &error))
    {
        JS_FreeValue(ctx, ab);
        return js_math_throw_api_error(ctx, &error);
    }
    JS_FreeValue(ctx, ab);
    return JS_UNDEFINED;
}

static JSValue js_mat4_multiply(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a, *b;
    JSValue ab_o, ab_a, ab_b;
    if (argc < 3 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 16, &a, &ab_a) ||
        !js_math_get_floats(ctx, argv[2], 16, &b, &ab_b))
    {
        if (argc >= 1)
            JS_FreeValue(ctx, ab_o);
        if (argc >= 2)
            JS_FreeValue(ctx, ab_a);
        return JS_EXCEPTION;
    }
    math_mat4_multiply(o, a, b);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    JS_FreeValue(ctx, ab_b);
    return JS_UNDEFINED;
}

static JSValue js_mat4_perspective(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o;
    JSValue ab;
    double fovy, aspect, znear, zfar;
    if (argc < 5 || !js_math_get_floats(ctx, argv[0], 16, &o, &ab))
        return JS_EXCEPTION;
    JS_ToFloat64(ctx, &fovy, argv[1]);
    JS_ToFloat64(ctx, &aspect, argv[2]);
    JS_ToFloat64(ctx, &znear, argv[3]);
    JS_ToFloat64(ctx, &zfar, argv[4]);
    math_mat4_perspective(o, (float)fovy, (float)aspect, (float)znear, (float)zfar);
    JS_FreeValue(ctx, ab);
    return JS_UNDEFINED;
}

static JSValue js_mat4_ortho(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o;
    JSValue ab;
    double l, r, b, t, n, f;
    if (argc < 7 || !js_math_get_floats(ctx, argv[0], 16, &o, &ab))
        return JS_EXCEPTION;
    JS_ToFloat64(ctx, &l, argv[1]);
    JS_ToFloat64(ctx, &r, argv[2]);
    JS_ToFloat64(ctx, &b, argv[3]);
    JS_ToFloat64(ctx, &t, argv[4]);
    JS_ToFloat64(ctx, &n, argv[5]);
    JS_ToFloat64(ctx, &f, argv[6]);
    math_mat4_ortho(o, (float)l, (float)r, (float)b, (float)t, (float)n, (float)f);
    JS_FreeValue(ctx, ab);
    return JS_UNDEFINED;
}

static JSValue js_mat4_lookat(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *eye, *target, *up;
    JSValue ab_o, ab_e, ab_t, ab_u;
    if (argc < 4 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 3, &eye, &ab_e) ||
        !js_math_get_floats(ctx, argv[2], 3, &target, &ab_t) ||
        !js_math_get_floats(ctx, argv[3], 3, &up, &ab_u))
        return JS_EXCEPTION;
    math_mat4_lookat(o, eye, target, up);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_e);
    JS_FreeValue(ctx, ab_t);
    JS_FreeValue(ctx, ab_u);
    return JS_UNDEFINED;
}

static JSValue js_mat4_translate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a, *v;
    JSValue ab_o, ab_a, ab_v;
    if (argc < 3 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 16, &a, &ab_a) ||
        !js_math_get_floats(ctx, argv[2], 3, &v, &ab_v))
        return JS_EXCEPTION;
    math_mat4_translate(o, a, v);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    JS_FreeValue(ctx, ab_v);
    return JS_UNDEFINED;
}

static JSValue js_mat4_scale(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a, *v;
    JSValue ab_o, ab_a, ab_v;
    if (argc < 3 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 16, &a, &ab_a) ||
        !js_math_get_floats(ctx, argv[2], 3, &v, &ab_v))
        return JS_EXCEPTION;
    math_mat4_scale(o, a, v);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    JS_FreeValue(ctx, ab_v);
    return JS_UNDEFINED;
}

#define DEFINE_MAT4_ROTATE(name, fn)                                     \
    static JSValue js_##name(JSContext *ctx, JSValue this_val, int argc, \
                             JSValue *argv)                              \
    {                                                                    \
        (void)this_val;                                                  \
        float *o, *a;                                                    \
        JSValue ab_o, ab_a;                                              \
        double ang;                                                      \
        if (argc < 3 ||                                                  \
            !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||          \
            !js_math_get_floats(ctx, argv[1], 16, &a, &ab_a))            \
            return JS_EXCEPTION;                                         \
        JS_ToFloat64(ctx, &ang, argv[2]);                                \
        fn(o, a, (float)ang);                                            \
        JS_FreeValue(ctx, ab_o);                                         \
        JS_FreeValue(ctx, ab_a);                                         \
        return JS_UNDEFINED;                                             \
    }

DEFINE_MAT4_ROTATE(mat4_rotate_x, math_mat4_rotate_x)
DEFINE_MAT4_ROTATE(mat4_rotate_y, math_mat4_rotate_y)
DEFINE_MAT4_ROTATE(mat4_rotate_z, math_mat4_rotate_z)

static JSValue js_mat4_invert(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    ApiError error;
    float *o, *a;
    JSValue ab_o, ab_a;
    bool ok;
    if (argc < 2 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 16, &a, &ab_a))
    {
        api_error_set(&error, API_STATUS_INVALID_ARGUMENT,
                      "math.invalid_buffer",
                      "Output and input must be Float32Arrays with at least 16 values");
        return js_math_throw_api_error(ctx, &error);
    }
    ok = math_service_mat4_invert(o, 16, a, 16, &error);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    return JS_NewBool(ctx, ok);
}

static JSValue js_mat4_transpose(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a;
    JSValue ab_o, ab_a;
    if (argc < 2 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 16, &a, &ab_a))
        return JS_EXCEPTION;
    math_mat4_transpose(o, a);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    return JS_UNDEFINED;
}

#define DEFINE_VEC3_BIN(name, fn)                                        \
    static JSValue js_##name(JSContext *ctx, JSValue this_val, int argc, \
                             JSValue *argv)                              \
    {                                                                    \
        (void)this_val;                                                  \
        float *o, *a, *b;                                                \
        JSValue ab_o, ab_a, ab_b;                                        \
        if (argc < 3 ||                                                  \
            !js_math_get_floats(ctx, argv[0], 3, &o, &ab_o) ||           \
            !js_math_get_floats(ctx, argv[1], 3, &a, &ab_a) ||           \
            !js_math_get_floats(ctx, argv[2], 3, &b, &ab_b))             \
            return JS_EXCEPTION;                                         \
        fn(o, a, b);                                                     \
        JS_FreeValue(ctx, ab_o);                                         \
        JS_FreeValue(ctx, ab_a);                                         \
        JS_FreeValue(ctx, ab_b);                                         \
        return JS_UNDEFINED;                                             \
    }

DEFINE_VEC3_BIN(vec3_add, math_vec3_add)
DEFINE_VEC3_BIN(vec3_sub, math_vec3_sub)
DEFINE_VEC3_BIN(vec3_cross, math_vec3_cross)

static JSValue js_vec3_scale(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    ApiError error;
    float *o, *a;
    JSValue ab_o, ab_a;
    double s;
    if (argc < 3 || !js_math_get_floats(ctx, argv[0], 3, &o, &ab_o))
    {
        api_error_set(&error, API_STATUS_INVALID_ARGUMENT,
                      "math.invalid_output_buffer",
                      "Output must be a Float32Array with at least 3 values");
        return js_math_throw_api_error(ctx, &error);
    }
    if (!js_math_get_floats(ctx, argv[1], 3, &a, &ab_a))
    {
        JS_FreeValue(ctx, ab_o);
        api_error_set(&error, API_STATUS_INVALID_ARGUMENT,
                      "math.invalid_input_buffer",
                      "Input must be a Float32Array with at least 3 values");
        return js_math_throw_api_error(ctx, &error);
    }
    JS_ToFloat64(ctx, &s, argv[2]);
    if (!math_service_vec3_scale(o, 3, a, 3, (float)s, &error))
    {
        JS_FreeValue(ctx, ab_o);
        JS_FreeValue(ctx, ab_a);
        return js_math_throw_api_error(ctx, &error);
    }
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    return JS_UNDEFINED;
}

static JSValue js_vec3_normalize(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a;
    JSValue ab_o, ab_a;
    if (argc < 2 ||
        !js_math_get_floats(ctx, argv[0], 3, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 3, &a, &ab_a))
        return JS_EXCEPTION;
    math_vec3_normalize(o, a);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    return JS_UNDEFINED;
}

static JSValue js_vec3_dot(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *a, *b;
    JSValue ab_a, ab_b;
    float r;
    if (argc < 2 ||
        !js_math_get_floats(ctx, argv[0], 3, &a, &ab_a) ||
        !js_math_get_floats(ctx, argv[1], 3, &b, &ab_b))
        return JS_EXCEPTION;
    r = math_vec3_dot(a, b);
    JS_FreeValue(ctx, ab_a);
    JS_FreeValue(ctx, ab_b);
    return JS_NewFloat64(ctx, r);
}

static JSValue js_vec3_length(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *a;
    JSValue ab_a;
    float r;
    if (argc < 1 || !js_math_get_floats(ctx, argv[0], 3, &a, &ab_a))
        return JS_EXCEPTION;
    r = math_vec3_length(a);
    JS_FreeValue(ctx, ab_a);
    return JS_NewFloat64(ctx, r);
}

static JSValue js_vec3_transform_mat4(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *v, *m;
    JSValue ab_o, ab_v, ab_m;
    if (argc < 3 ||
        !js_math_get_floats(ctx, argv[0], 3, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 3, &v, &ab_v) ||
        !js_math_get_floats(ctx, argv[2], 16, &m, &ab_m))
        return JS_EXCEPTION;
    math_vec3_transform_mat4(o, v, m);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_v);
    JS_FreeValue(ctx, ab_m);
    return JS_UNDEFINED;
}

static JSValue js_vec3_normalize_many(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o = NULL, *v = NULL;
    JSValue ab_o = JS_UNDEFINED, ab_v = JS_UNDEFINED;
    int32_t count;
    if (argc < 3 || JS_ToInt32(ctx, &count, argv[2]) < 0 || count < 0 || count > INT_MAX / 3)
        return JS_EXCEPTION;
    if (!js_math_get_floats(ctx, argv[0], count * 3, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], count * 3, &v, &ab_v))
    {
        JS_FreeValue(ctx, ab_o);
        JS_FreeValue(ctx, ab_v);
        return JS_EXCEPTION;
    }
    math_vec3_normalize_many(o, v, count);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_v);
    return JS_UNDEFINED;
}

static JSValue js_vec3_transform_mat4_many(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o = NULL, *v = NULL, *m = NULL;
    JSValue ab_o = JS_UNDEFINED, ab_v = JS_UNDEFINED, ab_m = JS_UNDEFINED;
    int32_t count;
    if (argc < 4 || JS_ToInt32(ctx, &count, argv[3]) < 0 || count < 0 || count > INT_MAX / 3)
        return JS_EXCEPTION;
    if (!js_math_get_floats(ctx, argv[0], count * 3, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], count * 3, &v, &ab_v) ||
        !js_math_get_floats(ctx, argv[2], 16, &m, &ab_m))
    {
        JS_FreeValue(ctx, ab_o);
        JS_FreeValue(ctx, ab_v);
        JS_FreeValue(ctx, ab_m);
        return JS_EXCEPTION;
    }
    math_vec3_transform_mat4_many(o, v, m, count);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_v);
    JS_FreeValue(ctx, ab_m);
    return JS_UNDEFINED;
}

static JSValue js_quat_from_axis_angle(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *axis;
    JSValue ab_o, ab_a;
    double ang;
    if (argc < 3 ||
        !js_math_get_floats(ctx, argv[0], 4, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 3, &axis, &ab_a))
        return JS_EXCEPTION;
    JS_ToFloat64(ctx, &ang, argv[2]);
    math_quat_from_axis_angle(o, axis, (float)ang);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    return JS_UNDEFINED;
}

static JSValue js_quat_multiply(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a, *b;
    JSValue ab_o, ab_a, ab_b;
    if (argc < 3 ||
        !js_math_get_floats(ctx, argv[0], 4, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 4, &a, &ab_a) ||
        !js_math_get_floats(ctx, argv[2], 4, &b, &ab_b))
        return JS_EXCEPTION;
    math_quat_multiply(o, a, b);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    JS_FreeValue(ctx, ab_b);
    return JS_UNDEFINED;
}

static JSValue js_quat_slerp(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *a, *b;
    JSValue ab_o, ab_a, ab_b;
    double t;
    if (argc < 4 ||
        !js_math_get_floats(ctx, argv[0], 4, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 4, &a, &ab_a) ||
        !js_math_get_floats(ctx, argv[2], 4, &b, &ab_b))
        return JS_EXCEPTION;
    JS_ToFloat64(ctx, &t, argv[3]);
    math_quat_slerp(o, a, b, (float)t);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_a);
    JS_FreeValue(ctx, ab_b);
    return JS_UNDEFINED;
}

static JSValue js_quat_to_mat4(JSContext *ctx, JSValue this_val, int argc, JSValue *argv)
{
    (void)this_val;
    float *o, *q;
    JSValue ab_o, ab_q;
    if (argc < 2 ||
        !js_math_get_floats(ctx, argv[0], 16, &o, &ab_o) ||
        !js_math_get_floats(ctx, argv[1], 4, &q, &ab_q))
        return JS_EXCEPTION;
    math_quat_to_mat4(o, q);
    JS_FreeValue(ctx, ab_o);
    JS_FreeValue(ctx, ab_q);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_sys_math_funcs[] = {
    JS_CFUNC_DEF("mat4Identity", 1, js_mat4_identity),
    JS_CFUNC_DEF("mat4Multiply", 3, js_mat4_multiply),
    JS_CFUNC_DEF("mat4Perspective", 5, js_mat4_perspective),
    JS_CFUNC_DEF("mat4Ortho", 7, js_mat4_ortho),
    JS_CFUNC_DEF("mat4LookAt", 4, js_mat4_lookat),
    JS_CFUNC_DEF("mat4Translate", 3, js_mat4_translate),
    JS_CFUNC_DEF("mat4RotateX", 3, js_mat4_rotate_x),
    JS_CFUNC_DEF("mat4RotateY", 3, js_mat4_rotate_y),
    JS_CFUNC_DEF("mat4RotateZ", 3, js_mat4_rotate_z),
    JS_CFUNC_DEF("mat4Scale", 3, js_mat4_scale),
    JS_CFUNC_DEF("mat4Invert", 2, js_mat4_invert),
    JS_CFUNC_DEF("mat4Transpose", 2, js_mat4_transpose),

    JS_CFUNC_DEF("vec3Add", 3, js_vec3_add),
    JS_CFUNC_DEF("vec3Sub", 3, js_vec3_sub),
    JS_CFUNC_DEF("vec3Scale", 3, js_vec3_scale),
    JS_CFUNC_DEF("vec3Normalize", 2, js_vec3_normalize),
    JS_CFUNC_DEF("vec3Cross", 3, js_vec3_cross),
    JS_CFUNC_DEF("vec3Dot", 2, js_vec3_dot),
    JS_CFUNC_DEF("vec3Length", 1, js_vec3_length),
    JS_CFUNC_DEF("vec3TransformMat4", 3, js_vec3_transform_mat4),
    JS_CFUNC_DEF("vec3NormalizeMany", 3, js_vec3_normalize_many),
    JS_CFUNC_DEF("vec3TransformMat4Many", 4, js_vec3_transform_mat4_many),

    JS_CFUNC_DEF("quatFromAxisAngle", 3, js_quat_from_axis_angle),
    JS_CFUNC_DEF("quatMultiply", 3, js_quat_multiply),
    JS_CFUNC_DEF("quatSlerp", 4, js_quat_slerp),
    JS_CFUNC_DEF("quatToMat4", 2, js_quat_to_mat4),
};

void js_math_init(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");
    JSValue math_obj = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, math_obj, js_sys_math_funcs,
                               sizeof(js_sys_math_funcs) / sizeof(js_sys_math_funcs[0]));
    JS_SetPropertyStr(ctx, sys_obj, "math", math_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);
}