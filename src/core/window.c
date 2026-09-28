#include "window.h"
#include "core/path_util.h"
#include "core/gl_shader_dialect.h"
#include "graphics/gl_shader_pipeline.h"
#include "graphics/gl_render_target.h"
#include "graphics/image_loader.h"
#include "graphics/gpu_resource_state.h"

#define GL_GLEXT_PROTOTYPES 1

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>

#include "core/gl_present_state.inc"
#include "graphics/gl_state_guard_gl.inc"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GL_PROGRAMS 128
#define MAX_GL_UNIFORM_CACHE 64
#define MAX_GL_RENDER_TARGETS 32
#define MAX_GL_BUFFERS 256
#define MAX_GL_TEXTURES 128
#define MAX_GL_VERTEX_LAYOUTS 64
#define MAX_GL_LAYOUT_ATTRS 8

typedef struct
{
    bool in_use;
    char name[64];
    GLint location;
} GLUniformCacheEntry;

typedef enum
{
    GL_UNIFORM_1I = 0,
    GL_UNIFORM_1F,
    GL_UNIFORM_2F,
    GL_UNIFORM_3F,
    GL_UNIFORM_4F,
    GL_UNIFORM_1IV,
    GL_UNIFORM_1FV,
    GL_UNIFORM_2FV,
    GL_UNIFORM_3FV,
    GL_UNIFORM_4FV,
    GL_UNIFORM_MAT3FV,
    GL_UNIFORM_MAT4FV
} GLUniformKind;

typedef struct
{
    bool in_use;
    GLuint program;
    GLint sampler_loc;
    GLint resolution_loc;
    GLint time_loc;
    GLint position_loc;
    GLint tex_coord_loc;
    GLUniformCacheEntry uniform_cache[MAX_GL_UNIFORM_CACHE];
} GLProgramSlot;

typedef GpuRenderTargetSlot GLRenderTarget;

typedef struct
{
    bool is_screen;
    int width;
    int height;
    bool has_depth;
} GLBoundRenderTarget;

typedef GpuBufferSlot GLBufferSlot;
typedef GpuTextureSlot GLTextureSlot;

typedef struct
{
    bool active;
    int location;
    int buffer_id;
    int size; 
    GLenum gl_type;
    bool normalized;
    int stride;
    int offset;
    int divisor;
} GLVertexAttribDesc;

typedef struct
{
    bool in_use;
    GLuint vao;
    GLVertexAttribDesc attrs[MAX_GL_LAYOUT_ATTRS];
    int attr_count;
    int index_buffer_id;  
    GLenum index_gl_type; 
    bool needs_rebind;
} GLVertexLayout;

static bool immediate_gl_bind_sampler_texture(Window *window, int program_id,
                                              const char *uniform_name,
                                              GLuint texture_id, GLenum texture_target,
                                              int texture_unit);

struct Window
{
    SDL_Window *sdl_window;
    SDL_GLContext gl_context;
    CanvasTexture *default_canvas;
    SkiaCanvas *skia_gpu_canvas; 

    int width;
    int height;
    float dpi_scale;
    bool should_close;

    uint64_t start_time;
    uint64_t last_frame_time;
    double delta_time;
    double frame_time;

    GLuint canvas_texture;
    GLuint canvas_fbo;
    GLuint blit_program;
    GLuint fullscreen_vao;
    GLuint fullscreen_vbo;

    GLProgramSlot programs[MAX_GL_PROGRAMS];

    GLRenderTarget render_targets[MAX_GL_RENDER_TARGETS];

    GLBufferSlot buffers[MAX_GL_BUFFERS];
    GLTextureSlot textures[MAX_GL_TEXTURES];
    GLVertexLayout vertex_layouts[MAX_GL_VERTEX_LAYOUTS];
    bool screen_rendered_this_frame;

    char project_dir[PATH_MAX];
    char error_msg[512];
};

static const char *k_blit_vertex_shader =
    "#version 150\n"
    "in vec2 a_position;\n"
    "in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "  v_texCoord = a_texCoord;\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static const char *k_blit_fragment_shader =
    "#version 150\n"
    "uniform sampler2D u_canvas;\n"
    "in vec2 v_texCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  fragColor = texture(u_canvas, v_texCoord);\n"
    "}\n";

static void set_error(Window *window, const char *message)
{
    if (!window || !message)
        return;

    strncpy(window->error_msg, message, sizeof(window->error_msg) - 1);
    window->error_msg[sizeof(window->error_msg) - 1] = '\0';
}

static void set_errorf(Window *window, const char *prefix, const char *detail)
{
    if (!window)
        return;

    if (!detail)
        detail = "unknown error";

    snprintf(window->error_msg, sizeof(window->error_msg), "%s: %s", prefix, detail);
}

static bool resolve_project_path(Window *window, const char *relative_path, char *resolved_path, size_t resolved_size)
{
    int written;

    if (!window || !relative_path || !resolved_path)
        return false;

    if (!path_is_safe_relative(relative_path))
    {
        set_error(window, "Shader paths must be relative to the application directory and cannot contain '.' or '..' segments");
        return false;
    }

    written = snprintf(resolved_path, resolved_size, "%s/%s", window->project_dir, relative_path);
    if (written < 0 || (size_t)written >= resolved_size)
    {
        set_error(window, "Resolved shader path is too long");
        return false;
    }

    return true;
}

static char *read_text_file(Window *window, const char *path)
{
    FILE *file;
    long size;
    char *buffer;

    file = fopen(path, "rb");
    if (!file)
    {
        set_errorf(window, "Failed to open shader file", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        set_error(window, "Failed to read shader file size");
        return NULL;
    }

    size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        set_error(window, "Failed to read shader file size");
        return NULL;
    }

    rewind(file);

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer)
    {
        fclose(file);
        set_error(window, "Failed to allocate shader source buffer");
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(buffer);
        set_error(window, "Failed to read shader file contents");
        return NULL;
    }

    fclose(file);
    buffer[size] = '\0';
    return buffer;
}

static uint8_t *read_binary_file(Window *window, const char *path, size_t *out_size)
{
    FILE *file;
    long size;
    uint8_t *buffer;

    if (out_size)
        *out_size = 0;

    file = fopen(path, "rb");
    if (!file)
    {
        set_errorf(window, "Failed to open resource file", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        set_error(window, "Failed to read resource file size");
        return NULL;
    }

    size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        set_error(window, "Failed to read resource file size");
        return NULL;
    }

    rewind(file);

    buffer = (uint8_t *)malloc((size_t)size);
    if (!buffer)
    {
        fclose(file);
        set_error(window, "Failed to allocate resource buffer");
        return NULL;
    }

    if (size > 0 && fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        free(buffer);
        fclose(file);
        set_error(window, "Failed to read resource file");
        return NULL;
    }

    fclose(file);
    if (out_size)
        *out_size = (size_t)size;
    return buffer;
}

static GLuint create_program_from_source(Window *window, const char *vertex_source, const char *fragment_source, bool require_app_dialect)
{
    const GlShaderPipelineApi api = {
        (uint32_t(*)(uint32_t))glCreateShader,
        (void (*)(uint32_t, int, const char *const *, const int *))glShaderSource,
        (void (*)(uint32_t))glCompileShader,
        (void (*)(uint32_t, uint32_t, int *))glGetShaderiv,
        (void (*)(uint32_t, int, int *, char *))glGetShaderInfoLog,
        (void (*)(uint32_t))glDeleteShader,
        (uint32_t(*)(void))glCreateProgram,
        (void (*)(uint32_t, uint32_t))glAttachShader,
        (void (*)(uint32_t, uint32_t, const char *))glBindAttribLocation,
        (void (*)(uint32_t))glLinkProgram,
        (void (*)(uint32_t, uint32_t, int *))glGetProgramiv,
        (void (*)(uint32_t, int, int *, char *))glGetProgramInfoLog,
        (void (*)(uint32_t))glDeleteProgram};
    const GlShaderPipelineConstants constants = {
        GL_VERTEX_SHADER, GL_FRAGMENT_SHADER,
        GL_COMPILE_STATUS, GL_LINK_STATUS, GL_TRUE};
    return (GLuint)gl_shader_pipeline_create(
        &api, &constants, BUDO_GLSL_TARGET_DESKTOP_GL150,
        vertex_source, fragment_source, require_app_dialect,
        window->error_msg, sizeof(window->error_msg));
}

static GLint get_cached_uniform_location(GLProgramSlot *slot, const char *name)
{
    int i;
    int empty_index = -1;

    if (!slot || !name || !name[0])
        return -1;

    for (i = 0; i < MAX_GL_UNIFORM_CACHE; i++)
    {
        if (slot->uniform_cache[i].in_use)
        {
            if (strcmp(slot->uniform_cache[i].name, name) == 0)
                return slot->uniform_cache[i].location;
        }
        else if (empty_index < 0)
        {
            empty_index = i;
        }
    }

    if (empty_index >= 0)
    {
        GLint location = glGetUniformLocation(slot->program, name);
        slot->uniform_cache[empty_index].in_use = true;
        strncpy(slot->uniform_cache[empty_index].name, name, sizeof(slot->uniform_cache[empty_index].name) - 1);
        slot->uniform_cache[empty_index].name[sizeof(slot->uniform_cache[empty_index].name) - 1] = '\0';
        slot->uniform_cache[empty_index].location = location;
        return location;
    }

    return glGetUniformLocation(slot->program, name);
}

static void flush_skia_canvas_transition(void *opaque)
{
    skia_canvas_flush((SkiaCanvas *)opaque);
}

static void flush_skia_canvas_preserving_gl_state(SkiaCanvas *canvas)
{
    if (canvas)
        budo_gl_state_guard_run(flush_skia_canvas_transition, canvas);
}

static void flush_skia_canvas_discarding_gl_state(SkiaCanvas *canvas)
{
    if (canvas)
    skia_canvas_flush(canvas);
}

static void sync_window_default_canvas_aliases(Window *window)
{
    if (!window || !window->default_canvas)
    {
        if (window)
        {
            window->skia_gpu_canvas = NULL;
            window->canvas_texture = 0;
            window->canvas_fbo = 0;
        }
        return;
    }

    window->skia_gpu_canvas = window_canvas_texture_get_canvas(window->default_canvas);
    window->canvas_texture = (GLuint)window_canvas_texture_get_gl_texture(window->default_canvas);
    window->canvas_fbo = (GLuint)window_canvas_texture_get_gl_framebuffer(window->default_canvas);
}

static GLProgramSlot *get_program_slot(Window *window, int program_id)
{
    if (!window || program_id <= 0 || program_id > MAX_GL_PROGRAMS)
        return NULL;

    if (!window->programs[program_id - 1].in_use)
        return NULL;

    return &window->programs[program_id - 1];
}

