#include "graphics/gl_shader_pipeline.h"

#include <stdio.h>
#include <string.h>

static int compile_succeeds;
static int link_succeeds;
static int deleted_shaders;
static int deleted_programs;
static int attribute_bindings;
static uint32_t next_shader;

static uint32_t create_shader(uint32_t type)
{
    (void)type;
    return ++next_shader;
}

static void shader_source(uint32_t shader, int count,
                          const char *const *source, const int *length)
{
    (void)shader;
    (void)count;
    (void)source;
    (void)length;
}

static void compile(uint32_t shader) { (void)shader; }
static void shader_iv(uint32_t shader, uint32_t name, int *value)
{
    (void)shader;
    (void)name;
    *value = compile_succeeds;
}
static void shader_log(uint32_t shader, int capacity, int *length, char *log)
{
    (void)shader;
    (void)length;
    snprintf(log, (size_t)capacity, "mock compile error");
}
static void delete_shader(uint32_t shader)
{
    (void)shader;
    deleted_shaders++;
}
static uint32_t create_program(void) { return 77; }
static void attach_shader(uint32_t program, uint32_t shader)
{
    (void)program;
    (void)shader;
}
static void bind_attribute(uint32_t program, uint32_t index, const char *name)
{
    (void)program;
    (void)index;
    (void)name;
    attribute_bindings++;
}
static void link(uint32_t program) { (void)program; }
static void program_iv(uint32_t program, uint32_t name, int *value)
{
    (void)program;
    (void)name;
    *value = link_succeeds;
}
static void program_log(uint32_t program, int capacity, int *length, char *log)
{
    (void)program;
    (void)length;
    snprintf(log, (size_t)capacity, "mock link error");
}
static void delete_program(uint32_t program)
{
    (void)program;
    deleted_programs++;
}

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "gl_shader_pipeline_test: %s\n", message);
    return 1;
}

int main(void)
{
    const GlShaderPipelineApi api = {
        create_shader, shader_source, compile, shader_iv, shader_log,
        delete_shader, create_program, attach_shader, bind_attribute,
        link, program_iv, program_log, delete_program};
    const GlShaderPipelineConstants constants = {1, 2, 3, 4, 1};
    const char *vertex = "#version 150\nvoid main() { gl_Position = vec4(0.0); }";
    const char *fragment = "#version 150\nout vec4 c; void main() { c = vec4(1.0); }";
    char error[256] = {0};
    uint32_t program;

    compile_succeeds = 1;
    link_succeeds = 1;
    program = gl_shader_pipeline_create(
        &api, &constants, BUDO_GLSL_TARGET_DESKTOP_GL150,
        vertex, fragment, false, error, sizeof(error));
    if (check(program == 77, "successful program was not returned") ||
        check(deleted_shaders == 2, "successful shaders were not cleaned") ||
        check(attribute_bindings == 6, "stable attributes were not bound"))
        return 1;

    deleted_shaders = 0;
    compile_succeeds = 0;
    program = gl_shader_pipeline_create(
        &api, &constants, BUDO_GLSL_TARGET_DESKTOP_GL150,
        vertex, fragment, false, error, sizeof(error));
    if (check(program == 0, "compile failure returned a program") ||
        check(deleted_shaders == 1, "failed shader was not deleted") ||
        check(strstr(error, "mock compile error") != NULL,
              "compile diagnostic was not retained"))
        return 1;

    deleted_shaders = 0;
    compile_succeeds = 1;
    link_succeeds = 0;
    program = gl_shader_pipeline_create(
        &api, &constants, BUDO_GLSL_TARGET_DESKTOP_GL150,
        vertex, fragment, false, error, sizeof(error));
    if (check(program == 0, "link failure returned a program") ||
        check(deleted_shaders == 2, "link-failure shaders were not deleted") ||
        check(deleted_programs == 1, "failed program was not deleted") ||
        check(strstr(error, "mock link error") != NULL,
              "link diagnostic was not retained"))
        return 1;

    puts("gl_shader_pipeline_test: ok");
    return 0;
}