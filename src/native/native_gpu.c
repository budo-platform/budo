#include "native_host_internal.h"
#include "core/window.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BUDO_GPU_MAX_ATTRIBUTES 8u
#define BUDO_GPU_MAX_TEXTURE_UNITS 8u

struct BudoShaderProgram
{
    BudoHost *host;
    Window *window;
    int id;
    uint64_t generation;
};

struct BudoGpuBuffer
{
    BudoHost *host;
    Window *window;
    int id;
    BudoGpuBufferTarget target;
    size_t size;
    uint32_t references;
    uint64_t generation;
};

struct BudoTexture
{
    BudoHost *host;
    Window *window;
    int id;
    uint32_t width;
    uint32_t height;
    uint64_t generation;
};

struct BudoRenderTarget
{
    BudoHost *host;
    Window *window;
    int id;
    uint32_t width;
    uint32_t height;
    uint64_t generation;
};

typedef struct BudoMeshAttributeBinding
{
    bool active;
    uint32_t location;
    BudoGpuBuffer *buffer;
} BudoMeshAttributeBinding;

struct BudoMesh
{
    BudoHost *host;
    Window *window;
    int id;
    BudoMeshAttributeBinding attributes[BUDO_GPU_MAX_ATTRIBUTES];
    BudoGpuBuffer *index_buffer;
    uint64_t generation;
};

static BudoStatus gpu_error(BudoHost *host, BudoStatus status,
                            const char *message)
{
    budo_host_set_error(host, status, message);
    return status;
}

static BudoStatus backend_error(BudoHost *host, BudoStatus status,
                                const char *fallback)
{
    const char *message = host && host->graphics_window
                              ? window_gl_get_error(host->graphics_window)
                              : NULL;
    return gpu_error(host, status,
                     message && message[0] ? message : fallback);
}

static BudoStatus validate_gpu_callback(BudoHost *host)
{
    if (!host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    if (!host->graphics_window)
        return gpu_error(host, BUDO_STATUS_UNSUPPORTED,
                         "GPU resources are unavailable on this host");
    if (!budo_native_host_graphics_available(host))
        return gpu_error(host, BUDO_STATUS_INVALID_STATE,
                         "GPU resources require an active graphics callback");
    return BUDO_STATUS_OK;
}

static bool valid_name(const char *name)
{
    return name && name[0] != '\0';
}

static bool finite_floats(const float *values, size_t count)
{
    size_t i;
    if (!values)
        return false;
    for (i = 0; i < count; ++i)
    {
        if (!isfinite(values[i]))
            return false;
    }
    return true;
}

static bool rgba8_size(uint32_t width, uint32_t height, size_t *out_size)
{
    size_t pixels;
    if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX)
        return false;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return false;
    pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / 4u)
        return false;
    *out_size = pixels * 4u;
    return true;
}

bool budo_host_graphics_capabilities(
    const BudoHost *host, BudoGraphicsCapabilities *out_capabilities)
{
    uint32_t supplied_size;
    if (!host || !host->graphics_window || !out_capabilities ||
        out_capabilities->struct_size < sizeof(*out_capabilities))
        return false;
    supplied_size = out_capabilities->struct_size;
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->struct_size = supplied_size;
    out_capabilities->feature_flags =
        BUDO_GRAPHICS_FEATURE_SHADER_PROGRAMS |
        BUDO_GRAPHICS_FEATURE_VERTEX_BUFFERS |
        BUDO_GRAPHICS_FEATURE_INDEX_BUFFERS |
        BUDO_GRAPHICS_FEATURE_TEXTURE_2D_RGBA8 |
        BUDO_GRAPHICS_FEATURE_MESH_DRAWING |
        BUDO_GRAPHICS_FEATURE_SHADER_FILES |
        BUDO_GRAPHICS_FEATURE_ENCODED_IMAGE_TEXTURES |
        BUDO_GRAPHICS_FEATURE_RENDER_TARGET_RGBA8 |
        BUDO_GRAPHICS_FEATURE_FULLSCREEN_PASSES |
        BUDO_GRAPHICS_FEATURE_REGION_PASSES;
    out_capabilities->max_texture_units = BUDO_GPU_MAX_TEXTURE_UNITS;
    out_capabilities->max_vertex_attributes = BUDO_GPU_MAX_ATTRIBUTES;
    return true;
}

static BudoStatus validate_program(BudoShaderProgram *program)
{
    BudoStatus status;
    if (!program || !program->host || program->id <= 0)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(program->host);
    if (status != BUDO_STATUS_OK)
        return status;
    if (program->window != program->host->graphics_window ||
        program->generation != program->host->graphics_generation)
        return gpu_error(program->host, BUDO_STATUS_INVALID_STATE,
                         "Shader program belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

static BudoShaderProgram *wrap_program(BudoHost *host, int id)
{
    BudoShaderProgram *program = calloc(1, sizeof(*program));
    if (!program)
    {
        window_gl_destroy_program(host->graphics_window, id);
        gpu_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                  "Could not allocate shader program handle");
        return NULL;
    }
    program->host = host;
    program->window = host->graphics_window;
    program->id = id;
    program->generation = host->graphics_generation;
    return program;
}

BudoShaderProgram *budo_shader_program_create(
    BudoHost *host, const char *vertex_source, size_t vertex_length,
    const char *fragment_source, size_t fragment_length)
{
    BudoShaderProgram *program;
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!vertex_source || vertex_length == 0 || !fragment_source ||
        fragment_length == 0 || memchr(vertex_source, '\0', vertex_length) ||
        memchr(fragment_source, '\0', fragment_length))
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "Shader sources must be non-empty and cannot contain embedded NUL bytes");
        return NULL;
    }
    id = window_gl_create_program_from_source(host->graphics_window,
                                              vertex_source, vertex_length,
                                              fragment_source, fragment_length);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_APPLICATION_ERROR,
                      "Shader compilation or linking failed");
        return NULL;
    }
    program = wrap_program(host, id);
    return program;
}

BudoShaderProgram *budo_shader_program_create_from_files(
    BudoHost *host, const char *vertex_path, const char *fragment_path)
{
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!valid_name(vertex_path) || !valid_name(fragment_path))
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "Vertex and fragment shader asset paths are required");
        return NULL;
    }
    id = window_gl_create_program(host->graphics_window, vertex_path,
                                  fragment_path);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_APPLICATION_ERROR,
                      "Could not load, compile, or link shader asset files");
        return NULL;
    }
    return wrap_program(host, id);
}

BudoStatus budo_shader_program_destroy(BudoShaderProgram *program)
{
    BudoStatus status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!window_gl_destroy_program(program->window, program->id))
        return backend_error(program->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not destroy shader program");
    program->id = 0;
    free(program);
    return BUDO_STATUS_OK;
}

BudoStatus budo_shader_program_attribute_location(
    BudoShaderProgram *program, const char *name, int32_t *out_location)
{
    BudoStatus status = validate_program(program);
    int location;
    if (status != BUDO_STATUS_OK)
        return status;
    if (!valid_name(name) || !out_location)
        return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Attribute name and output location are required");
    location = window_gl_get_attrib_location(program->window, program->id, name);
    if (location < 0)
        return gpu_error(program->host, BUDO_STATUS_APPLICATION_ERROR,
                         "Shader attribute was not found or was optimized out");
    *out_location = location;
    return BUDO_STATUS_OK;
}

#define UNIFORM_SCALAR(name, backend, type, validation)                        \
    BudoStatus name(BudoShaderProgram *program, const char *uniform_name,      \
                    type value)                                                \
    {                                                                          \
        BudoStatus status = validate_program(program);                         \
        if (status != BUDO_STATUS_OK)                                          \
            return status;                                                     \
        if (!valid_name(uniform_name) || !(validation))                        \
            return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,      \
                             "Uniform name and value are invalid");            \
        if (!backend(program->window, program->id, uniform_name, value))       \
            return backend_error(program->host, BUDO_STATUS_APPLICATION_ERROR, \
                                 "Could not set shader uniform");              \
        return BUDO_STATUS_OK;                                                 \
    }

UNIFORM_SCALAR(budo_shader_program_set_uniform_1i, window_gl_set_uniform_1i,
               int32_t, true)
UNIFORM_SCALAR(budo_shader_program_set_uniform_1f, window_gl_set_uniform_1f,
               float, isfinite(value))

BudoStatus budo_shader_program_set_uniform_2f(
    BudoShaderProgram *program, const char *name, float x, float y)
{
    float values[] = {x, y};
    BudoStatus status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!valid_name(name) || !finite_floats(values, 2))
        return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Uniform name and values are invalid");
    if (!window_gl_set_uniform_2f(program->window, program->id, name, x, y))
        return backend_error(program->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not set shader uniform");
    return BUDO_STATUS_OK;
}

BudoStatus budo_shader_program_set_uniform_3f(
    BudoShaderProgram *program, const char *name, float x, float y, float z)
{
    float values[] = {x, y, z};
    BudoStatus status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!valid_name(name) || !finite_floats(values, 3))
        return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Uniform name and values are invalid");
    if (!window_gl_set_uniform_3f(program->window, program->id, name, x, y, z))
        return backend_error(program->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not set shader uniform");
    return BUDO_STATUS_OK;
}

BudoStatus budo_shader_program_set_uniform_4f(
    BudoShaderProgram *program, const char *name, float x, float y,
    float z, float w)
{
    float values[] = {x, y, z, w};
    BudoStatus status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!valid_name(name) || !finite_floats(values, 4))
        return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Uniform name and values are invalid");
    if (!window_gl_set_uniform_4f(program->window, program->id, name,
                                  x, y, z, w))
        return backend_error(program->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not set shader uniform");
    return BUDO_STATUS_OK;
}

BudoStatus budo_shader_program_set_uniform_matrix4(
    BudoShaderProgram *program, const char *name, const float values[16])
{
    BudoStatus status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!valid_name(name) || !finite_floats(values, 16))
        return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Matrix uniform name and values are invalid");
    if (!window_gl_set_uniform_matrix4fv(program->window, program->id,
                                         name, values, 1))
        return backend_error(program->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not set matrix uniform");
    return BUDO_STATUS_OK;
}

static BudoStatus validate_buffer(BudoGpuBuffer *buffer)
{
    BudoStatus status;
    if (!buffer || !buffer->host || buffer->id <= 0)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(buffer->host);
    if (status != BUDO_STATUS_OK)
        return status;
    if (buffer->window != buffer->host->graphics_window ||
        buffer->generation != buffer->host->graphics_generation)
        return gpu_error(buffer->host, BUDO_STATUS_INVALID_STATE,
                         "GPU buffer belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

BudoGpuBuffer *budo_gpu_buffer_create(BudoHost *host,
                                      BudoGpuBufferTarget target,
                                      const void *data, size_t size,
                                      BudoGpuBufferUsage usage)
{
    BudoGpuBuffer *buffer;
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!data || size == 0 ||
        (target != BUDO_GPU_BUFFER_VERTEX && target != BUDO_GPU_BUFFER_INDEX) ||
        (usage != BUDO_GPU_USAGE_STATIC && usage != BUDO_GPU_USAGE_DYNAMIC))
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "GPU buffer data, target, size, or usage is invalid");
        return NULL;
    }
    id = window_gl_create_buffer(host->graphics_window);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_INTERNAL_ERROR,
                      "Could not create GPU buffer");
        return NULL;
    }
    if (!window_gl_buffer_data(host->graphics_window, id,
                               (WindowGLBufferTarget)target, data, size,
                               (WindowGLBufferUsage)usage))
    {
        window_gl_destroy_buffer(host->graphics_window, id);
        backend_error(host, BUDO_STATUS_INTERNAL_ERROR,
                      "Could not upload GPU buffer data");
        return NULL;
    }
    buffer = calloc(1, sizeof(*buffer));
    if (!buffer)
    {
        window_gl_destroy_buffer(host->graphics_window, id);
        gpu_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                  "Could not allocate GPU buffer handle");
        return NULL;
    }
    buffer->host = host;
    buffer->window = host->graphics_window;
    buffer->id = id;
    buffer->target = target;
    buffer->size = size;
    buffer->generation = host->graphics_generation;
    return buffer;
}

BudoStatus budo_gpu_buffer_update(BudoGpuBuffer *buffer, size_t offset,
                                  const void *data, size_t size)
{
    BudoStatus status = validate_buffer(buffer);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!data || size == 0 || offset > buffer->size ||
        size > buffer->size - offset)
        return gpu_error(buffer->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "GPU buffer update is outside the allocated range");
    if (!window_gl_buffer_sub_data(buffer->window, buffer->id,
                                   offset, data, size))
        return backend_error(buffer->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not update GPU buffer");
    return BUDO_STATUS_OK;
}

BudoStatus budo_gpu_buffer_destroy(BudoGpuBuffer *buffer)
{
    BudoStatus status = validate_buffer(buffer);
    if (status != BUDO_STATUS_OK)
        return status;
    if (buffer->references != 0)
        return gpu_error(buffer->host, BUDO_STATUS_INVALID_STATE,
                         "GPU buffer is still referenced by a mesh");
    if (!window_gl_destroy_buffer(buffer->window, buffer->id))
        return backend_error(buffer->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not destroy GPU buffer");
    buffer->id = 0;
    free(buffer);
    return BUDO_STATUS_OK;
}

static BudoStatus validate_texture(BudoTexture *texture)
{
    BudoStatus status;
    if (!texture || !texture->host || texture->id <= 0)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(texture->host);
    if (status != BUDO_STATUS_OK)
        return status;
    if (texture->window != texture->host->graphics_window ||
        texture->generation != texture->host->graphics_generation)
        return gpu_error(texture->host, BUDO_STATUS_INVALID_STATE,
                         "Texture belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

static BudoTexture *wrap_texture(BudoHost *host, int id,
                                 uint32_t width, uint32_t height)
{
    BudoTexture *texture = calloc(1, sizeof(*texture));
    if (!texture)
    {
        window_gl_destroy_texture(host->graphics_window, id);
        gpu_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                  "Could not allocate texture handle");
        return NULL;
    }
    texture->host = host;
    texture->window = host->graphics_window;
    texture->id = id;
    texture->width = width;
    texture->height = height;
    texture->generation = host->graphics_generation;
    return texture;
}

static BudoTexture *wrap_loaded_texture(BudoHost *host, int id)
{
    int width = 0;
    int height = 0;
    if (id <= 0)
        return NULL;
    if (!window_gl_get_texture_size(host->graphics_window, id,
                                    &width, &height) ||
        width <= 0 || height <= 0)
    {
        window_gl_destroy_texture(host->graphics_window, id);
        backend_error(host, BUDO_STATUS_INTERNAL_ERROR,
                      "Could not query decoded texture dimensions");
        return NULL;
    }
    return wrap_texture(host, id, (uint32_t)width, (uint32_t)height);
}

BudoTexture *budo_texture_2d_create_rgba8(BudoHost *host, uint32_t width,
                                          uint32_t height,
                                          const void *pixels,
                                          size_t byte_count)
{
    BudoTexture *texture;
    size_t required;
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!pixels || !rgba8_size(width, height, &required) ||
        byte_count != required)
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "RGBA8 texture dimensions or pixel byte count is invalid");
        return NULL;
    }
    id = window_gl_create_texture_2d(host->graphics_window, (int)width,
                                     (int)height, WINDOW_GL_TEX_RGBA8, pixels);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_INTERNAL_ERROR,
                      "Could not create RGBA8 texture");
        return NULL;
    }
    texture = wrap_texture(host, id, width, height);
    return texture;
}

BudoTexture *budo_texture_2d_create_from_file(BudoHost *host,
                                              const char *path)
{
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!valid_name(path))
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "Encoded image asset path is required");
        return NULL;
    }
    id = window_gl_create_texture_2d_from_file(host->graphics_window, path);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_APPLICATION_ERROR,
                      "Could not load or decode image asset");
        return NULL;
    }
    return wrap_loaded_texture(host, id);
}

BudoTexture *budo_texture_2d_create_from_encoded_data(
    BudoHost *host, const void *data, size_t byte_count)
{
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!data || byte_count == 0 || byte_count > INT_MAX)
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "Encoded image data is empty or too large");
        return NULL;
    }
    id = window_gl_create_texture_2d_from_buffer(
        host->graphics_window, (const uint8_t *)data, byte_count);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_APPLICATION_ERROR,
                      "Could not decode encoded image data");
        return NULL;
    }
    return wrap_loaded_texture(host, id);
}

BudoStatus budo_texture_2d_get_size(BudoTexture *texture,
                                    uint32_t *out_width,
                                    uint32_t *out_height)
{
    BudoStatus status = validate_texture(texture);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!out_width || !out_height)
        return gpu_error(texture->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Texture width and height outputs are required");
    *out_width = texture->width;
    *out_height = texture->height;
    return BUDO_STATUS_OK;
}

BudoStatus budo_texture_2d_update_rgba8(BudoTexture *texture, uint32_t x,
                                        uint32_t y, uint32_t width,
                                        uint32_t height, const void *pixels,
                                        size_t byte_count)
{
    BudoStatus status = validate_texture(texture);
    size_t required;
    if (status != BUDO_STATUS_OK)
        return status;
    if (!pixels || !rgba8_size(width, height, &required) ||
        byte_count != required || x > texture->width || y > texture->height ||
        width > texture->width - x || height > texture->height - y)
        return gpu_error(texture->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "RGBA8 texture update is outside the texture or has an invalid byte count");
    if (!window_gl_update_texture_2d(texture->window, texture->id,
                                     (int)x, (int)y, (int)width, (int)height,
                                     pixels))
        return backend_error(texture->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not update RGBA8 texture");
    return BUDO_STATUS_OK;
}

BudoStatus budo_texture_destroy(BudoTexture *texture)
{
    BudoStatus status = validate_texture(texture);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!window_gl_destroy_texture(texture->window, texture->id))
        return backend_error(texture->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not destroy texture");
    texture->id = 0;
    free(texture);
    return BUDO_STATUS_OK;
}

BudoStatus budo_shader_program_bind_texture(
    BudoShaderProgram *program, const char *uniform_name,
    BudoTexture *texture, uint32_t texture_unit)
{
    BudoStatus status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_texture(texture);
    if (status != BUDO_STATUS_OK)
        return status;
    if (texture->host != program->host || !valid_name(uniform_name) ||
        texture_unit == 0 || texture_unit >= BUDO_GPU_MAX_TEXTURE_UNITS)
        return gpu_error(program->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Texture, sampler name, or texture unit is invalid");
    if (!window_gl_bind_texture_2d_immediate(program->window, program->id,
                                             uniform_name, texture->id,
                                             (int)texture_unit))
        return backend_error(program->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not bind texture to shader program");
    return BUDO_STATUS_OK;
}

static BudoStatus validate_render_target(BudoRenderTarget *target)
{
    BudoStatus status;
    if (!target || !target->host || target->id <= 0)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(target->host);
    if (status != BUDO_STATUS_OK)
        return status;
    if (target->window != target->host->graphics_window ||
        target->generation != target->host->graphics_generation)
        return gpu_error(target->host, BUDO_STATUS_INVALID_STATE,
                         "Render target belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

BudoRenderTarget *budo_render_target_create_rgba8(
    BudoHost *host, uint32_t width, uint32_t height)
{
    BudoRenderTarget *target;
    size_t ignored_size;
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    if (!rgba8_size(width, height, &ignored_size))
    {
        gpu_error(host, BUDO_STATUS_INVALID_ARGUMENT,
                  "Render target dimensions are invalid");
        return NULL;
    }
    id = window_gl_create_render_target(host->graphics_window,
                                        (int)width, (int)height, false);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_INTERNAL_ERROR,
                      "Could not create RGBA8 render target");
        return NULL;
    }
    target = calloc(1, sizeof(*target));
    if (!target)
    {
        window_gl_destroy_render_target(host->graphics_window, id);
        gpu_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                  "Could not allocate render target handle");
        return NULL;
    }
    target->host = host;
    target->window = host->graphics_window;
    target->id = id;
    target->width = width;
    target->height = height;
    target->generation = host->graphics_generation;
    return target;
}

BudoStatus budo_render_target_resize(BudoRenderTarget *target,
                                     uint32_t width, uint32_t height)
{
    BudoStatus status = validate_render_target(target);
    size_t ignored_size;
    if (status != BUDO_STATUS_OK)
        return status;
    if (!rgba8_size(width, height, &ignored_size))
        return gpu_error(target->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Render target dimensions are invalid");
    if (!window_gl_resize_render_target(target->window, target->id,
                                        (int)width, (int)height))
        return backend_error(target->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not resize render target");
    target->width = width;
    target->height = height;
    return BUDO_STATUS_OK;
}

BudoStatus budo_render_target_get_size(BudoRenderTarget *target,
                                       uint32_t *out_width,
                                       uint32_t *out_height)
{
    BudoStatus status = validate_render_target(target);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!out_width || !out_height)
        return gpu_error(target->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Render target width and height outputs are required");
    *out_width = target->width;
    *out_height = target->height;
    return BUDO_STATUS_OK;
}

BudoStatus budo_render_target_destroy(BudoRenderTarget *target)
{
    BudoStatus status = validate_render_target(target);
    if (status != BUDO_STATUS_OK)
        return status;
    if (!window_gl_destroy_render_target(target->window, target->id))
        return backend_error(target->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not destroy render target");
    target->id = 0;
    free(target);
    return BUDO_STATUS_OK;
}

BudoStatus budo_canvas_draw_fullscreen_pass(
    BudoCanvas *canvas, BudoShaderProgram *program,
    BudoRenderTarget *source, BudoRenderTarget *destination)
{
    BudoStatus status;
    if (!canvas || !canvas->host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(canvas->host);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (source)
    {
        status = validate_render_target(source);
        if (status != BUDO_STATUS_OK)
            return status;
    }
    if (destination)
    {
        status = validate_render_target(destination);
        if (status != BUDO_STATUS_OK)
            return status;
    }
    if (canvas->implementation != canvas->host->canvas.implementation ||
        program->host != canvas->host ||
        (source && source->host != canvas->host) ||
        (destination && destination->host != canvas->host))
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Fullscreen pass resources belong to a different Budo host");
    if (source && source == destination)
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Fullscreen pass cannot sample from its destination");
    if (!window_gl_draw_fullscreen_pass_immediate(
            canvas->host->graphics_window, program->id,
            source ? source->id : 0, destination ? destination->id : 0))
        return backend_error(canvas->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not draw fullscreen pass");
    return BUDO_STATUS_OK;
}

BudoStatus budo_canvas_draw_region_pass(
    BudoCanvas *canvas, BudoShaderProgram *program,
    BudoRenderTarget *source, BudoRenderTarget *destination,
    int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    BudoStatus status;
    if (!canvas || !canvas->host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(canvas->host);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    if (source)
    {
        status = validate_render_target(source);
        if (status != BUDO_STATUS_OK)
            return status;
    }
    if (destination)
    {
        status = validate_render_target(destination);
        if (status != BUDO_STATUS_OK)
            return status;
    }
    if (canvas->implementation != canvas->host->canvas.implementation ||
        program->host != canvas->host ||
        (source && source->host != canvas->host) ||
        (destination && destination->host != canvas->host))
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Region pass resources belong to a different Budo host");
    if (source && source == destination)
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Region pass cannot sample from its destination");
    if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX)
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Region pass dimensions are invalid");
    if (!window_gl_draw_region_pass_immediate(
            canvas->host->graphics_window, program->id,
            source ? source->id : 0, destination ? destination->id : 0,
            (int)x, (int)y, (int)width, (int)height))
        return backend_error(canvas->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not draw region pass");
    return BUDO_STATUS_OK;
}

static BudoStatus validate_mesh(BudoMesh *mesh)
{
    BudoStatus status;
    if (!mesh || !mesh->host || mesh->id <= 0)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(mesh->host);
    if (status != BUDO_STATUS_OK)
        return status;
    if (mesh->window != mesh->host->graphics_window ||
        mesh->generation != mesh->host->graphics_generation)
        return gpu_error(mesh->host, BUDO_STATUS_INVALID_STATE,
                         "Mesh belongs to an inactive graphics surface");
    return BUDO_STATUS_OK;
}

BudoMesh *budo_mesh_create(BudoHost *host)
{
    BudoMesh *mesh;
    int id;
    if (validate_gpu_callback(host) != BUDO_STATUS_OK)
        return NULL;
    id = window_gl_create_vertex_layout(host->graphics_window);
    if (id <= 0)
    {
        backend_error(host, BUDO_STATUS_INTERNAL_ERROR,
                      "Could not create mesh layout");
        return NULL;
    }
    mesh = calloc(1, sizeof(*mesh));
    if (!mesh)
    {
        window_gl_destroy_vertex_layout(host->graphics_window, id);
        gpu_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                  "Could not allocate mesh handle");
        return NULL;
    }
    mesh->host = host;
    mesh->window = host->graphics_window;
    mesh->id = id;
    mesh->generation = host->graphics_generation;
    return mesh;
}

BudoStatus budo_mesh_set_vertex_buffer(
    BudoMesh *mesh, uint32_t location, BudoGpuBuffer *buffer,
    uint32_t component_count, BudoVertexAttributeType type,
    uint32_t stride, uint32_t offset)
{
    BudoStatus status = validate_mesh(mesh);
    BudoMeshAttributeBinding *binding = NULL;
    size_t i;
    WindowGLAttrType backend_type;
    bool normalized;
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_buffer(buffer);
    if (status != BUDO_STATUS_OK)
        return status;
    if (buffer->host != mesh->host || buffer->target != BUDO_GPU_BUFFER_VERTEX ||
        location > 31u || component_count < 1u || component_count > 4u ||
        stride > INT_MAX || offset > INT_MAX || offset >= buffer->size ||
        (type != BUDO_VERTEX_ATTRIBUTE_FLOAT32 &&
         type != BUDO_VERTEX_ATTRIBUTE_UNORM8))
        return gpu_error(mesh->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Mesh vertex attribute is invalid");
    backend_type = type == BUDO_VERTEX_ATTRIBUTE_FLOAT32
                       ? WINDOW_GL_ATTR_FLOAT
                       : WINDOW_GL_ATTR_UBYTE;
    normalized = type == BUDO_VERTEX_ATTRIBUTE_UNORM8;
    for (i = 0; i < BUDO_GPU_MAX_ATTRIBUTES; ++i)
    {
        if (mesh->attributes[i].active &&
            mesh->attributes[i].location == location)
        {
            binding = &mesh->attributes[i];
            break;
        }
        if (!binding && !mesh->attributes[i].active)
            binding = &mesh->attributes[i];
    }
    if (!binding)
        return gpu_error(mesh->host, BUDO_STATUS_INVALID_STATE,
                         "Mesh has reached its vertex attribute limit");
    if (!window_gl_set_attribute(mesh->window, mesh->id, (int)location,
                                 buffer->id, (int)component_count,
                                 backend_type, normalized, (int)stride,
                                 (int)offset, 0))
        return backend_error(mesh->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not configure mesh vertex attribute");
    if (binding->active && binding->buffer)
        binding->buffer->references--;
    binding->active = true;
    binding->location = location;
    binding->buffer = buffer;
    buffer->references++;
    return BUDO_STATUS_OK;
}

BudoStatus budo_mesh_set_index_buffer(BudoMesh *mesh,
                                      BudoGpuBuffer *buffer,
                                      BudoIndexType type)
{
    BudoStatus status = validate_mesh(mesh);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_buffer(buffer);
    if (status != BUDO_STATUS_OK)
        return status;
    if (buffer->host != mesh->host || buffer->target != BUDO_GPU_BUFFER_INDEX ||
        type != BUDO_INDEX_UINT16)
        return gpu_error(mesh->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Mesh index buffer or index type is invalid");
    if (!window_gl_set_index_buffer(mesh->window, mesh->id, buffer->id,
                                    WINDOW_GL_INDEX_U16))
        return backend_error(mesh->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not configure mesh index buffer");
    if (mesh->index_buffer)
        mesh->index_buffer->references--;
    mesh->index_buffer = buffer;
    buffer->references++;
    return BUDO_STATUS_OK;
}

BudoStatus budo_mesh_destroy(BudoMesh *mesh)
{
    BudoStatus status = validate_mesh(mesh);
    size_t i;
    if (status != BUDO_STATUS_OK)
        return status;
    if (!window_gl_destroy_vertex_layout(mesh->window, mesh->id))
        return backend_error(mesh->host, BUDO_STATUS_INTERNAL_ERROR,
                             "Could not destroy mesh layout");
    for (i = 0; i < BUDO_GPU_MAX_ATTRIBUTES; ++i)
    {
        if (mesh->attributes[i].active && mesh->attributes[i].buffer)
            mesh->attributes[i].buffer->references--;
    }
    if (mesh->index_buffer)
        mesh->index_buffer->references--;
    mesh->id = 0;
    free(mesh);
    return BUDO_STATUS_OK;
}

BudoStatus budo_canvas_draw_mesh(BudoCanvas *canvas,
                                 BudoShaderProgram *program,
                                 BudoMesh *mesh,
                                 const BudoMeshDrawInfo *draw_info)
{
    BudoStatus status;
    WindowGLDrawState state;
    if (!canvas || !canvas->host)
        return BUDO_STATUS_INVALID_ARGUMENT;
    status = validate_gpu_callback(canvas->host);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_program(program);
    if (status != BUDO_STATUS_OK)
        return status;
    status = validate_mesh(mesh);
    if (status != BUDO_STATUS_OK)
        return status;
    if (canvas->implementation != canvas->host->canvas.implementation ||
        program->host != canvas->host || mesh->host != canvas->host)
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Mesh resources belong to a different Budo host");
    if (!draw_info || draw_info->struct_size < sizeof(*draw_info) ||
        draw_info->count == 0 || draw_info->first > INT_MAX ||
        draw_info->count > INT_MAX ||
        draw_info->primitive < BUDO_PRIMITIVE_TRIANGLES ||
        draw_info->primitive > BUDO_PRIMITIVE_POINTS ||
        draw_info->cull < BUDO_CULL_NONE || draw_info->cull > BUDO_CULL_FRONT ||
        draw_info->blend < BUDO_BLEND_NONE ||
        draw_info->blend > BUDO_BLEND_PREMULTIPLIED_ALPHA)
        return gpu_error(canvas->host, BUDO_STATUS_INVALID_ARGUMENT,
                         "Mesh draw information is invalid");
    state.depth_test = draw_info->depth_test;
    state.depth_write = draw_info->depth_write;
    state.cull = (WindowGLCullMode)draw_info->cull;
    state.blend = (WindowGLBlendMode)draw_info->blend;
    if (!window_gl_draw_mesh_immediate(canvas->host->graphics_window,
                                       program->id, mesh->id,
                                       (WindowGLPrimitive)draw_info->primitive,
                                       (int)draw_info->first,
                                       (int)draw_info->count, -1, &state, 1))
        return backend_error(canvas->host, BUDO_STATUS_APPLICATION_ERROR,
                             "Could not draw mesh");
    return BUDO_STATUS_OK;
}