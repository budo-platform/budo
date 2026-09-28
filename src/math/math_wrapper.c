#include "math/math_wrapper.h"

#include <math.h>
#include <string.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

bool math_service_validate_buffer(const float *buffer, size_t count,
                                  size_t required, bool output,
                                  ApiError *error)
{
    if (buffer && count >= required)
        return true;
    api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                  output ? "math.invalid_output_buffer"
                         : "math.invalid_input_buffer",
                  output ? "Math output buffer is too small"
                         : "Math input buffer is too small");
    return false;
}

bool math_service_mat4_identity(float *out, size_t out_count, ApiError *error)
{
    api_error_clear(error);
    if (!math_service_validate_buffer(out, out_count, 16, true, error))
        return false;
    math_mat4_identity(out);
    return true;
}

bool math_service_mat4_invert(float *out, size_t out_count,
                              const float *matrix, size_t matrix_count,
                              ApiError *error)
{
    api_error_clear(error);
    if (!math_service_validate_buffer(out, out_count, 16, true, error) ||
        !math_service_validate_buffer(matrix, matrix_count, 16, false, error))
        return false;
    if (math_mat4_invert(out, matrix))
        return true;
    api_error_set(error, API_STATUS_APPLICATION_ERROR,
                  "math.singular_matrix",
                  "Matrix cannot be inverted because it is singular");
    return false;
}

bool math_service_vec3_scale(float *out, size_t out_count,
                             const float *value, size_t value_count,
                             float scale, ApiError *error)
{
    api_error_clear(error);
    if (!math_service_validate_buffer(out, out_count, 3, true, error) ||
        !math_service_validate_buffer(value, value_count, 3, false, error))
        return false;
    math_vec3_scale(out, value, scale);
    return true;
}

void math_mat4_identity(float *out)
{
    memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void math_mat4_multiply(float *out, const float *a, const float *b)
{
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t a0 = vld1q_f32(a + 0);
    float32x4_t a1 = vld1q_f32(a + 4);
    float32x4_t a2 = vld1q_f32(a + 8);
    float32x4_t a3 = vld1q_f32(a + 12);
    int i;
    for (i = 0; i < 4; ++i)
    {
        float32x4_t bc = vld1q_f32(b + i * 4);
        float32x4_t r = vmulq_n_f32(a0, vgetq_lane_f32(bc, 0));
        r = vmlaq_n_f32(r, a1, vgetq_lane_f32(bc, 1));
        r = vmlaq_n_f32(r, a2, vgetq_lane_f32(bc, 2));
        r = vmlaq_n_f32(r, a3, vgetq_lane_f32(bc, 3));
        vst1q_f32(out + i * 4, r);
    }
#else
    float r[16];
    int i, j, k;
    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            float s = 0.0f;
            for (k = 0; k < 4; ++k)
                s += a[k * 4 + j] * b[i * 4 + k];
            r[i * 4 + j] = s;
        }
    }
    memcpy(out, r, sizeof(r));
#endif
}

void math_mat4_perspective(float *out, float fovy, float aspect,
                           float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    float nf = 1.0f / (znear - zfar);
    memset(out, 0, 16 * sizeof(float));
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (zfar + znear) * nf;
    out[11] = -1.0f;
    out[14] = 2.0f * zfar * znear * nf;
}

void math_mat4_ortho(float *out, float left, float right,
                     float bottom, float top, float znear, float zfar)
{
    float lr = 1.0f / (left - right);
    float bt = 1.0f / (bottom - top);
    float nf = 1.0f / (znear - zfar);
    memset(out, 0, 16 * sizeof(float));
    out[0] = -2.0f * lr;
    out[5] = -2.0f * bt;
    out[10] = 2.0f * nf;
    out[12] = (left + right) * lr;
    out[13] = (top + bottom) * bt;
    out[14] = (zfar + znear) * nf;
    out[15] = 1.0f;
}