static void apply_uniform_value(GLint location, GLUniformKind kind, int count, const void *data)
{
    const GLint *iv;
    const GLfloat *fv;

    switch (kind)
    {
    case GL_UNIFORM_1I:
        iv = (const GLint *)data;
        glUniform1i(location, iv[0]);
        break;
    case GL_UNIFORM_1F:
        fv = (const GLfloat *)data;
        glUniform1f(location, fv[0]);
        break;
    case GL_UNIFORM_2F:
        fv = (const GLfloat *)data;
        glUniform2f(location, fv[0], fv[1]);
        break;
    case GL_UNIFORM_3F:
        fv = (const GLfloat *)data;
        glUniform3f(location, fv[0], fv[1], fv[2]);
        break;
    case GL_UNIFORM_4F:
        fv = (const GLfloat *)data;
        glUniform4f(location, fv[0], fv[1], fv[2], fv[3]);
        break;
    case GL_UNIFORM_1IV:
        glUniform1iv(location, count, (const GLint *)data);
        break;
    case GL_UNIFORM_1FV:
        glUniform1fv(location, count, (const GLfloat *)data);
        break;
    case GL_UNIFORM_2FV:
        glUniform2fv(location, count, (const GLfloat *)data);
        break;
    case GL_UNIFORM_3FV:
        glUniform3fv(location, count, (const GLfloat *)data);
        break;
    case GL_UNIFORM_4FV:
        glUniform4fv(location, count, (const GLfloat *)data);
        break;
    case GL_UNIFORM_MAT3FV:
        glUniformMatrix3fv(location, count, GL_FALSE, (const GLfloat *)data);
        break;
    case GL_UNIFORM_MAT4FV:
        glUniformMatrix4fv(location, count, GL_FALSE, (const GLfloat *)data);
        break;
    }
}

static bool apply_program_uniform_immediate(Window *window, int program_id, const char *name,
                                            GLUniformKind kind, int count,
                                            const void *data)
{
    GLProgramSlot *slot = get_program_slot(window, program_id);
    GLint location;
    GLint previous_program = 0;

    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!name || !name[0])
    {
        set_error(window, "Uniform name cannot be empty");
        return false;
    }

    location = get_cached_uniform_location(slot, name);
    if (location < 0)
    {
        snprintf(window->error_msg, sizeof(window->error_msg), "Uniform not found: %s", name);
        return false;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    if ((GLuint)previous_program != slot->program)
        glUseProgram(slot->program);
    apply_uniform_value(location, kind, count, data);
    if ((GLuint)previous_program != slot->program)
        glUseProgram((GLuint)previous_program);

    return true;
}

static bool set_program_uniform(Window *window, int program_id, const char *name,
                                GLUniformKind kind, int count,
                                const void *data, size_t bytes)
{
    (void)bytes;
    return apply_program_uniform_immediate(window, program_id, name, kind, count, data);
}

static GLBufferSlot *get_buffer_slot(Window *window, int buffer_id);
static GLTextureSlot *get_texture_slot(Window *window, int texture_id);

static bool upload_buffer_sub_data_immediate(Window *window, GLBufferSlot *buffer,
                                             size_t offset, const void *data, size_t size)
{
    if (!buffer || buffer->target == 0)
    {
        set_error(window, "Buffer has no data uploaded yet");
        return false;
    }

    glBindBuffer(buffer->target, buffer->id);
    glBufferSubData(buffer->target, (GLintptr)offset, (GLsizeiptr)size, data);
    glBindBuffer(buffer->target, 0);
    return true;
}

static void immediate_gl_use_program(GLuint program)
{
    glUseProgram(program);
}

static void immediate_gl_apply_builtin_uniforms(Window *window, GLuint program, GLProgramSlot *slot,
                                                GLfloat resolution_width, GLfloat resolution_height,
                                                int canvas_texture_unit, bool apply_canvas_sampler)
{
    GLint sampler_loc;
    GLint resolution_loc;
    GLint time_loc;

    if (apply_canvas_sampler)
    {
        sampler_loc = slot ? slot->sampler_loc : glGetUniformLocation(program, "u_canvas");
        if (sampler_loc >= 0)
            glUniform1i(sampler_loc, canvas_texture_unit);
    }

    resolution_loc = slot ? slot->resolution_loc : glGetUniformLocation(program, "u_resolution");
    if (resolution_loc >= 0)
        glUniform2f(resolution_loc, resolution_width, resolution_height);

    time_loc = slot ? slot->time_loc : glGetUniformLocation(program, "u_time");
    if (time_loc >= 0)
        glUniform1f(time_loc, (GLfloat)window->frame_time);
}

