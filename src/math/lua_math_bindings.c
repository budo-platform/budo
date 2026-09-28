#include "lua_math_bindings.h"
#include "math/math_wrapper.h"

#include "lua.h"
#include "lauxlib.h"

#include <stdbool.h>

static void lua_math_read(lua_State *L, int idx, float *dst, int n)
{
    int i;
    luaL_checktype(L, idx, LUA_TTABLE);
    for (i = 0; i < n; ++i)
    {
        lua_geti(L, idx, i + 1);
        dst[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}

static void lua_math_write(lua_State *L, int idx, const float *src, int n)
{
    int i;
    luaL_checktype(L, idx, LUA_TTABLE);
    for (i = 0; i < n; ++i)
    {
        lua_pushnumber(L, src[i]);
        lua_seti(L, idx, i + 1);
    }
}

static void lua_math_require_buffer(lua_State *L, int index, size_t count,
                                    const char *code)
{
    if (lua_istable(L, index) && lua_rawlen(L, index) >= count)
        return;
    luaL_error(L, "%s: expected a table with at least %d values",
               code, (int)count);
}

static int l_mat4_identity(lua_State *L)
{
    ApiError error;
    float o[16];
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!math_service_mat4_identity(o, 16, &error))
        return luaL_error(L, "%s: %s", error.code, error.message);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static int l_mat4_multiply(lua_State *L)
{
    float o[16], a[16], b[16];
    lua_math_read(L, 2, a, 16);
    lua_math_read(L, 3, b, 16);
    math_mat4_multiply(o, a, b);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static int l_mat4_perspective(lua_State *L)
{
    float o[16];
    float fovy = (float)luaL_checknumber(L, 2);
    float aspect = (float)luaL_checknumber(L, 3);
    float znear = (float)luaL_checknumber(L, 4);
    float zfar = (float)luaL_checknumber(L, 5);
    math_mat4_perspective(o, fovy, aspect, znear, zfar);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static int l_mat4_ortho(lua_State *L)
{
    float o[16];
    float left = (float)luaL_checknumber(L, 2);
    float right = (float)luaL_checknumber(L, 3);
    float bottom = (float)luaL_checknumber(L, 4);
    float top = (float)luaL_checknumber(L, 5);
    float znear = (float)luaL_checknumber(L, 6);
    float zfar = (float)luaL_checknumber(L, 7);
    math_mat4_ortho(o, left, right, bottom, top, znear, zfar);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static int l_mat4_lookat(lua_State *L)
{
    float o[16], eye[3], target[3], up[3];
    lua_math_read(L, 2, eye, 3);
    lua_math_read(L, 3, target, 3);
    lua_math_read(L, 4, up, 3);
    math_mat4_lookat(o, eye, target, up);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static int l_mat4_translate(lua_State *L)
{
    float o[16], a[16], v[3];
    lua_math_read(L, 2, a, 16);
    lua_math_read(L, 3, v, 3);
    math_mat4_translate(o, a, v);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static int l_mat4_scale(lua_State *L)
{
    float o[16], a[16], v[3];
    lua_math_read(L, 2, a, 16);
    lua_math_read(L, 3, v, 3);
    math_mat4_scale(o, a, v);
    lua_math_write(L, 1, o, 16);
    return 0;
}

#define DEFINE_LUA_ROTATE(name, fn)                \
    static int l_##name(lua_State *L)              \
    {                                              \
        float o[16], a[16];                        \
        float ang = (float)luaL_checknumber(L, 3); \
        lua_math_read(L, 2, a, 16);                \
        fn(o, a, ang);                             \
        lua_math_write(L, 1, o, 16);               \
        return 0;                                  \
    }

DEFINE_LUA_ROTATE(mat4_rotate_x, math_mat4_rotate_x)
DEFINE_LUA_ROTATE(mat4_rotate_y, math_mat4_rotate_y)
DEFINE_LUA_ROTATE(mat4_rotate_z, math_mat4_rotate_z)

static int l_mat4_invert(lua_State *L)
{
    ApiError error;
    float o[16], a[16];
    bool ok;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_math_require_buffer(L, 2, 16, "math.invalid_input_buffer");
    lua_math_read(L, 2, a, 16);
    ok = math_service_mat4_invert(o, 16, a, 16, &error);
    lua_math_write(L, 1, o, 16);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int l_mat4_transpose(lua_State *L)
{
    float o[16], a[16];
    lua_math_read(L, 2, a, 16);
    math_mat4_transpose(o, a);
    lua_math_write(L, 1, o, 16);
    return 0;
}

#define DEFINE_LUA_VEC3_BIN(name, fn) \
    static int l_##name(lua_State *L) \
    {                                 \
        float o[3], a[3], b[3];       \
        lua_math_read(L, 2, a, 3);    \
        lua_math_read(L, 3, b, 3);    \
        fn(o, a, b);                  \
        lua_math_write(L, 1, o, 3);   \
        return 0;                     \
    }

DEFINE_LUA_VEC3_BIN(vec3_add, math_vec3_add)
DEFINE_LUA_VEC3_BIN(vec3_sub, math_vec3_sub)
DEFINE_LUA_VEC3_BIN(vec3_cross, math_vec3_cross)

static int l_vec3_scale(lua_State *L)
{
    ApiError error;
    float o[3], a[3];
    float s = (float)luaL_checknumber(L, 3);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_math_require_buffer(L, 2, 3, "math.invalid_input_buffer");
    lua_math_read(L, 2, a, 3);
    if (!math_service_vec3_scale(o, 3, a, 3, s, &error))
        return luaL_error(L, "%s: %s", error.code, error.message);
    lua_math_write(L, 1, o, 3);
    return 0;
}

static int l_vec3_normalize(lua_State *L)
{
    float o[3], a[3];
    lua_math_read(L, 2, a, 3);
    math_vec3_normalize(o, a);
    lua_math_write(L, 1, o, 3);
    return 0;
}

static int l_vec3_dot(lua_State *L)
{
    float a[3], b[3];
    lua_math_read(L, 1, a, 3);
    lua_math_read(L, 2, b, 3);
    lua_pushnumber(L, math_vec3_dot(a, b));
    return 1;
}

static int l_vec3_length(lua_State *L)
{
    float a[3];
    lua_math_read(L, 1, a, 3);
    lua_pushnumber(L, math_vec3_length(a));
    return 1;
}

static int l_vec3_transform_mat4(lua_State *L)
{
    float o[3], v[3], m[16];
    lua_math_read(L, 2, v, 3);
    lua_math_read(L, 3, m, 16);
    math_vec3_transform_mat4(o, v, m);
    lua_math_write(L, 1, o, 3);
    return 0;
}

static int l_vec3_normalize_many(lua_State *L)
{
    int count = (int)luaL_checkinteger(L, 3);
    int i;
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_argcheck(L, count >= 0, 3, "count must be non-negative");
    for (i = 0; i < count; ++i)
    {
        float o[3], v[3];
        int base = i * 3;
        lua_geti(L, 2, base + 1);
        v[0] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_geti(L, 2, base + 2);
        v[1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_geti(L, 2, base + 3);
        v[2] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        math_vec3_normalize(o, v);
        lua_pushnumber(L, o[0]);
        lua_seti(L, 1, base + 1);
        lua_pushnumber(L, o[1]);
        lua_seti(L, 1, base + 2);
        lua_pushnumber(L, o[2]);
        lua_seti(L, 1, base + 3);
    }
    return 0;
}

static int l_vec3_transform_mat4_many(lua_State *L)
{
    float m[16];
    int count = (int)luaL_checkinteger(L, 4);
    int i;
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_math_read(L, 3, m, 16);
    luaL_argcheck(L, count >= 0, 4, "count must be non-negative");
    for (i = 0; i < count; ++i)
    {
        float o[3], v[3];
        int base = i * 3;
        lua_geti(L, 2, base + 1);
        v[0] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_geti(L, 2, base + 2);
        v[1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_geti(L, 2, base + 3);
        v[2] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        math_vec3_transform_mat4(o, v, m);
        lua_pushnumber(L, o[0]);
        lua_seti(L, 1, base + 1);
        lua_pushnumber(L, o[1]);
        lua_seti(L, 1, base + 2);
        lua_pushnumber(L, o[2]);
        lua_seti(L, 1, base + 3);
    }
    return 0;
}

static int l_quat_from_axis_angle(lua_State *L)
{
    float o[4], axis[3];
    float ang = (float)luaL_checknumber(L, 3);
    lua_math_read(L, 2, axis, 3);
    math_quat_from_axis_angle(o, axis, ang);
    lua_math_write(L, 1, o, 4);
    return 0;
}

static int l_quat_multiply(lua_State *L)
{
    float o[4], a[4], b[4];
    lua_math_read(L, 2, a, 4);
    lua_math_read(L, 3, b, 4);
    math_quat_multiply(o, a, b);
    lua_math_write(L, 1, o, 4);
    return 0;
}

static int l_quat_slerp(lua_State *L)
{
    float o[4], a[4], b[4];
    float t = (float)luaL_checknumber(L, 4);
    lua_math_read(L, 2, a, 4);
    lua_math_read(L, 3, b, 4);
    math_quat_slerp(o, a, b, t);
    lua_math_write(L, 1, o, 4);
    return 0;
}

static int l_quat_to_mat4(lua_State *L)
{
    float o[16], q[4];
    lua_math_read(L, 2, q, 4);
    math_quat_to_mat4(o, q);
    lua_math_write(L, 1, o, 16);
    return 0;
}

static const luaL_Reg math_funcs[] = {
    {"mat4Identity", l_mat4_identity},
    {"mat4Multiply", l_mat4_multiply},
    {"mat4Perspective", l_mat4_perspective},
    {"mat4Ortho", l_mat4_ortho},
    {"mat4LookAt", l_mat4_lookat},
    {"mat4Translate", l_mat4_translate},
    {"mat4RotateX", l_mat4_rotate_x},
    {"mat4RotateY", l_mat4_rotate_y},
    {"mat4RotateZ", l_mat4_rotate_z},
    {"mat4Scale", l_mat4_scale},
    {"mat4Invert", l_mat4_invert},
    {"mat4Transpose", l_mat4_transpose},

    {"vec3Add", l_vec3_add},
    {"vec3Sub", l_vec3_sub},
    {"vec3Scale", l_vec3_scale},
    {"vec3Normalize", l_vec3_normalize},
    {"vec3Cross", l_vec3_cross},
    {"vec3Dot", l_vec3_dot},
    {"vec3Length", l_vec3_length},
    {"vec3TransformMat4", l_vec3_transform_mat4},
    {"vec3NormalizeMany", l_vec3_normalize_many},
    {"vec3TransformMat4Many", l_vec3_transform_mat4_many},

    {"quatFromAxisAngle", l_quat_from_axis_angle},
    {"quatMultiply", l_quat_multiply},
    {"quatSlerp", l_quat_slerp},
    {"quatToMat4", l_quat_to_mat4},

    {NULL, NULL},
};

void lua_math_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    lua_getglobal(L, "sys");
    lua_newtable(L);
    luaL_setfuncs(L, math_funcs, 0);
    lua_setfield(L, -2, "math");
    lua_pop(L, 1); 
}