void math_mat4_lookat(float *out, const float *eye, const float *target,
                      const float *up)
{
    float fx = target[0] - eye[0];
    float fy = target[1] - eye[1];
    float fz = target[2] - eye[2];
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    float sx, sy, sz, sl, ux, uy, uz;
    if (fl < 1e-8f)
    {
        math_mat4_identity(out);
        return;
    }
    fl = 1.0f / fl;
    fx *= fl;
    fy *= fl;
    fz *= fl;
    
    sx = fy * up[2] - fz * up[1];
    sy = fz * up[0] - fx * up[2];
    sz = fx * up[1] - fy * up[0];
    sl = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sl < 1e-8f)
    {
        sx = 1.0f;
        sy = 0.0f;
        sz = 0.0f;
    }
    else
    {
        sl = 1.0f / sl;
        sx *= sl;
        sy *= sl;
        sz *= sl;
    }
    
    ux = sy * fz - sz * fy;
    uy = sz * fx - sx * fz;
    uz = sx * fy - sy * fx;

    out[0] = sx;
    out[1] = ux;
    out[2] = -fx;
    out[3] = 0.0f;
    out[4] = sy;
    out[5] = uy;
    out[6] = -fy;
    out[7] = 0.0f;
    out[8] = sz;
    out[9] = uz;
    out[10] = -fz;
    out[11] = 0.0f;
    out[12] = -(sx * eye[0] + sy * eye[1] + sz * eye[2]);
    out[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    out[14] = (fx * eye[0] + fy * eye[1] + fz * eye[2]);
    out[15] = 1.0f;
}

void math_mat4_translate(float *out, const float *a, const float *v)
{
    float x = v[0], y = v[1], z = v[2];
    float r[16];
    if (out != a)
        memcpy(r, a, sizeof(r));
    else
        memcpy(r, a, sizeof(r));
    
    r[12] = a[0] * x + a[4] * y + a[8] * z + a[12];
    r[13] = a[1] * x + a[5] * y + a[9] * z + a[13];
    r[14] = a[2] * x + a[6] * y + a[10] * z + a[14];
    r[15] = a[3] * x + a[7] * y + a[11] * z + a[15];
    memcpy(out, r, sizeof(r));
}

void math_mat4_rotate_x(float *out, const float *a, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    float a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    float a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    if (out != a)
        memcpy(out, a, 16 * sizeof(float));
    out[4] = a10 * c + a20 * s;
    out[5] = a11 * c + a21 * s;
    out[6] = a12 * c + a22 * s;
    out[7] = a13 * c + a23 * s;
    out[8] = a20 * c - a10 * s;
    out[9] = a21 * c - a11 * s;
    out[10] = a22 * c - a12 * s;
    out[11] = a23 * c - a13 * s;
}

void math_mat4_rotate_y(float *out, const float *a, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    float a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    float a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    if (out != a)
        memcpy(out, a, 16 * sizeof(float));
    out[0] = a00 * c - a20 * s;
    out[1] = a01 * c - a21 * s;
    out[2] = a02 * c - a22 * s;
    out[3] = a03 * c - a23 * s;
    out[8] = a00 * s + a20 * c;
    out[9] = a01 * s + a21 * c;
    out[10] = a02 * s + a22 * c;
    out[11] = a03 * s + a23 * c;
}

void math_mat4_rotate_z(float *out, const float *a, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    float a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    float a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    if (out != a)
        memcpy(out, a, 16 * sizeof(float));
    out[0] = a00 * c + a10 * s;
    out[1] = a01 * c + a11 * s;
    out[2] = a02 * c + a12 * s;
    out[3] = a03 * c + a13 * s;
    out[4] = a10 * c - a00 * s;
    out[5] = a11 * c - a01 * s;
    out[6] = a12 * c - a02 * s;
    out[7] = a13 * c - a03 * s;
}

void math_mat4_scale(float *out, const float *a, const float *v)
{
    float x = v[0], y = v[1], z = v[2];
    out[0] = a[0] * x;
    out[1] = a[1] * x;
    out[2] = a[2] * x;
    out[3] = a[3] * x;
    out[4] = a[4] * y;
    out[5] = a[5] * y;
    out[6] = a[6] * y;
    out[7] = a[7] * y;
    out[8] = a[8] * z;
    out[9] = a[9] * z;
    out[10] = a[10] * z;
    out[11] = a[11] * z;
    out[12] = a[12];
    out[13] = a[13];
    out[14] = a[14];
    out[15] = a[15];
}

