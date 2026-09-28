#include "gl_shader_pipeline.h"

#include <stdio.h>
#include <stdlib.h>

static void set_error(char *output, size_t capacity, const char *message)
{
    if (output && capacity > 0)
        snprintf(output, capacity, "%s", message ? message : "GL shader pipeline failed");
}

static uint32_t compile_shader(
    const GlShaderPipelineApi *api,
    const GlShaderPipelineConstants *constants,
    BudoGLSLTarget target, uint32_t shader_type,
    const char *source, const char *label, bool require_app_dialect,
    char *error_message, size_t error_capacity)
{
    const char *dialect_error = NULL;
    char *prepared_source = budo_glsl_prepare(
        source, target, require_app_dialect, &dialect_error);
    uint32_t shader;
    int compiled = 0;

    if (!prepared_source)
    {
        set_error(error_message, error_capacity,
                  dialect_error ? dialect_error : "Failed to prepare shader source");
        return 0;
    }

    shader = api->create_shader(shader_type);
    if (!shader)
    {
        free(prepared_source);
        set_error(error_message, error_capacity, "glCreateShader failed");
        return 0;
    }

    api->shader_source(shader, 1, (const char *const *)&prepared_source, NULL);
    api->compile_shader(shader);
    api->get_shader_iv(shader, constants->compile_status, &compiled);
    if (compiled != constants->true_value)
    {
        char info_log[1024] = {0};
        api->get_shader_info_log(shader, (int)sizeof(info_log), NULL, info_log);
        snprintf(error_message, error_capacity, "%s compile failed: %s",
                 label, info_log);
        api->delete_shader(shader);
        free(prepared_source);
        return 0;
    }

    free(prepared_source);
    return shader;
}

uint32_t gl_shader_pipeline_create(
    const GlShaderPipelineApi *api,
    const GlShaderPipelineConstants *constants,
    BudoGLSLTarget target,
    const char *vertex_source,
    const char *fragment_source,
    bool require_app_dialect,
    char *error_message,
    size_t error_capacity)
{
    uint32_t vertex_shader;
    uint32_t fragment_shader;
    uint32_t program;
    int linked = 0;

    if (!api || !constants || !vertex_source || !fragment_source ||
        !error_message || error_capacity == 0)
        return 0;

    vertex_shader = compile_shader(
        api, constants, target, constants->vertex_shader,
        vertex_source, "Vertex shader", require_app_dialect,
        error_message, error_capacity);
    if (!vertex_shader)
        return 0;

    fragment_shader = compile_shader(
        api, constants, target, constants->fragment_shader,
        fragment_source, "Fragment shader", require_app_dialect,
        error_message, error_capacity);
    if (!fragment_shader)
    {
        api->delete_shader(vertex_shader);
        return 0;
    }

    program = api->create_program();
    if (!program)
    {
        set_error(error_message, error_capacity, "glCreateProgram failed");
    }
    else
    {
        api->attach_shader(program, vertex_shader);
        api->attach_shader(program, fragment_shader);
        api->bind_attrib_location(program, 0, "a_position");
        api->bind_attrib_location(program, 1, "a_texCoord");
        api->bind_attrib_location(program, 1, "a_uv");
        api->bind_attrib_location(program, 2, "a_normal");
        api->bind_attrib_location(program, 3, "a_color");
        api->bind_attrib_location(program, 4, "a_tangent");
        api->link_program(program);
        api->get_program_iv(program, constants->link_status, &linked);
        if (linked != constants->true_value)
        {
            char info_log[1024] = {0};
            api->get_program_info_log(program, (int)sizeof(info_log), NULL, info_log);
            snprintf(error_message, error_capacity,
                     "Program link failed: %s", info_log);
            api->delete_program(program);
            program = 0;
        }
    }

    api->delete_shader(vertex_shader);
    api->delete_shader(fragment_shader);
    return program;
}