static void immediate_gl_cleanup_texture_units(int first_unit, int unit_count)
{
    int i;

    for (i = 0; i < unit_count; i++)
    {
        glActiveTexture(GL_TEXTURE0 + (GLenum)(first_unit + i));
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

static void immediate_gl_apply_texture_2d_sampling(void)
{
    BUDO_GL_APPLY_TEXTURE_2D_LINEAR_CLAMP();
}

static void immediate_gl_draw_fullscreen_geometry(Window *window, GLuint program, GLProgramSlot *slot)
{
    GLint position_loc;
    GLint tex_coord_loc;

    glBindVertexArray(window->fullscreen_vao);
    glBindBuffer(GL_ARRAY_BUFFER, window->fullscreen_vbo);

    position_loc = slot ? slot->position_loc : glGetAttribLocation(program, "a_position");
    if (position_loc >= 0)
    {
        glEnableVertexAttribArray((GLuint)position_loc);
        glVertexAttribPointer((GLuint)position_loc, 2, GL_FLOAT, GL_FALSE,
                              (GLsizei)(4 * sizeof(GLfloat)), (const void *)0);
    }

    tex_coord_loc = slot ? slot->tex_coord_loc : glGetAttribLocation(program, "a_texCoord");
    if (tex_coord_loc >= 0)
    {
        glEnableVertexAttribArray((GLuint)tex_coord_loc);
        glVertexAttribPointer((GLuint)tex_coord_loc, 2, GL_FLOAT, GL_FALSE,
                              (GLsizei)(4 * sizeof(GLfloat)), (const void *)(intptr_t)(2 * sizeof(GLfloat)));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (position_loc >= 0)
        glDisableVertexAttribArray((GLuint)position_loc);
    if (tex_coord_loc >= 0)
        glDisableVertexAttribArray((GLuint)tex_coord_loc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void immediate_gl_restore_present_baseline(Window *window)
{
    BUDO_GL_PREPARE_SCREEN_PRESENT(window->width, window->height);
    glBindVertexArray(window->fullscreen_vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static GLProgramSlot *find_program_slot_by_gl_program(Window *window, GLuint program)
{
    int i;

    if (!window || program == 0)
        return NULL;

    for (i = 0; i < MAX_GL_PROGRAMS; i++)
    {
        if (window->programs[i].in_use && window->programs[i].program == program)
            return &window->programs[i];
    }
    return NULL;
}

static bool immediate_gl_prepare_fullscreen_draw(Window *window, GLuint program, GLProgramSlot *slot,
                                                 GLuint source_texture, bool flush_default_canvas)
{
    GLuint texture;
    GLint viewport[4];

    if (!window || program == 0)
        return false;

    texture = source_texture != 0 ? source_texture : window->canvas_texture;
    if (texture == 0)
        return false;

    if (flush_default_canvas && source_texture == 0 && window->skia_gpu_canvas)
        flush_skia_canvas_preserving_gl_state(window->skia_gpu_canvas);

    immediate_gl_use_program(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    immediate_gl_apply_texture_2d_sampling();
    glGetIntegerv(GL_VIEWPORT, viewport);
    immediate_gl_apply_builtin_uniforms(window, program, slot,
                                        (GLfloat)viewport[2], (GLfloat)viewport[3],
                                        0, true);
    return true;
}

static bool immediate_gl_draw_fullscreen_now(Window *window, GLuint program, GLProgramSlot *slot,
                                             GLuint source_texture, bool flush_default_canvas)
{
    GLint framebuffer = 0;

    if (!immediate_gl_prepare_fullscreen_draw(window, program, slot, source_texture, flush_default_canvas))
        return false;

    immediate_gl_draw_fullscreen_geometry(window, program, slot);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    if (framebuffer == 0)
        window->screen_rendered_this_frame = true;
    return true;
}

static void draw_fullscreen(Window *window, GLuint program, GLuint source_texture)
{
    GLProgramSlot *slot = find_program_slot_by_gl_program(window, program);

    if (!immediate_gl_prepare_fullscreen_draw(window, program, slot, source_texture, false))
        return;

    immediate_gl_draw_fullscreen_geometry(window, program, slot);
    immediate_gl_use_program(0);
}

Window *window_create(const WindowConfig *config)
{
    Window *window;
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

    if (!config)
        return NULL;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return NULL;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    window = (Window *)calloc(1, sizeof(Window));
    if (!window)
    {
        SDL_Quit();
        return NULL;
    }

    window->width = config->width > 0 ? config->width : 800;
    window->height = config->height > 0 ? config->height : 600;
    if (config->project_dir)
    {
        strncpy(window->project_dir, config->project_dir, sizeof(window->project_dir) - 1);
        window->project_dir[sizeof(window->project_dir) - 1] = '\0';
    }

    if (config->resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (config->fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    window->sdl_window = SDL_CreateWindow(
        config->title ? config->title : "Budo",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window->width,
        window->height,
        flags);
    if (!window->sdl_window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        free(window);
        SDL_Quit();
        return NULL;
    }

    window->gl_context = SDL_GL_CreateContext(window->sdl_window);
    if (!window->gl_context)
    {
        SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
        SDL_DestroyWindow(window->sdl_window);
        free(window);
        SDL_Quit();
        return NULL;
    }

    SDL_GL_MakeCurrent(window->sdl_window, window->gl_context);
    SDL_GL_SetSwapInterval(config->vsync ? 1 : 0);

    {
        GLint major = 0;
        GLint minor = 0;
        GLint max_texture_units = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        if (major < 3 || (major == 3 && minor < 2))
        {
            SDL_Log("Budo requires OpenGL 3.2 core or newer, got %d.%d", major, minor);
            SDL_GL_DeleteContext(window->gl_context);
            SDL_DestroyWindow(window->sdl_window);
            free(window);
            SDL_Quit();
            return NULL;
        }
        if (max_texture_units < 8)
        {
            SDL_Log("Budo requires at least 8 fragment texture units, got %d", max_texture_units);
            SDL_GL_DeleteContext(window->gl_context);
            SDL_DestroyWindow(window->sdl_window);
            free(window);
            SDL_Quit();
            return NULL;
        }
    }

    {
        int logical_w = window->width;
        SDL_GL_GetDrawableSize(window->sdl_window, &window->width, &window->height);
        window->dpi_scale = (logical_w > 0) ? (float)window->width / (float)logical_w : 1.0f;
    }

    glViewport(0, 0, window->width, window->height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenVertexArrays(1, &window->fullscreen_vao);
    glBindVertexArray(window->fullscreen_vao);
    glGenBuffers(1, &window->fullscreen_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, window->fullscreen_vbo);
    {
        static const GLfloat fullscreen_vertices[] = {
            -1.0f,
            -1.0f,
            0.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            1.0f,
            0.0f,
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreen_vertices), fullscreen_vertices, GL_STATIC_DRAW);
    }

    window->blit_program = create_program_from_source(window, k_blit_vertex_shader, k_blit_fragment_shader, false);
    if (window->blit_program == 0)
    {
        SDL_Log("Failed to create blit program: %s", window->error_msg);
        SDL_GL_DeleteContext(window->gl_context);
        SDL_DestroyWindow(window->sdl_window);
        free(window);
        SDL_Quit();
        return NULL;
    }

    window->default_canvas = window_canvas_texture_create(window->width, window->height);
    sync_window_default_canvas_aliases(window);
    if (!window->default_canvas)
    {
        SDL_Log("Failed to create Skia GPU canvas");
        glDeleteProgram(window->blit_program);
        SDL_GL_DeleteContext(window->gl_context);
        SDL_DestroyWindow(window->sdl_window);
        free(window);
        SDL_Quit();
        return NULL;
    }

    window->start_time = SDL_GetPerformanceCounter();
    window->last_frame_time = window->start_time;

    return window;
}

void window_destroy(Window *window)
{
    int i;

    if (!window)
        return;

    for (i = 0; i < MAX_GL_PROGRAMS; i++)
    {
        if (window->programs[i].in_use)
            glDeleteProgram(window->programs[i].program);
    }

    for (i = 0; i < MAX_GL_RENDER_TARGETS; i++)
    {
        if (window->render_targets[i].in_use)
        {
            if (window->render_targets[i].depth_rbo != 0)
                glDeleteRenderbuffers(1, &window->render_targets[i].depth_rbo);
            if (window->render_targets[i].texture != 0)
                glDeleteTextures(1, &window->render_targets[i].texture);
            if (window->render_targets[i].fbo != 0)
                glDeleteFramebuffers(1, &window->render_targets[i].fbo);
        }
    }

    for (i = 0; i < MAX_GL_BUFFERS; i++)
    {
        if (window->buffers[i].in_use && window->buffers[i].id != 0)
            glDeleteBuffers(1, &window->buffers[i].id);
    }

    for (i = 0; i < MAX_GL_TEXTURES; i++)
    {
        if (window->textures[i].in_use && window->textures[i].id != 0)
            glDeleteTextures(1, &window->textures[i].id);
    }

    if (window->blit_program != 0)
        glDeleteProgram(window->blit_program);
    for (int i = 0; i < MAX_GL_VERTEX_LAYOUTS; i++)
    {
        if (window->vertex_layouts[i].in_use && window->vertex_layouts[i].vao != 0)
            glDeleteVertexArrays(1, &window->vertex_layouts[i].vao);
    }
    if (window->fullscreen_vbo != 0)
        glDeleteBuffers(1, &window->fullscreen_vbo);
    if (window->fullscreen_vao != 0)
        glDeleteVertexArrays(1, &window->fullscreen_vao);
    if (window->default_canvas)
        window_canvas_texture_destroy(window->default_canvas);

    if (window->gl_context)
        SDL_GL_DeleteContext(window->gl_context);
    if (window->sdl_window)
        SDL_DestroyWindow(window->sdl_window);

    free(window);
    SDL_Quit();
}

void window_reset_app_resources(Window *window)
{
    int i;

    if (!window)
        return;

    for (i = 0; i < MAX_GL_PROGRAMS; i++)
    {
        if (window->programs[i].in_use)
        {
            glDeleteProgram(window->programs[i].program);
            memset(&window->programs[i], 0, sizeof(window->programs[i]));
        }
    }

    for (i = 0; i < MAX_GL_RENDER_TARGETS; i++)
    {
        if (window->render_targets[i].in_use)
        {
            if (window->render_targets[i].depth_rbo != 0)
                glDeleteRenderbuffers(1, &window->render_targets[i].depth_rbo);
            if (window->render_targets[i].texture != 0)
                glDeleteTextures(1, &window->render_targets[i].texture);
            if (window->render_targets[i].fbo != 0)
                glDeleteFramebuffers(1, &window->render_targets[i].fbo);
            memset(&window->render_targets[i], 0, sizeof(window->render_targets[i]));
        }
    }

    for (i = 0; i < MAX_GL_BUFFERS; i++)
    {
        if (window->buffers[i].in_use && window->buffers[i].id != 0)
            glDeleteBuffers(1, &window->buffers[i].id);
        memset(&window->buffers[i], 0, sizeof(window->buffers[i]));
    }

    for (i = 0; i < MAX_GL_TEXTURES; i++)
    {
        if (window->textures[i].in_use && window->textures[i].id != 0)
            glDeleteTextures(1, &window->textures[i].id);
        memset(&window->textures[i], 0, sizeof(window->textures[i]));
    }

    for (i = 0; i < MAX_GL_VERTEX_LAYOUTS; i++)
    {
        if (window->vertex_layouts[i].in_use && window->vertex_layouts[i].vao != 0)
            glDeleteVertexArrays(1, &window->vertex_layouts[i].vao);
        memset(&window->vertex_layouts[i], 0, sizeof(window->vertex_layouts[i]));
    }

    window->error_msg[0] = '\0';
}

static InputMouseButton sdl_button_to_input(Uint8 button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return INPUT_MOUSE_LEFT;
    case SDL_BUTTON_MIDDLE:
        return INPUT_MOUSE_MIDDLE;
    case SDL_BUTTON_RIGHT:
        return INPUT_MOUSE_RIGHT;
    default:
        return INPUT_MOUSE_MAX;
    }
}

static void desktop_text_start(void *context, const char *text,
                               int selection_start, int selection_end,
                               bool multiline)
{
    (void)context;
    (void)text;
    (void)selection_start;
    (void)selection_end;
    (void)multiline;
    SDL_StartTextInput();
}

static void desktop_text_update(void *context, const char *text,
                                int selection_start, int selection_end,
                                int x, int y, int width, int height)
{
    SDL_Rect rect;
    Window *window = (Window *)context;
    (void)text;
    (void)selection_start;
    (void)selection_end;
    rect.x = window ? (int)(x / window->dpi_scale) : x;
    rect.y = window ? (int)(y / window->dpi_scale) : y;
    rect.w = window ? (int)(width / window->dpi_scale) : width;
    rect.h = window ? (int)(height / window->dpi_scale) : height;
    SDL_SetTextInputRect(&rect);
}

static void desktop_text_stop(void *context)
{
    (void)context;
    SDL_StopTextInput();
}

static const InputTextPlatformCallbacks desktop_text_callbacks = {
    .start = desktop_text_start,
    .update = desktop_text_update,
    .stop = desktop_text_stop,
};

bool window_poll_events(Window *window, InputState *input)
{
    SDL_Event event;
    uint64_t current_time;
    uint64_t frequency;

    if (!window)
        return false;

    current_time = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    window->delta_time = (double)(current_time - window->last_frame_time) / (double)frequency;
    window->last_frame_time = current_time;

    if (input)
    {
        if (input->text_platform.start != desktop_text_start)
            input_text_set_platform(input, &desktop_text_callbacks, window);
        input_begin_frame(input);
        input_update_timing(input, window->delta_time);
    }

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            window->should_close = true;
            break;

        case SDL_WINDOWEVENT:
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_CLOSE:
                window->should_close = true;
                break;
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            {
                int dw, dh;
                SDL_GL_GetDrawableSize(window->sdl_window, &dw, &dh);
                window_resize(window, dw, dh);
                break;
            }
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                if (input)
                    input_set_focus(input, true);
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                if (input)
                    input_set_focus(input, false);
                break;
            }
            break;

        case SDL_MOUSEMOTION:
            if (input)
                input_set_mouse_position(input,
                                         (int)(event.motion.x * window->dpi_scale),
                                         (int)(event.motion.y * window->dpi_scale));
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (input)
                input_set_mouse_button(input, sdl_button_to_input(event.button.button), true);
            break;

        case SDL_MOUSEBUTTONUP:
            if (input)
                input_set_mouse_button(input, sdl_button_to_input(event.button.button), false);
            break;

        case SDL_MOUSEWHEEL:
            if (input)
                input_set_mouse_wheel(input, event.wheel.x, event.wheel.y);
            break;

        case SDL_KEYDOWN:
            if (input && !event.key.repeat)
                input_set_key(input, event.key.keysym.scancode, true);
            if (event.key.keysym.sym == SDLK_ESCAPE)
                window->should_close = true;
            break;

        case SDL_KEYUP:
            if (input)
                input_set_key(input, event.key.keysym.scancode, false);
            break;

        case SDL_TEXTINPUT:
            if (input)
            {
                input_append_text(input, event.text.text);
                input_text_set_composition(input, "", 0, 0, false);
            }
            break;

        case SDL_TEXTEDITING:
            if (input)
                input_text_set_composition(input, event.edit.text,
                                           event.edit.start,
                                           event.edit.start + event.edit.length,
                                           event.edit.text[0] != '\0');
            break;

#if SDL_VERSION_ATLEAST(2, 0, 22)
        case SDL_TEXTEDITING_EXT:
            if (input)
                input_text_set_composition(input, event.editExt.text,
                                           event.editExt.start,
                                           event.editExt.start + event.editExt.length,
                                           event.editExt.text &&
                                               event.editExt.text[0] != '\0');
            SDL_free(event.editExt.text);
            break;
#endif
        }
    }

    if (input)
    {
        SDL_Keymod mod = SDL_GetModState();
        input_set_modifiers(
            input,
            (mod & KMOD_SHIFT) != 0,
            (mod & KMOD_CTRL) != 0,
            (mod & KMOD_ALT) != 0,
            (mod & KMOD_GUI) != 0);
    }

    return !window->should_close;
}

SkiaCanvas *window_get_canvas(Window *window)
{
    if (!window)
        return NULL;
    return window->skia_gpu_canvas;
}

void window_begin_frame(Window *window, double time_seconds)
{
    if (!window)
        return;

    window->frame_time = time_seconds;
    window->screen_rendered_this_frame = false;

    if (window->skia_gpu_canvas)
        skia_canvas_reset_gl_context(window->skia_gpu_canvas);
}

static GLRenderTarget *get_render_target_slot(Window *window, int target_id)
{
    return window ? (GLRenderTarget *)gpu_resource_slot_lookup(
                        window->render_targets, sizeof(window->render_targets[0]),
                        MAX_GL_RENDER_TARGETS, target_id)
                  : NULL;
}

static bool immediate_gl_bind_screen_target(Window *window, GLBoundRenderTarget *bound)
{
    if (!window)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (bound)
    {
        bound->is_screen = true;
        bound->width = window->width;
        bound->height = window->height;
        bound->has_depth = true;
    }
    return true;
}

static bool immediate_gl_bind_app_render_target(Window *window, int target_id, GLBoundRenderTarget *bound)
{
    GLRenderTarget *rt = get_render_target_slot(window, target_id);

    if (!rt)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);

    if (bound)
    {
        bound->is_screen = false;
        bound->width = rt->width;
        bound->height = rt->height;
        bound->has_depth = rt->has_depth;
    }
    return true;
}

static bool immediate_gl_bind_render_target(Window *window, int target_id, GLBoundRenderTarget *bound)
{
    if (target_id > 0)
        return immediate_gl_bind_app_render_target(window, target_id, bound);
    return immediate_gl_bind_screen_target(window, bound);
}

static bool immediate_gl_get_bound_render_target(Window *window, GLBoundRenderTarget *bound)
{
    GLint framebuffer = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    int i;

    if (!window || !bound)
        return false;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);

    if (framebuffer == 0)
    {
        bound->is_screen = true;
        bound->width = window->width;
        bound->height = window->height;
        bound->has_depth = true;
        return true;
    }

    for (i = 0; i < MAX_GL_RENDER_TARGETS; i++)
    {
        GLRenderTarget *rt = &window->render_targets[i];
        if (rt->in_use && rt->fbo == (GLuint)framebuffer)
        {
            bound->is_screen = false;
            bound->width = rt->width;
            bound->height = rt->height;
            bound->has_depth = rt->has_depth;
            return true;
        }
    }

    bound->is_screen = false;
    bound->width = viewport[2] > 0 ? viewport[2] : window->width;
    bound->height = viewport[3] > 0 ? viewport[3] : window->height;
    bound->has_depth = false;
    return true;
}

static void immediate_gl_apply_render_target_viewport(const GLBoundRenderTarget *bound,
                                                      int x, int y, int width, int height)
{
    (void)bound;
    glViewport(x, y, width, height);
}

static void immediate_gl_apply_depth_state(const GLBoundRenderTarget *bound,
                                           bool depth_test, bool depth_write,
                                           bool clear_depth)
{
    if (depth_test)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        if (clear_depth && bound && bound->has_depth)
            glClear(GL_DEPTH_BUFFER_BIT);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(depth_write ? GL_TRUE : GL_FALSE);
}

static void immediate_gl_clear_render_target_depth(const GLBoundRenderTarget *bound)
{
    if (bound && bound->has_depth)
        glClear(GL_DEPTH_BUFFER_BIT);
}

static bool immediate_gl_draw_region_now(Window *window, GLuint program, GLProgramSlot *slot,
                                         float rx, float ry, float rw, float rh,
                                         int target_id, bool bind_explicit_target,
                                         bool flush_default_canvas)
{
    static const GLfloat positions[] = {
        -1.0f,
        -1.0f,
        1.0f,
        -1.0f,
        -1.0f,
        1.0f,
        1.0f,
        1.0f,
    };
    static const GLfloat tex_coords[] = {
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    GLBoundRenderTarget target;
    GLint position_loc;
    GLint tex_coord_loc;
    GLint offset_loc;
    int vp_x, vp_y, vp_w, vp_h;

    if (!window || program == 0 || rw <= 0.0f || rh <= 0.0f)
        return false;

    if (flush_default_canvas && window->skia_gpu_canvas)
        flush_skia_canvas_preserving_gl_state(window->skia_gpu_canvas);

    if (bind_explicit_target)
    {
        if (!immediate_gl_bind_render_target(window, target_id, &target))
            return false;
    }
    else if (!immediate_gl_get_bound_render_target(window, &target))
        return false;

    if (target.is_screen && !window->screen_rendered_this_frame)
    {
        immediate_gl_restore_present_baseline(window);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, window->canvas_texture);
        immediate_gl_apply_texture_2d_sampling();
        draw_fullscreen(window, window->blit_program, window->canvas_texture);
    }

    if (target_id > 0)
    {
        
        vp_x = (int)rx;
        vp_y = target.height - (int)(ry + rh);
        vp_w = (int)rw;
        vp_h = (int)rh;
    }
    else
    {
        
        vp_x = (int)rx;
        vp_y = target.height - (int)(ry + rh);
        vp_w = (int)rw;
        vp_h = (int)rh;
    }

    immediate_gl_apply_render_target_viewport(&target, vp_x, vp_y, vp_w, vp_h);
    glBindVertexArray(window->fullscreen_vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vp_x, vp_y, vp_w, vp_h);

    if (!target.is_screen && target.has_depth)
    {
        glEnable(GL_DEPTH_TEST);
        immediate_gl_clear_render_target_depth(&target);
    }

    immediate_gl_use_program(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, window->canvas_texture);
    immediate_gl_apply_builtin_uniforms(window, program, slot, rw, rh, 0, true);

    offset_loc = glGetUniformLocation(program, "u_offset");
    if (offset_loc >= 0)
        glUniform2f(offset_loc, rx, ry);

    position_loc = slot ? slot->position_loc : glGetAttribLocation(program, "a_position");
    if (position_loc >= 0)
    {
        glEnableVertexAttribArray((GLuint)position_loc);
        glVertexAttribPointer((GLuint)position_loc, 2, GL_FLOAT, GL_FALSE, 0, positions);
    }

    tex_coord_loc = slot ? slot->tex_coord_loc : glGetAttribLocation(program, "a_texCoord");
    if (tex_coord_loc >= 0)
    {
        glEnableVertexAttribArray((GLuint)tex_coord_loc);
        glVertexAttribPointer((GLuint)tex_coord_loc, 2, GL_FLOAT, GL_FALSE, 0, tex_coords);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (position_loc >= 0)
        glDisableVertexAttribArray((GLuint)position_loc);
    if (tex_coord_loc >= 0)
        glDisableVertexAttribArray((GLuint)tex_coord_loc);

    immediate_gl_cleanup_texture_units(1, 7);

    immediate_gl_use_program(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    if (!target.is_screen && target.has_depth)
        glDisable(GL_DEPTH_TEST);

    if (target.is_screen)
        window->screen_rendered_this_frame = true;

    if (bind_explicit_target && !target.is_screen)
        immediate_gl_bind_screen_target(window, NULL);

    return true;
}

void window_present(Window *window)
{
    if (!window)
        return;

    if (window->screen_rendered_this_frame)
    {
        SDL_GL_SwapWindow(window->sdl_window);
        return;
    }

    if (window->skia_gpu_canvas)
        flush_skia_canvas_discarding_gl_state(window->skia_gpu_canvas);

    immediate_gl_restore_present_baseline(window);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, window->canvas_texture);

    immediate_gl_apply_texture_2d_sampling();

    draw_fullscreen(window, window->blit_program, window->canvas_texture);

    glBindTexture(GL_TEXTURE_2D, 0);
    SDL_GL_SwapWindow(window->sdl_window);
}

void window_get_size(Window *window, int *width, int *height)
{
    if (window)
    {
        if (width)
            *width = window->width;
        if (height)
            *height = window->height;
    }
}

float window_get_dpi_scale(Window *window)
{
    return window ? window->dpi_scale : 1.0f;
}

bool window_should_close(Window *window)
{
    return window ? window->should_close : true;
}

void window_set_title(Window *window, const char *title)
{
    if (window && window->sdl_window)
        SDL_SetWindowTitle(window->sdl_window, title);
}

double window_get_time(Window *window)
{
    uint64_t current;
    uint64_t frequency;

    if (!window)
        return 0.0;

    current = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    return (double)(current - window->start_time) / (double)frequency;
}

double window_get_delta_time(Window *window)
{
    return window ? window->delta_time : 0.0;
}

void window_resize(Window *window, int width, int height)
{
    if (!window || width <= 0 || height <= 0)
        return;
    if (width == window->width && height == window->height)
        return;

    window->width = width;
    window->height = height;

    {
        int logical_w, logical_h;
        SDL_GetWindowSize(window->sdl_window, &logical_w, &logical_h);
        window->dpi_scale = (logical_w > 0) ? (float)width / (float)logical_w : 1.0f;
    }

    if (window->default_canvas)
    {
        if (!window_canvas_texture_resize(window->default_canvas, width, height))
        {
            set_error(window, "Failed to resize default canvas texture");
            return;
        }
    }
    else
    {
        window->default_canvas = window_canvas_texture_create(width, height);
        if (!window->default_canvas)
        {
            set_error(window, "Failed to create default canvas texture");
            return;
        }
    }
    sync_window_default_canvas_aliases(window);

    glViewport(0, 0, width, height);
}

int window_gl_create_program(Window *window, const char *vertex_path, const char *fragment_path)
{
    char vertex_full_path[PATH_MAX];
    char fragment_full_path[PATH_MAX];
    char *vertex_source = NULL;
    char *fragment_source = NULL;

    if (!resolve_project_path(window, vertex_path, vertex_full_path, sizeof(vertex_full_path)) ||
        !resolve_project_path(window, fragment_path, fragment_full_path, sizeof(fragment_full_path)))
    {
        return -1;
    }

    vertex_source = read_text_file(window, vertex_full_path);
    if (!vertex_source)
        return -1;

    fragment_source = read_text_file(window, fragment_full_path);
    if (!fragment_source)
    {
        free(vertex_source);
        return -1;
    }

    int program_id = window_gl_create_program_from_source(window, vertex_source, strlen(vertex_source),
                                                          fragment_source, strlen(fragment_source));
    free(vertex_source);
    free(fragment_source);

    return program_id;
}

int window_gl_create_program_from_source(Window *window,
                                         const char *vertex_source, size_t vertex_len,
                                         const char *fragment_source, size_t fragment_len)
{
    char *vertex_copy = NULL;
    char *fragment_copy = NULL;
    GLuint program = 0;
    int i;

    if (!window || !vertex_source || !fragment_source)
        return -1;

    vertex_copy = (char *)malloc(vertex_len + 1);
    fragment_copy = (char *)malloc(fragment_len + 1);
    if (!vertex_copy || !fragment_copy)
    {
        free(vertex_copy);
        free(fragment_copy);
        set_error(window, "Failed to allocate shader source buffer");
        return -1;
    }

    memcpy(vertex_copy, vertex_source, vertex_len);
    vertex_copy[vertex_len] = '\0';
    memcpy(fragment_copy, fragment_source, fragment_len);
    fragment_copy[fragment_len] = '\0';

    program = create_program_from_source(window, vertex_copy, fragment_copy, true);
    free(vertex_copy);
    free(fragment_copy);

    if (program == 0)
        return -1;

    for (i = 0; i < MAX_GL_PROGRAMS; i++)
    {
        if (!window->programs[i].in_use)
        {
            window->programs[i].in_use = true;
            window->programs[i].program = program;
            window->programs[i].sampler_loc = glGetUniformLocation(program, "u_canvas");
            window->programs[i].resolution_loc = glGetUniformLocation(program, "u_resolution");
            window->programs[i].time_loc = glGetUniformLocation(program, "u_time");
            window->programs[i].position_loc = glGetAttribLocation(program, "a_position");
            window->programs[i].tex_coord_loc = glGetAttribLocation(program, "a_texCoord");
            memset(window->programs[i].uniform_cache, 0, sizeof(window->programs[i].uniform_cache));
            return i + 1;
        }
    }

    glDeleteProgram(program);
    set_error(window, "Maximum number of shader programs reached");
    return -1;
}

bool window_gl_destroy_program(Window *window, int program_id)
{
    GLProgramSlot *slot = get_program_slot(window, program_id);

    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    glDeleteProgram(slot->program);
    slot->program = 0;
    slot->in_use = false;
    return true;
}

bool window_gl_use_program(Window *window, int program_id)
{
    GLProgramSlot *slot;

    if (!window)
        return false;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    immediate_gl_use_program(slot->program);
    return true;
}

bool window_gl_bind_screen(Window *window)
{
    GLBoundRenderTarget target;

    if (!immediate_gl_bind_screen_target(window, &target))
        return false;

    immediate_gl_apply_render_target_viewport(&target, 0, 0, target.width, target.height);
    return true;
}

bool window_gl_bind_render_target_immediate(Window *window, int target_id)
{
    GLBoundRenderTarget target;

    if (!window)
        return false;
    if (target_id <= 0)
        return window_gl_bind_screen(window);
    if (!immediate_gl_bind_app_render_target(window, target_id, &target))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    immediate_gl_apply_render_target_viewport(&target, 0, 0, target.width, target.height);
    return true;
}

bool window_gl_draw_fullscreen(Window *window, int program_id)
{
    GLProgramSlot *slot;
    GLBoundRenderTarget target;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    immediate_gl_restore_present_baseline(window);
    if (!immediate_gl_bind_screen_target(window, &target))
        return false;
    immediate_gl_apply_render_target_viewport(&target, 0, 0, target.width, target.height);
    immediate_gl_apply_depth_state(&target, false, false, false);
    if (!immediate_gl_draw_fullscreen_now(window, slot->program, slot, 0, true))
    {
        set_error(window, "Immediate fullscreen draw failed");
        return false;
    }
    return true;
}

bool window_gl_draw_fullscreen_immediate(Window *window, int program_id, uint32_t source_texture)
{
    GLProgramSlot *slot = NULL;
    GLuint program = 0;

    if (!window)
        return false;

    if (program_id > 0)
    {
        slot = get_program_slot(window, program_id);
        if (!slot)
        {
            set_error(window, "Invalid shader program id");
            return false;
        }
        program = slot->program;
    }
    else
    {
        GLint current_program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
        program = (GLuint)current_program;
        slot = find_program_slot_by_gl_program(window, program);
    }

    if (program == 0)
    {
        set_error(window, "No shader program selected for immediate fullscreen draw");
        return false;
    }

    if (!immediate_gl_draw_fullscreen_now(window, program, slot, (GLuint)source_texture, true))
    {
        set_error(window, "Immediate fullscreen draw failed");
        return false;
    }
    return true;
}

bool window_gl_draw_fullscreen_pass_immediate(Window *window, int program_id,
                                              int source_target_id,
                                              int destination_target_id)
{
    GLProgramSlot *slot;
    GLRenderTarget *source = NULL;
    GLBoundRenderTarget destination;
    GLuint source_texture;
    GLint saved_framebuffer = 0;
    GLint saved_viewport[4];

    if (!window || source_target_id < 0 || destination_target_id < 0)
        return false;
    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (source_target_id > 0)
    {
        source = get_render_target_slot(window, source_target_id);
        if (!source)
        {
            set_error(window, "Invalid source render target id");
            return false;
        }
    }
    if (source_target_id > 0 && source_target_id == destination_target_id)
    {
        set_error(window, "Fullscreen pass source and destination must differ");
        return false;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_framebuffer);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);

    if (!source && window->skia_gpu_canvas)
        flush_skia_canvas_preserving_gl_state(window->skia_gpu_canvas);

    if (destination_target_id > 0)
    {
        if (!immediate_gl_bind_app_render_target(window, destination_target_id,
                                                 &destination))
        {
            set_error(window, "Invalid destination render target id");
            return false;
        }
    }
    else if (!immediate_gl_bind_screen_target(window, &destination))
        return false;

    immediate_gl_apply_render_target_viewport(
        &destination, 0, 0, destination.width, destination.height);
    immediate_gl_apply_depth_state(&destination, false, false, false);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    source_texture = source ? source->texture : window->canvas_texture;
    if (!immediate_gl_draw_fullscreen_now(window, slot->program, slot,
                                          source_texture, false))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_framebuffer);
        glViewport(saved_viewport[0], saved_viewport[1],
                   saved_viewport[2], saved_viewport[3]);
        set_error(window, "Explicit fullscreen pass failed");
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_framebuffer);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    return true;
}

bool window_gl_draw_region_pass_immediate(Window *window, int program_id,
                                          int source_target_id,
                                          int destination_target_id,
                                          int x, int y,
                                          int width, int height)
{
    GLProgramSlot *slot;
    GLRenderTarget *source = NULL;
    GLRenderTarget *destination_slot = NULL;
    GLBoundRenderTarget destination;
    GLuint source_texture;
    GLint saved_framebuffer = 0;
    GLint saved_viewport[4];
    GLint offset_loc;
    int64_t right;
    int64_t bottom;
    int clip_left;
    int clip_top;
    int clip_right;
    int clip_bottom;
    int viewport_y;
    int scissor_y;

    if (!window || source_target_id < 0 || destination_target_id < 0 ||
        width <= 0 || height <= 0)
        return false;
    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (source_target_id > 0)
    {
        source = get_render_target_slot(window, source_target_id);
        if (!source)
        {
            set_error(window, "Invalid source render target id");
            return false;
        }
    }
    if (destination_target_id > 0)
    {
        destination_slot = get_render_target_slot(window, destination_target_id);
        if (!destination_slot)
        {
            set_error(window, "Invalid destination render target id");
            return false;
        }
        destination.is_screen = false;
        destination.width = destination_slot->width;
        destination.height = destination_slot->height;
        destination.has_depth = destination_slot->has_depth;
    }
    else
    {
        destination.is_screen = true;
        destination.width = window->width;
        destination.height = window->height;
        destination.has_depth = true;
    }
    if (source_target_id > 0 && source_target_id == destination_target_id)
    {
        set_error(window, "Region pass source and destination must differ");
        return false;
    }

    right = (int64_t)x + (int64_t)width;
    bottom = (int64_t)y + (int64_t)height;
    clip_left = x > 0 ? x : 0;
    clip_top = y > 0 ? y : 0;
    clip_right = right < destination.width ? (int)right : destination.width;
    clip_bottom = bottom < destination.height ? (int)bottom : destination.height;
    if (clip_left >= clip_right || clip_top >= clip_bottom)
        return true;
    if (bottom < INT_MIN || bottom > INT_MAX ||
        (int64_t)destination.height - bottom < INT_MIN ||
        (int64_t)destination.height - bottom > INT_MAX)
    {
        set_error(window, "Region pass bounds exceed the backend coordinate range");
        return false;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_framebuffer);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);
    if (!source && window->skia_gpu_canvas)
        flush_skia_canvas_preserving_gl_state(window->skia_gpu_canvas);

    if (destination_slot)
        glBindFramebuffer(GL_FRAMEBUFFER, destination_slot->fbo);
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!window->screen_rendered_this_frame)
        {
            if (source && window->skia_gpu_canvas)
                flush_skia_canvas_discarding_gl_state(window->skia_gpu_canvas);
            immediate_gl_restore_present_baseline(window);
            draw_fullscreen(window, window->blit_program, window->canvas_texture);
        }
    }

    viewport_y = destination.height - (int)bottom;
    scissor_y = destination.height - clip_bottom;
    immediate_gl_apply_render_target_viewport(&destination, x, viewport_y,
                                              width, height);
    immediate_gl_apply_depth_state(&destination, false, false, false);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);
    glScissor(clip_left, scissor_y, clip_right - clip_left,
              clip_bottom - clip_top);

    source_texture = source ? source->texture : window->canvas_texture;
    immediate_gl_use_program(slot->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    immediate_gl_apply_texture_2d_sampling();
    immediate_gl_apply_builtin_uniforms(window, slot->program, slot,
                                        (GLfloat)destination.width,
                                        (GLfloat)destination.height, 0, true);
    offset_loc = glGetUniformLocation(slot->program, "u_offset");
    if (offset_loc >= 0)
        glUniform2f(offset_loc, (GLfloat)x, (GLfloat)y);
    immediate_gl_draw_fullscreen_geometry(window, slot->program, slot);
    immediate_gl_use_program(0);
    glDisable(GL_SCISSOR_TEST);
    if (destination.is_screen)
        window->screen_rendered_this_frame = true;

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_framebuffer);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    return true;
}

