#ifndef BUDO_GL_STATE_GUARD_H
#define BUDO_GL_STATE_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GL_STATE_GUARD_MAX_TEXTURE_UNITS 8

    typedef struct GlStateGuardApi
    {
        void (*get_integer)(uint32_t name, int *values);
        void (*get_boolean)(uint32_t name, unsigned char *values);
        bool (*is_enabled)(uint32_t capability);
        void (*set_enabled)(uint32_t capability, bool enabled);
        void (*bind_framebuffer)(uint32_t target, uint32_t framebuffer);
        void (*set_viewport)(int x, int y, int width, int height);
        void (*use_program)(uint32_t program);
        void (*bind_vertex_array)(uint32_t vertex_array);
        void (*bind_buffer)(uint32_t target, uint32_t buffer);
        void (*bind_renderbuffer)(uint32_t target, uint32_t renderbuffer);
        void (*active_texture)(uint32_t texture);
        void (*bind_texture)(uint32_t target, uint32_t texture);
        void (*set_blend_func)(uint32_t src_rgb, uint32_t dst_rgb,
                               uint32_t src_alpha, uint32_t dst_alpha);
        void (*set_blend_equation)(uint32_t rgb, uint32_t alpha);
        void (*set_depth_func)(uint32_t function);
        void (*set_cull_face)(uint32_t mode);
        void (*set_front_face)(uint32_t mode);
        void (*set_scissor)(int x, int y, int width, int height);
        void (*set_color_mask)(bool red, bool green, bool blue, bool alpha);
        void (*set_depth_mask)(bool enabled);
    } GlStateGuardApi;

    typedef struct GlStateGuardConstants
    {
        uint32_t framebuffer_binding;
        uint32_t framebuffer;
        uint32_t viewport;
        uint32_t current_program;
        uint32_t vertex_array_binding;
        uint32_t array_buffer_binding;
        uint32_t array_buffer;
        uint32_t element_array_buffer_binding;
        uint32_t element_array_buffer;
        uint32_t renderbuffer_binding;
        uint32_t renderbuffer;
        uint32_t active_texture_name;
        uint32_t texture0;
        uint32_t texture_binding_2d;
        uint32_t texture_2d;
        uint32_t texture_binding_cube;
        uint32_t texture_cube;
        uint32_t max_texture_units;
        uint32_t blend;
        uint32_t depth_test;
        uint32_t cull_face;
        uint32_t scissor_test;
        uint32_t blend_src_rgb;
        uint32_t blend_dst_rgb;
        uint32_t blend_src_alpha;
        uint32_t blend_dst_alpha;
        uint32_t blend_equation_rgb;
        uint32_t blend_equation_alpha;
        uint32_t depth_function;
        uint32_t cull_face_mode;
        uint32_t front_face;
        uint32_t scissor_box;
        uint32_t color_writemask;
        uint32_t depth_writemask;
    } GlStateGuardConstants;

    typedef struct GlStateSnapshot
    {
        int framebuffer;
        int viewport[4];
        int program;
        int vertex_array;
        int array_buffer;
        int element_array_buffer;
        int renderbuffer;
        int active_texture;
        int texture_unit_count;
        int texture_2d[GL_STATE_GUARD_MAX_TEXTURE_UNITS];
        int texture_cube[GL_STATE_GUARD_MAX_TEXTURE_UNITS];
        bool blend_enabled;
        bool depth_test_enabled;
        bool cull_face_enabled;
        bool scissor_test_enabled;
        int blend_src_rgb;
        int blend_dst_rgb;
        int blend_src_alpha;
        int blend_dst_alpha;
        int blend_equation_rgb;
        int blend_equation_alpha;
        int depth_function;
        int cull_face_mode;
        int front_face;
        int scissor_box[4];
        unsigned char color_mask[4];
        unsigned char depth_mask;
    } GlStateSnapshot;

    typedef void (*GlStateGuardTransition)(void *context);

    bool gl_state_guard_capture(const GlStateGuardApi *api,
                                const GlStateGuardConstants *constants,
                                GlStateSnapshot *snapshot);
    void gl_state_guard_restore(const GlStateGuardApi *api,
                                const GlStateGuardConstants *constants,
                                const GlStateSnapshot *snapshot);
    bool gl_state_guard_run(const GlStateGuardApi *api,
                            const GlStateGuardConstants *constants,
                            GlStateGuardTransition transition,
                            void *context);

#ifdef __cplusplus
}
#endif

#endif