bool math_mat4_invert(float *out, const float *a)
{
    float a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    float a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    float a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    float a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (det == 0.0f)
        return false;
    det = 1.0f / det;

    out[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det;
    out[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det;
    out[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det;
    out[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
    out[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det;
    out[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det;
    out[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det;
    out[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
    out[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det;
    out[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det;
    out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
    out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
    out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
    out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
    out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
    out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
    return true;
}

void math_mat4_transpose(float *out, const float *a)
{
    if (out == a)
    {
        float t;
        t = a[1];
        out[1] = a[4];
        out[4] = t;
        t = a[2];
        out[2] = a[8];
        out[8] = t;
        t = a[3];
        out[3] = a[12];
        out[12] = t;
        t = a[6];
        out[6] = a[9];
        out[9] = t;
        t = a[7];
        out[7] = a[13];
        out[13] = t;
        t = a[11];
        out[11] = a[14];
        out[14] = t;
        return;
    }
    out[0] = a[0];
    out[1] = a[4];
    out[2] = a[8];
    out[3] = a[12];
    out[4] = a[1];
    out[5] = a[5];
    out[6] = a[9];
    out[7] = a[13];
    out[8] = a[2];
    out[9] = a[6];
    out[10] = a[10];
    out[11] = a[14];
    out[12] = a[3];
    out[13] = a[7];
    out[14] = a[11];
    out[15] = a[15];
}

void math_vec3_add(float *out, const float *a, const float *b)
{
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

void math_vec3_sub(float *out, const float *a, const float *b)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

void math_vec3_scale(float *out, const float *a, float s)
{
    out[0] = a[0] * s;
    out[1] = a[1] * s;
    out[2] = a[2] * s;
}

void math_vec3_normalize(float *out, const float *a)
{
    float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (l < 1e-8f)
    {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    l = 1.0f / l;
    out[0] = a[0] * l;
    out[1] = a[1] * l;
    out[2] = a[2] * l;
}

void math_vec3_cross(float *out, const float *a, const float *b)
{
    float x = a[1] * b[2] - a[2] * b[1];
    float y = a[2] * b[0] - a[0] * b[2];
    float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

float math_vec3_dot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float math_vec3_length(const float *a)
{
    return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

void math_vec3_transform_mat4(float *out, const float *v, const float *m)
{
    float x = v[0], y = v[1], z = v[2];
    float w = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (w == 0.0f)
        w = 1.0f;
    out[0] = (m[0] * x + m[4] * y + m[8] * z + m[12]) / w;
    out[1] = (m[1] * x + m[5] * y + m[9] * z + m[13]) / w;
    out[2] = (m[2] * x + m[6] * y + m[10] * z + m[14]) / w;
}

void math_vec3_normalize_many(float *out, const float *v, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        math_vec3_normalize(out + i * 3, v + i * 3);
}

void math_vec3_transform_mat4_many(float *out, const float *v, const float *m, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        math_vec3_transform_mat4(out + i * 3, v + i * 3, m);
}

void math_quat_from_axis_angle(float *out, const float *axis, float ang)
{
    float half = ang * 0.5f;
    float s = sinf(half);
    float l = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (l < 1e-8f)
    {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    l = s / l;
    out[0] = axis[0] * l;
    out[1] = axis[1] * l;
    out[2] = axis[2] * l;
    out[3] = cosf(half);
}

void math_quat_multiply(float *out, const float *a, const float *b)
{
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = ax * bw + aw * bx + ay * bz - az * by;
    out[1] = ay * bw + aw * by + az * bx - ax * bz;
    out[2] = az * bw + aw * bz + ax * by - ay * bx;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

void math_quat_slerp(float *out, const float *a, const float *b, float t)
{
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    float cosom = ax * bx + ay * by + az * bz + aw * bw;
    float scale0, scale1;
    if (cosom < 0.0f)
    {
        cosom = -cosom;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }
    if (1.0f - cosom > 1e-6f)
    {
        float omega = acosf(cosom);
        float sinom = sinf(omega);
        scale0 = sinf((1.0f - t) * omega) / sinom;
        scale1 = sinf(t * omega) / sinom;
    }
    else
    {
        scale0 = 1.0f - t;
        scale1 = t;
    }
    out[0] = scale0 * ax + scale1 * bx;
    out[1] = scale0 * ay + scale1 * by;
    out[2] = scale0 * az + scale1 * bz;
    out[3] = scale0 * aw + scale1 * bw;
}

void math_quat_to_mat4(float *out, const float *q)
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    out[0] = 1.0f - (yy + zz);
    out[1] = xy + wz;
    out[2] = xz - wy;
    out[3] = 0.0f;
    out[4] = xy - wz;
    out[5] = 1.0f - (xx + zz);
    out[6] = yz + wx;
    out[7] = 0.0f;
    out[8] = xz + wy;
    out[9] = yz - wx;
    out[10] = 1.0f - (xx + yy);
    out[11] = 0.0f;
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
}