bool window_gl_set_uniform_1i(Window *window, int program_id, const char *name, int value)
{
    GLint v = (GLint)value;
    return set_program_uniform(window, program_id, name, GL_UNIFORM_1I, 1, &v, sizeof(v));
}

bool window_gl_set_uniform_1f(Window *window, int program_id, const char *name, float value)
{
    GLfloat v = value;
    return set_program_uniform(window, program_id, name, GL_UNIFORM_1F, 1, &v, sizeof(v));
}

bool window_gl_set_uniform_2f(Window *window, int program_id, const char *name, float v0, float v1)
{
    GLfloat v[2] = {v0, v1};
    return set_program_uniform(window, program_id, name, GL_UNIFORM_2F, 1, v, sizeof(v));
}

bool window_gl_set_uniform_3f(Window *window, int program_id, const char *name, float v0, float v1, float v2)
{
    GLfloat v[3] = {v0, v1, v2};
    return set_program_uniform(window, program_id, name, GL_UNIFORM_3F, 1, v, sizeof(v));
}

bool window_gl_set_uniform_4f(Window *window, int program_id, const char *name, float v0, float v1, float v2, float v3)
{
    GLfloat v[4] = {v0, v1, v2, v3};
    return set_program_uniform(window, program_id, name, GL_UNIFORM_4F, 1, v, sizeof(v));
}

const char *window_gl_get_error(Window *window)
{
    return window ? window->error_msg : NULL;
}

const char *window_get_project_dir(Window *window)
{
    return window ? window->project_dir : NULL;
}

static const GlRenderTargetApi render_target_api = {
    (void (*)(int, uint32_t *))glGenTextures,
    (void (*)(uint32_t, uint32_t))glBindTexture,
    (void (*)(uint32_t, uint32_t, int))glTexParameteri,
    (void (*)(uint32_t, int, int, int, int, int, uint32_t, uint32_t,
              const void *))glTexImage2D,
    (void (*)(int, const uint32_t *))glDeleteTextures,
    (void (*)(int, uint32_t *))glGenFramebuffers,
    (void (*)(uint32_t, uint32_t))glBindFramebuffer,
    (void (*)(uint32_t, uint32_t, uint32_t, uint32_t, int))glFramebufferTexture2D,
    (uint32_t(*)(uint32_t))glCheckFramebufferStatus,
    (void (*)(int, const uint32_t *))glDeleteFramebuffers,
    (void (*)(int, uint32_t *))glGenRenderbuffers,
    (void (*)(uint32_t, uint32_t))glBindRenderbuffer,
    (void (*)(uint32_t, uint32_t, int, int))glRenderbufferStorage,
    (void (*)(uint32_t, uint32_t, uint32_t, uint32_t))glFramebufferRenderbuffer,
    (void (*)(int, const uint32_t *))glDeleteRenderbuffers};

static const GlRenderTargetConstants render_target_constants = {
    GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER,
    GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_LINEAR, GL_CLAMP_TO_EDGE,
    GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, GL_FRAMEBUFFER,
    GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
    GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_COMPLETE};

int window_gl_create_render_target(Window *window, int width, int height, bool depth)
{
    int i;
    GLRenderTarget *rt;

    if (!window || width <= 0 || height <= 0)
        return -1;

    for (i = 0; i < MAX_GL_RENDER_TARGETS; i++)
    {
        if (!window->render_targets[i].in_use)
            break;
    }

    if (i == MAX_GL_RENDER_TARGETS)
    {
        set_error(window, "Maximum number of render targets reached");
        return -1;
    }

    rt = &window->render_targets[i];
    if (!gl_render_target_create(&render_target_api, &render_target_constants,
                                 rt, width, height, depth))
    {
        set_error(window, "Render target framebuffer incomplete");
        return -1;
    }
    return i + 1;
}

bool window_gl_destroy_render_target(Window *window, int target_id)
{
    GLRenderTarget *rt = get_render_target_slot(window, target_id);

    if (!rt)
    {
        if (window)
            set_error(window, "Invalid render target id");
        return false;
    }

    gl_render_target_destroy(&render_target_api, rt);
    return true;
}

bool window_gl_resize_render_target(Window *window, int target_id, int width, int height)
{
    GLRenderTarget *rt;
    if (!window || width <= 0 || height <= 0)
        return false;

    rt = get_render_target_slot(window, target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    if (!gl_render_target_resize(&render_target_api, &render_target_constants,
                                 rt, width, height))
    {
        set_error(window, "Render target framebuffer incomplete after resize");
        return false;
    }
    return true;
}

bool window_gl_draw_region(Window *window, int program_id,
                           float x, float y, float w, float h,
                           int target_id)
{
    GLProgramSlot *slot;

    if (!window)
        return false;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    return window_gl_draw_region_immediate(window, program_id, x, y, w, h, target_id);
}

bool window_gl_draw_region_immediate(Window *window, int program_id,
                                     float x, float y, float w, float h,
                                     int target_id)
{
    GLProgramSlot *slot = NULL;
    GLuint program = 0;
    bool bind_explicit_target = target_id != 0;

    if (!window)
        return false;

    if (program_id > 0)
    {
        slot = get_program_slot(window, program_id);
        if (!slot)
        {
            set_error(window, "Invalid shader program id");
            return false;
        }
        program = slot->program;
    }
    else
    {
        GLint current_program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
        program = (GLuint)current_program;
        slot = find_program_slot_by_gl_program(window, program);
    }

    if (program == 0)
    {
        set_error(window, "No shader program selected for immediate region draw");
        return false;
    }

    if (w <= 0.0f || h <= 0.0f)
    {
        set_error(window, "Region draw dimensions must be positive");
        return false;
    }

    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    if (!immediate_gl_draw_region_now(window, program, slot,
                                      x, y, w, h,
                                      target_id, bind_explicit_target,
                                      true))
    {
        set_error(window, "Immediate region draw failed");
        return false;
    }
    return true;
}

bool window_gl_bind_texture(Window *window, int program_id,
                            const char *uniform_name,
                            int render_target_id, int texture_unit)
{
    GLRenderTarget *target;

    if (!window)
        return false;
    target = get_render_target_slot(window, render_target_id);
    if (!target)
    {
        set_error(window, "Invalid render target id");
        return false;
    }
    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             target->texture, GL_TEXTURE_2D,
                                             texture_unit);
}

bool window_gl_bind_canvas_texture(Window *window, int program_id,
                                   const char *uniform_name,
                                   CanvasTexture *canvas_texture,
                                   int texture_unit)
{
    uint32_t texture_id;

    if (!window)
        return false;
    if (!canvas_texture)
    {
        set_error(window, "Invalid canvas texture");
        return false;
    }

    texture_id = window_canvas_texture_get_gl_texture(canvas_texture);
    if (texture_id == 0)
    {
        set_error(window, "Canvas texture has no GL texture");
        return false;
    }

    window_canvas_texture_flush(canvas_texture);
    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             (GLuint)texture_id, GL_TEXTURE_2D,
                                             texture_unit);
}

static bool immediate_gl_bind_sampler_texture(Window *window, int program_id,
                                              const char *uniform_name,
                                              GLuint texture_id, GLenum texture_target,
                                              int texture_unit)
{
    GLProgramSlot *slot;
    GLint uniform_loc;
    GLint max_units = 0;

    if (!window || !uniform_name || texture_id == 0)
        return false;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_units);
    if (texture_unit < 0 || texture_unit > 7 || texture_unit >= max_units)
    {
        set_error(window, "Texture unit must be between 0 and 7");
        return false;
    }

    immediate_gl_use_program(slot->program);
    glActiveTexture(GL_TEXTURE0 + (GLenum)texture_unit);
    glBindTexture(texture_target, texture_id);
    if (texture_target == GL_TEXTURE_2D)
        immediate_gl_apply_texture_2d_sampling();

    uniform_loc = glGetUniformLocation(slot->program, uniform_name);
    if (uniform_loc >= 0)
        glUniform1i(uniform_loc, texture_unit);

    return true;
}

bool window_gl_bind_texture_immediate(Window *window, int program_id,
                                      const char *uniform_name,
                                      int render_target_id, int texture_unit)
{
    GLRenderTarget *target;

    if (!window)
        return false;
    target = get_render_target_slot(window, render_target_id);
    if (!target)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             target->texture, GL_TEXTURE_2D,
                                             texture_unit);
}

bool window_gl_bind_canvas_texture_immediate(Window *window, int program_id,
                                             const char *uniform_name,
                                             CanvasTexture *canvas_texture,
                                             int texture_unit)
{
    uint32_t texture_id;

    if (!window)
        return false;
    if (!canvas_texture)
    {
        set_error(window, "Invalid canvas texture");
        return false;
    }

    window_canvas_texture_flush(canvas_texture);
    texture_id = window_canvas_texture_get_gl_texture(canvas_texture);
    if (texture_id == 0)
    {
        set_error(window, "Canvas texture has no GL texture");
        return false;
    }

    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             (GLuint)texture_id, GL_TEXTURE_2D,
                                             texture_unit);
}

static GLenum gl_buffer_target_to_gl(WindowGLBufferTarget t)
{
    return (t == WINDOW_GL_BUFFER_INDEX) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
}

static GLenum gl_buffer_usage_to_gl(WindowGLBufferUsage u)
{
    switch (u)
    {
    case WINDOW_GL_USAGE_DYNAMIC:
        return GL_DYNAMIC_DRAW;
    case WINDOW_GL_USAGE_STREAM:
        return GL_STREAM_DRAW;
    default:
        return GL_STATIC_DRAW;
    }
}

static GLenum gl_attr_type_to_gl(WindowGLAttrType t)
{
    switch (t)
    {
    case WINDOW_GL_ATTR_BYTE:
        return GL_BYTE;
    case WINDOW_GL_ATTR_UBYTE:
        return GL_UNSIGNED_BYTE;
    case WINDOW_GL_ATTR_SHORT:
        return GL_SHORT;
    case WINDOW_GL_ATTR_USHORT:
        return GL_UNSIGNED_SHORT;
    case WINDOW_GL_ATTR_INT:
        return GL_INT;
    case WINDOW_GL_ATTR_UINT:
        return GL_UNSIGNED_INT;
    default:
        return GL_FLOAT;
    }
}

static GLenum gl_prim_to_gl(WindowGLPrimitive p)
{
    switch (p)
    {
    case WINDOW_GL_PRIM_TRIANGLE_STRIP:
        return GL_TRIANGLE_STRIP;
    case WINDOW_GL_PRIM_TRIANGLE_FAN:
        return GL_TRIANGLE_FAN;
    case WINDOW_GL_PRIM_LINES:
        return GL_LINES;
    case WINDOW_GL_PRIM_LINE_STRIP:
        return GL_LINE_STRIP;
    case WINDOW_GL_PRIM_POINTS:
        return GL_POINTS;
    default:
        return GL_TRIANGLES;
    }
}

static GLenum gl_tex_format_internal(WindowGLTexFormat f)
{
    switch (f)
    {
    case WINDOW_GL_TEX_RGB8:
        return GL_RGB;
    case WINDOW_GL_TEX_R8:
        return GL_LUMINANCE; 
    default:
        return GL_RGBA;
    }
}

static GLenum gl_tex_format_upload(WindowGLTexFormat f)
{
    return gl_tex_format_internal(f);
}

static GLBufferSlot *get_buffer_slot(Window *window, int buffer_id)
{
    return window ? (GLBufferSlot *)gpu_resource_slot_lookup(
                        window->buffers, sizeof(window->buffers[0]),
                        MAX_GL_BUFFERS, buffer_id)
                  : NULL;
}

static GLTextureSlot *get_texture_slot(Window *window, int texture_id)
{
    return window ? (GLTextureSlot *)gpu_resource_slot_lookup(
                        window->textures, sizeof(window->textures[0]),
                        MAX_GL_TEXTURES, texture_id)
                  : NULL;
}

static GLVertexLayout *get_layout_slot(Window *window, int layout_id)
{
    if (!window || layout_id <= 0 || layout_id > MAX_GL_VERTEX_LAYOUTS)
        return NULL;
    if (!window->vertex_layouts[layout_id - 1].in_use)
        return NULL;
    return &window->vertex_layouts[layout_id - 1];
}

int window_gl_create_buffer(Window *window)
{
    int i;
    if (!window)
        return -1;
    for (i = 0; i < MAX_GL_BUFFERS; i++)
    {
        if (!window->buffers[i].in_use)
        {
            GLuint id = 0;
            glGenBuffers(1, &id);
            if (id == 0)
            {
                set_error(window, "glGenBuffers failed");
                return -1;
            }
            memset(&window->buffers[i], 0, sizeof(window->buffers[i]));
            window->buffers[i].in_use = true;
            window->buffers[i].id = id;
            window->buffers[i].target = GL_ARRAY_BUFFER;
            return i + 1;
        }
    }
    set_error(window, "Maximum number of buffers reached");
    return -1;
}

bool window_gl_destroy_buffer(Window *window, int buffer_id)
{
    GLBufferSlot *b = get_buffer_slot(window, buffer_id);
    if (!b)
    {
        if (window)
            set_error(window, "Invalid buffer id");
        return false;
    }
    glDeleteBuffers(1, &b->id);
    memset(b, 0, sizeof(*b));
    return true;
}

bool window_gl_buffer_data(Window *window, int buffer_id,
                           WindowGLBufferTarget target,
                           const void *data, size_t size,
                           WindowGLBufferUsage usage)
{
    GLBufferSlot *b = get_buffer_slot(window, buffer_id);
    GLenum gl_target;
    if (!b)
    {
        set_error(window, "Invalid buffer id");
        return false;
    }
    gl_target = gl_buffer_target_to_gl(target);
    b->target = gl_target;
    b->size = size;
    glBindBuffer(gl_target, b->id);
    glBufferData(gl_target, (GLsizeiptr)size, data, gl_buffer_usage_to_gl(usage));
    glBindBuffer(gl_target, 0);
    return true;
}

bool window_gl_buffer_sub_data(Window *window, int buffer_id,
                               size_t offset, const void *data, size_t size)
{
    GLBufferSlot *b = get_buffer_slot(window, buffer_id);

    if (!b)
    {
        set_error(window, "Invalid buffer id");
        return false;
    }
    if (b->target == 0)
    {
        set_error(window, "Buffer has no data uploaded yet");
        return false;
    }
    if (size == 0)
        return true;

    return upload_buffer_sub_data_immediate(window, b, offset, data, size);
}

static int alloc_texture_slot(Window *window)
{
    int i;
    for (i = 0; i < MAX_GL_TEXTURES; i++)
        if (!window->textures[i].in_use)
            return i;
    return -1;
}

int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    int slot;
    GLuint id = 0;
    GLenum internal_fmt, upload_fmt;
    if (!window || width <= 0 || height <= 0)
        return -1;
    slot = alloc_texture_slot(window);
    if (slot < 0)
    {
        set_error(window, "Maximum number of textures reached");
        return -1;
    }
    glGenTextures(1, &id);
    if (id == 0)
    {
        set_error(window, "glGenTextures failed");
        return -1;
    }
    internal_fmt = gl_tex_format_internal(format);
    upload_fmt = gl_tex_format_upload(format);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0,
                 upload_fmt, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    memset(&window->textures[slot], 0, sizeof(window->textures[slot]));
    window->textures[slot].in_use = true;
    window->textures[slot].id = id;
    window->textures[slot].target = GL_TEXTURE_2D;
    window->textures[slot].width = width;
    window->textures[slot].height = height;
    window->textures[slot].format = upload_fmt;
    return slot + 1;
}

int window_gl_create_texture_2d_from_file(Window *window, const char *path)
{
    char resolved[PATH_MAX];
    size_t byte_count = 0;
    uint8_t *bytes;
    int id;
    if (!window || !path)
        return -1;
    if (!resolve_project_path(window, path, resolved, sizeof(resolved)))
        return -1;
    bytes = read_binary_file(window, resolved, &byte_count);
    if (!bytes)
        return -1;
    id = window_gl_create_texture_2d_from_buffer(window, bytes, byte_count);
    free(bytes);
    return id;
}

int window_gl_create_texture_2d_from_buffer(Window *window, const uint8_t *bytes, size_t byte_count)
{
    int w = 0, h = 0, id;
    uint8_t *pixels;
    if (!window || !bytes || byte_count == 0)
        return -1;
    pixels = image_load_rgba8_from_memory(bytes, byte_count, &w, &h);
    if (!pixels)
    {
        const char *why = image_loader_last_error();
        char msg[512];
        snprintf(msg, sizeof(msg), "loadTexture2DFromBuffer: failed to decode image: %s",
                 why ? why : "unknown error");
        set_error(window, msg);
        return -1;
    }
    id = window_gl_create_texture_2d(window, w, h, WINDOW_GL_TEX_RGBA8, pixels);
    image_free(pixels);
    return id;
}

bool window_gl_get_texture_size(Window *window, int texture_id,
                                int *out_width, int *out_height)
{
    GLTextureSlot *texture = get_texture_slot(window, texture_id);
    if (!texture || texture->target != GL_TEXTURE_2D ||
        !out_width || !out_height)
    {
        if (window)
            set_error(window, "Invalid 2D texture or size output");
        return false;
    }
    *out_width = texture->width;
    *out_height = texture->height;
    return true;
}

int window_gl_create_texture_cube(Window *window, int size,
                                  WindowGLTexFormat format,
                                  const void *const faces[6])
{
    int slot, i;
    GLuint id = 0;
    GLenum internal_fmt, upload_fmt;
    if (!window || size <= 0 || !faces)
        return -1;
    slot = alloc_texture_slot(window);
    if (slot < 0)
    {
        set_error(window, "Maximum number of textures reached");
        return -1;
    }
    glGenTextures(1, &id);
    if (id == 0)
    {
        set_error(window, "glGenTextures failed");
        return -1;
    }
    internal_fmt = gl_tex_format_internal(format);
    upload_fmt = gl_tex_format_upload(format);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, internal_fmt,
                     size, size, 0, upload_fmt, GL_UNSIGNED_BYTE, faces[i]);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    memset(&window->textures[slot], 0, sizeof(window->textures[slot]));
    window->textures[slot].in_use = true;
    window->textures[slot].id = id;
    window->textures[slot].target = GL_TEXTURE_CUBE_MAP;
    window->textures[slot].width = size;
    window->textures[slot].height = size;
    window->textures[slot].format = upload_fmt;
    return slot + 1;
}

int window_gl_create_texture_cube_from_files(Window *window, const char *const paths[6])
{
    char resolved[6][PATH_MAX];
    uint8_t *bytes[6] = {0};
    size_t byte_counts[6] = {0};
    int i, id = -1;
    if (!window || !paths)
        return -1;
    for (i = 0; i < 6; ++i)
    {
        if (!paths[i] || !resolve_project_path(window, paths[i], resolved[i], sizeof(resolved[i])))
            goto cleanup;
        bytes[i] = read_binary_file(window, resolved[i], &byte_counts[i]);
        if (!bytes[i])
            goto cleanup;
    }
    id = window_gl_create_texture_cube_from_buffers(window, (const uint8_t *const *)bytes, byte_counts);
cleanup:
    for (i = 0; i < 6; ++i)
        free(bytes[i]);
    return id;
}

int window_gl_create_texture_cube_from_buffers(Window *window,
                                               const uint8_t *const bytes[6],
                                               const size_t byte_counts[6])
{
    uint8_t *pixels[6] = {0};
    int w[6] = {0}, h[6] = {0};
    const void *faces[6];
    int i, id = -1, size;
    if (!window || !bytes || !byte_counts)
        return -1;
    for (i = 0; i < 6; ++i)
    {
        if (!bytes[i] || byte_counts[i] == 0)
            goto cleanup;
        pixels[i] = image_load_rgba8_from_memory(bytes[i], byte_counts[i], &w[i], &h[i]);
        if (!pixels[i])
        {
            char msg[512];
            const char *why = image_loader_last_error();
            snprintf(msg, sizeof(msg), "loadTextureCubeFromBuffer: failed to decode face %d: %s",
                     i, why ? why : "unknown error");
            set_error(window, msg);
            goto cleanup;
        }
        faces[i] = pixels[i];
    }
    size = w[0];
    for (i = 0; i < 6; ++i)
    {
        if (w[i] != size || h[i] != size)
        {
            set_error(window, "loadTextureCubeFromBuffer: all 6 faces must be square and the same size");
            goto cleanup;
        }
    }
    id = window_gl_create_texture_cube(window, size, WINDOW_GL_TEX_RGBA8, faces);
cleanup:
    for (i = 0; i < 6; ++i)
        if (pixels[i])
            image_free(pixels[i]);
    return id;
}

bool window_gl_update_texture_2d(Window *window, int texture_id,
                                 int x, int y, int w, int h, const void *pixels)
{
    GLTextureSlot *t = get_texture_slot(window, texture_id);
    if (!t)
    {
        set_error(window, "Invalid texture id");
        return false;
    }
    if (t->target != GL_TEXTURE_2D)
    {
        set_error(window, "Texture is not a 2D texture");
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, t->format, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool window_gl_destroy_texture(Window *window, int texture_id)
{
    GLTextureSlot *t = get_texture_slot(window, texture_id);
    if (!t)
    {
        if (window)
            set_error(window, "Invalid texture id");
        return false;
    }
    glDeleteTextures(1, &t->id);
    memset(t, 0, sizeof(*t));
    return true;
}

int window_gl_create_vertex_layout(Window *window)
{
    int i;
    if (!window)
        return -1;
    for (i = 0; i < MAX_GL_VERTEX_LAYOUTS; i++)
    {
        if (!window->vertex_layouts[i].in_use)
        {
            GLuint vao = 0;
            glGenVertexArrays(1, &vao);
            if (vao == 0)
            {
                set_error(window, "glGenVertexArrays failed");
                return -1;
            }
            memset(&window->vertex_layouts[i], 0, sizeof(window->vertex_layouts[i]));
            window->vertex_layouts[i].in_use = true;
            window->vertex_layouts[i].vao = vao;
            window->vertex_layouts[i].needs_rebind = true;
            return i + 1;
        }
    }
    set_error(window, "Maximum number of vertex layouts reached");
    return -1;
}

bool window_gl_set_attribute(Window *window, int layout_id,
                             int location, int buffer_id,
                             int size, WindowGLAttrType type,
                             bool normalized, int stride, int offset, int divisor)
{
    GLVertexLayout *layout = get_layout_slot(window, layout_id);
    GLBufferSlot *b;
    int i;
    if (!layout)
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    b = get_buffer_slot(window, buffer_id);
    if (!b)
    {
        set_error(window, "Invalid buffer id");
        return false;
    }
    if (location < 0 || location > 31)
    {
        set_error(window, "Attribute location out of range");
        return false;
    }
    if (size < 1 || size > 4)
    {
        set_error(window, "Attribute component count must be 1..4");
        return false;
    }

    for (i = 0; i < layout->attr_count; i++)
    {
        if (layout->attrs[i].active && layout->attrs[i].location == location)
            break;
    }
    if (i == layout->attr_count)
    {
        if (layout->attr_count >= MAX_GL_LAYOUT_ATTRS)
        {
            set_error(window, "Maximum attributes per layout reached");
            return false;
        }
        layout->attr_count++;
    }
    layout->attrs[i].active = true;
    layout->attrs[i].location = location;
    layout->attrs[i].buffer_id = buffer_id;
    layout->attrs[i].size = size;
    layout->attrs[i].gl_type = gl_attr_type_to_gl(type);
    layout->attrs[i].normalized = normalized;
    layout->attrs[i].stride = stride;
    layout->attrs[i].offset = offset;
    layout->attrs[i].divisor = divisor;
    layout->needs_rebind = true;
    return true;
}

bool window_gl_set_index_buffer(Window *window, int layout_id,
                                int buffer_id, WindowGLIndexType type)
{
    GLVertexLayout *layout = get_layout_slot(window, layout_id);
    if (!layout)
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (buffer_id <= 0)
    {
        layout->index_buffer_id = 0;
        layout->index_gl_type = 0;
        layout->needs_rebind = true;
        return true;
    }
    if (!get_buffer_slot(window, buffer_id))
    {
        set_error(window, "Invalid index buffer id");
        return false;
    }
    layout->index_buffer_id = buffer_id;
    layout->index_gl_type = (type == WINDOW_GL_INDEX_U32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    layout->needs_rebind = true;
    return true;
}

bool window_gl_destroy_vertex_layout(Window *window, int layout_id)
{
    GLVertexLayout *layout = get_layout_slot(window, layout_id);
    if (!layout)
    {
        if (window)
            set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (layout->vao != 0)
        glDeleteVertexArrays(1, &layout->vao);
    memset(layout, 0, sizeof(*layout));
    return true;
}

#define UNIFORM_ARRAY_SETTER(suffix, ctype, kind_enum, components_per_elem)    \
    bool window_gl_set_uniform_##suffix(Window *window, int program_id,        \
                                        const char *name,                      \
                                        const ctype *values, int count)        \
    {                                                                          \
        size_t bytes;                                                          \
        if (count < 1)                                                         \
            count = 1;                                                         \
        bytes = sizeof(ctype) * (size_t)(components_per_elem) * (size_t)count; \
        return set_program_uniform(window, program_id, name, kind_enum, count, \
                                   values, bytes);                             \
    }

UNIFORM_ARRAY_SETTER(matrix3fv, float, GL_UNIFORM_MAT3FV, 9)
UNIFORM_ARRAY_SETTER(matrix4fv, float, GL_UNIFORM_MAT4FV, 16)
UNIFORM_ARRAY_SETTER(1iv, int, GL_UNIFORM_1IV, 1)
UNIFORM_ARRAY_SETTER(1fv, float, GL_UNIFORM_1FV, 1)
UNIFORM_ARRAY_SETTER(2fv, float, GL_UNIFORM_2FV, 2)
UNIFORM_ARRAY_SETTER(3fv, float, GL_UNIFORM_3FV, 3)
UNIFORM_ARRAY_SETTER(4fv, float, GL_UNIFORM_4FV, 4)

#undef UNIFORM_ARRAY_SETTER

static bool bind_texture_common(Window *window, int program_id,
                                const char *uniform_name, int texture_id,
                                int texture_unit,
                                GLenum required_target)
{
    GLTextureSlot *t;
    if (!window)
        return false;
    t = get_texture_slot(window, texture_id);
    if (!t)
    {
        set_error(window, "Invalid texture id");
        return false;
    }
    if (t->target != required_target)
    {
        set_error(window, "Texture target does not match binding kind");
        return false;
    }
    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             t->id, required_target,
                                             texture_unit);
}

bool window_gl_bind_texture_2d(Window *window, int program_id,
                               const char *uniform_name,
                               int texture_id, int texture_unit)
{
    return bind_texture_common(window, program_id, uniform_name, texture_id,
                               texture_unit, GL_TEXTURE_2D);
}

bool window_gl_bind_texture_cube(Window *window, int program_id,
                                 const char *uniform_name,
                                 int texture_id, int texture_unit)
{
    return bind_texture_common(window, program_id, uniform_name, texture_id,
                               texture_unit, GL_TEXTURE_CUBE_MAP);
}

bool window_gl_bind_texture_2d_immediate(Window *window, int program_id,
                                         const char *uniform_name,
                                         int texture_id, int texture_unit)
{
    GLTextureSlot *texture;

    if (!window)
        return false;
    texture = get_texture_slot(window, texture_id);
    if (!texture)
    {
        set_error(window, "Invalid texture id");
        return false;
    }
    if (texture->target != GL_TEXTURE_2D)
    {
        set_error(window, "Texture target does not match binding kind");
        return false;
    }

    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             texture->id, GL_TEXTURE_2D,
                                             texture_unit);
}

bool window_gl_bind_texture_cube_immediate(Window *window, int program_id,
                                           const char *uniform_name,
                                           int texture_id, int texture_unit)
{
    GLTextureSlot *texture;

    if (!window)
        return false;
    texture = get_texture_slot(window, texture_id);
    if (!texture)
    {
        set_error(window, "Invalid texture id");
        return false;
    }
    if (texture->target != GL_TEXTURE_CUBE_MAP)
    {
        set_error(window, "Texture target does not match binding kind");
        return false;
    }

    return immediate_gl_bind_sampler_texture(window, program_id, uniform_name,
                                             texture->id, GL_TEXTURE_CUBE_MAP,
                                             texture_unit);
}

bool window_gl_draw_mesh(Window *window, int program_id, int layout_id,
                         WindowGLPrimitive mode, int first, int count,
                         int target_id,
                         const WindowGLDrawState *state,
                         int instance_count)
{
    if (!window)
        return false;
    if (!get_program_slot(window, program_id))
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!get_layout_slot(window, layout_id))
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (count <= 0)
    {
        set_error(window, "Mesh draw count must be > 0");
        return false;
    }
    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    return window_gl_draw_mesh_immediate(window, program_id, layout_id,
                                         mode, first, count, target_id,
                                         state, instance_count);
}

static void apply_blend_mode(int mode)
{
    if (mode == WINDOW_GL_BLEND_NONE)
    {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    switch (mode)
    {
    case WINDOW_GL_BLEND_ADD:
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case WINDOW_GL_BLEND_PREMULT:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

static bool immediate_gl_draw_mesh_now(Window *window, int program_id, int layout_id,
                                       GLenum mesh_mode, int first, int count,
                                       int target_id, bool bind_explicit_target,
                                       bool depth_test, bool depth_write,
                                       GLenum cull, int blend,
                                       int instance_count)
{
    GLProgramSlot *slot;
    GLVertexLayout *layout;
    GLBoundRenderTarget target;
    int i;
    int viewport_w, viewport_h;

    if (!window)
        return false;
    slot = get_program_slot(window, program_id);
    layout = get_layout_slot(window, layout_id);
    if (!slot || !layout)
        return false;

    if (bind_explicit_target)
    {
        if (!immediate_gl_bind_render_target(window, target_id, &target))
            return false;
    }
    else if (!immediate_gl_get_bound_render_target(window, &target))
    {
        return false;
    }

    viewport_w = target.width;
    viewport_h = target.height;

    if (target.is_screen && !window->screen_rendered_this_frame)
    {
        if (window->skia_gpu_canvas)
            flush_skia_canvas_discarding_gl_state(window->skia_gpu_canvas);
        immediate_gl_restore_present_baseline(window);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, window->canvas_texture);
        immediate_gl_apply_texture_2d_sampling();
        draw_fullscreen(window, window->blit_program, window->canvas_texture);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
    }

    immediate_gl_apply_render_target_viewport(&target, 0, 0, viewport_w, viewport_h);

    immediate_gl_use_program(slot->program);

    immediate_gl_apply_builtin_uniforms(window, slot->program, slot,
                                        (GLfloat)viewport_w, (GLfloat)viewport_h,
                                        0, false);

    glBindVertexArray(layout->vao);

    immediate_gl_apply_depth_state(&target, depth_test, depth_write,
                                   depth_test && !target.is_screen);

    if (cull != GL_NONE)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(cull);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    apply_blend_mode(blend);

    if (layout->needs_rebind)
    {
        glBindVertexArray(layout->vao);
        for (i = 0; i < MAX_GL_LAYOUT_ATTRS; i++)
        {
            glDisableVertexAttribArray((GLuint)i);
#if defined(GL_VERSION_3_3) || defined(GL_ARB_instanced_arrays)
            glVertexAttribDivisor((GLuint)i, 0);
#endif
        }
        for (i = 0; i < layout->attr_count; i++)
        {
            GLVertexAttribDesc *a = &layout->attrs[i];
            GLBufferSlot *b;
            if (!a->active)
                continue;
            b = get_buffer_slot(window, a->buffer_id);
            if (!b)
                continue;
            glBindBuffer(GL_ARRAY_BUFFER, b->id);
            glEnableVertexAttribArray((GLuint)a->location);
            glVertexAttribPointer((GLuint)a->location, a->size, a->gl_type,
                                  a->normalized ? GL_TRUE : GL_FALSE,
                                  (GLsizei)a->stride,
                                  (const void *)(intptr_t)a->offset);
#if defined(GL_VERSION_3_3) || defined(GL_ARB_instanced_arrays)
            glVertexAttribDivisor((GLuint)a->location, (GLuint)(a->divisor > 0 ? a->divisor : 0));
#endif
        }
        if (layout->index_buffer_id > 0)
        {
            GLBufferSlot *ib = get_buffer_slot(window, layout->index_buffer_id);
            if (ib)
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        layout->needs_rebind = false;
    }

    if (layout->index_buffer_id > 0)
    {
        GLBufferSlot *ib = get_buffer_slot(window, layout->index_buffer_id);
        if (ib)
        {
            size_t element_size = (layout->index_gl_type == GL_UNSIGNED_INT) ? 4 : 2;
            const void *offset_ptr = (const void *)(intptr_t)((size_t)first * element_size);
            if (instance_count > 1)
            {
#if defined(GL_VERSION_3_1) || defined(GL_ARB_draw_instanced)
                glDrawElementsInstanced(mesh_mode, count,
                                        layout->index_gl_type, offset_ptr,
                                        instance_count);
#else
                glDrawElements(mesh_mode, count,
                               layout->index_gl_type, offset_ptr);
#endif
            }
            else
            {
                glDrawElements(mesh_mode, count,
                               layout->index_gl_type, offset_ptr);
            }
        }
    }
    else
    {
        if (instance_count > 1)
        {
#if defined(GL_VERSION_3_1) || defined(GL_ARB_draw_instanced)
            glDrawArraysInstanced(mesh_mode, first,
                                  count, instance_count);
#else
            glDrawArrays(mesh_mode, first, count);
#endif
        }
        else
        {
            glDrawArrays(mesh_mode, first, count);
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    immediate_gl_cleanup_texture_units(1, 7);

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    immediate_gl_use_program(0);
    if (target.is_screen)
        window->screen_rendered_this_frame = true;
    else if (bind_explicit_target)
        immediate_gl_bind_screen_target(window, NULL);

    return true;
}

bool window_gl_draw_mesh_immediate(Window *window, int program_id, int layout_id,
                                   WindowGLPrimitive mode, int first, int count,
                                   int target_id,
                                   const WindowGLDrawState *state,
                                   int instance_count)
{
    GLenum cull_gl = GL_NONE;

    if (!window)
        return false;
    if (!get_program_slot(window, program_id))
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!get_layout_slot(window, layout_id))
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (count <= 0)
    {
        set_error(window, "Mesh draw count must be > 0");
        return false;
    }
    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    if (state)
    {
        switch (state->cull)
        {
        case WINDOW_GL_CULL_BACK:
            cull_gl = GL_BACK;
            break;
        case WINDOW_GL_CULL_FRONT:
            cull_gl = GL_FRONT;
            break;
        default:
            cull_gl = GL_NONE;
            break;
        }
    }

    if (!immediate_gl_draw_mesh_now(window, program_id, layout_id,
                                    gl_prim_to_gl(mode), first, count,
                                    target_id, target_id != 0,
                                    state ? state->depth_test : true,
                                    state ? state->depth_write : true,
                                    cull_gl,
                                    state ? (int)state->blend : (int)WINDOW_GL_BLEND_ALPHA,
                                    instance_count))
    {
        set_error(window, "Immediate mesh draw failed");
        return false;
    }
    return true;
}

int window_gl_get_attrib_location(Window *window, int program_id, const char *name)
{
    GLProgramSlot *slot = get_program_slot(window, program_id);
    if (!slot || !name || !name[0])
        return -1;
    return (int)glGetAttribLocation(slot->program, name);
}