#ifndef BUDO_GL_SHADER_PIPELINE_H
#define BUDO_GL_SHADER_PIPELINE_H

#include "core/gl_shader_dialect.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct GlShaderPipelineApi
    {
        uint32_t (*create_shader)(uint32_t shader_type);
        void (*shader_source)(uint32_t shader, int count,
                              const char *const *source, const int *length);
        void (*compile_shader)(uint32_t shader);
        void (*get_shader_iv)(uint32_t shader, uint32_t name, int *value);
        void (*get_shader_info_log)(uint32_t shader, int capacity,
                                    int *length, char *log);
        void (*delete_shader)(uint32_t shader);
        uint32_t (*create_program)(void);
        void (*attach_shader)(uint32_t program, uint32_t shader);
        void (*bind_attrib_location)(uint32_t program, uint32_t index,
                                     const char *name);
        void (*link_program)(uint32_t program);
        void (*get_program_iv)(uint32_t program, uint32_t name, int *value);
        void (*get_program_info_log)(uint32_t program, int capacity,
                                     int *length, char *log);
        void (*delete_program)(uint32_t program);
    } GlShaderPipelineApi;

    typedef struct GlShaderPipelineConstants
    {
        uint32_t vertex_shader;
        uint32_t fragment_shader;
        uint32_t compile_status;
        uint32_t link_status;
        int true_value;
    } GlShaderPipelineConstants;

    uint32_t gl_shader_pipeline_create(
        const GlShaderPipelineApi *api,
        const GlShaderPipelineConstants *constants,
        BudoGLSLTarget target,
        const char *vertex_source,
        const char *fragment_source,
        bool require_app_dialect,
        char *error_message,
        size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif