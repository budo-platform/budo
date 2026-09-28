#include "math3d.h"

#include <math.h>
#include <string.h>

float clampf(float value, float minimum, float maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

Vec3 vec3(float x, float y, float z)
{
    Vec3 result = {x, y, z};
    return result;
}

Vec3 vec3_add(Vec3 a, Vec3 b)
{
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vec3 vec3_sub(Vec3 a, Vec3 b)
{
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vec3 vec3_scale(Vec3 value, float scale)
{
    return vec3(value.x * scale, value.y * scale, value.z * scale);
}

float vec3_dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

Vec3 vec3_normalize(Vec3 value)
{
    float length = sqrtf(vec3_dot(value, value));
    return length > 0.000001f ? vec3_scale(value, 1.0f / length)
                              : vec3(0.0f, 0.0f, 1.0f);
}

void mat4_identity(float out[16])
{
    memset(out, 0, sizeof(float) * 16);
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void mat4_multiply(float out[16], const float a[16], const float b[16])
{
    float result[16];
    int column;
    int row;
    for (column = 0; column < 4; ++column)
    {
        for (row = 0; row < 4; ++row)
        {
            result[column * 4 + row] =
                a[0 * 4 + row] * b[column * 4 + 0] +
                a[1 * 4 + row] * b[column * 4 + 1] +
                a[2 * 4 + row] * b[column * 4 + 2] +
                a[3 * 4 + row] * b[column * 4 + 3];
        }
    }
    memcpy(out, result, sizeof(result));
}

void mat4_perspective(float out[16], float fov_y, float aspect, float near_z,
                      float far_z)
{
    float f = 1.0f / tanf(fov_y * 0.5f);
    memset(out, 0, sizeof(float) * 16);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (far_z + near_z) / (near_z - far_z);
    out[11] = -1.0f;
    out[14] = (2.0f * far_z * near_z) / (near_z - far_z);
}

void mat4_look_at(float out[16], Vec3 eye, Vec3 center, Vec3 up)
{
    Vec3 forward = vec3_normalize(vec3_sub(center, eye));
    Vec3 side = vec3_normalize(vec3_cross(forward, up));
    Vec3 camera_up = vec3_cross(side, forward);
    mat4_identity(out);
    out[0] = side.x;
    out[4] = side.y;
    out[8] = side.z;
    out[1] = camera_up.x;
    out[5] = camera_up.y;
    out[9] = camera_up.z;
    out[2] = -forward.x;
    out[6] = -forward.y;
    out[10] = -forward.z;
    out[12] = -vec3_dot(side, eye);
    out[13] = -vec3_dot(camera_up, eye);
    out[14] = vec3_dot(forward, eye);
}