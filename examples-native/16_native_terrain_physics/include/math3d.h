#ifndef NATIVE_TERRAIN_MATH3D_H
#define NATIVE_TERRAIN_MATH3D_H

typedef struct Vec3
{
    float x;
    float y;
    float z;
} Vec3;

float clampf(float value, float minimum, float maximum);
Vec3 vec3(float x, float y, float z);
Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 value, float scale);
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
Vec3 vec3_normalize(Vec3 value);
void mat4_identity(float out[16]);
void mat4_multiply(float out[16], const float a[16], const float b[16]);
void mat4_perspective(float out[16], float fov_y, float aspect, float near_z,
                      float far_z);
void mat4_look_at(float out[16], Vec3 eye, Vec3 center, Vec3 up);

#endif