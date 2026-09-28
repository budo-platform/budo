#ifndef MATH_WRAPPER_H
#define MATH_WRAPPER_H

#include "core/api_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    bool math_service_mat4_identity(float *out, size_t out_count,
                                    ApiError *error);
    bool math_service_mat4_invert(float *out, size_t out_count,
                                  const float *matrix, size_t matrix_count,
                                  ApiError *error);
    bool math_service_vec3_scale(float *out, size_t out_count,
                                 const float *value, size_t value_count,
                                 float scale, ApiError *error);
    bool math_service_validate_buffer(const float *buffer, size_t count,
                                      size_t required, bool output,
                                      ApiError *error);

    void math_mat4_identity(float *out);
    void math_mat4_multiply(float *out, const float *a, const float *b);
    void math_mat4_perspective(float *out, float fovy_rad, float aspect,
                               float znear, float zfar);
    void math_mat4_ortho(float *out, float left, float right,
                         float bottom, float top, float znear, float zfar);
    void math_mat4_lookat(float *out, const float *eye, const float *target,
                          const float *up);
    void math_mat4_translate(float *out, const float *a, const float *v3);
    void math_mat4_rotate_x(float *out, const float *a, float angle_rad);
    void math_mat4_rotate_y(float *out, const float *a, float angle_rad);
    void math_mat4_rotate_z(float *out, const float *a, float angle_rad);
    void math_mat4_scale(float *out, const float *a, const float *v3);
    bool math_mat4_invert(float *out, const float *a);
    void math_mat4_transpose(float *out, const float *a);

    void math_vec3_add(float *out, const float *a, const float *b);
    void math_vec3_sub(float *out, const float *a, const float *b);
    void math_vec3_scale(float *out, const float *a, float s);
    void math_vec3_normalize(float *out, const float *a);
    void math_vec3_cross(float *out, const float *a, const float *b);
    float math_vec3_dot(const float *a, const float *b);
    float math_vec3_length(const float *a);
    
    void math_vec3_transform_mat4(float *out, const float *v3, const float *m);
    void math_vec3_normalize_many(float *out, const float *v3, int count);
    
    void math_vec3_transform_mat4_many(float *out, const float *v3, const float *m, int count);

    void math_quat_from_axis_angle(float *out, const float *axis3, float angle_rad);
    void math_quat_multiply(float *out, const float *a, const float *b);
    void math_quat_slerp(float *out, const float *a, const float *b, float t);
    void math_quat_to_mat4(float *out, const float *q);

#ifdef __cplusplus
}
#endif

#endif