#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <GLES3/gl3.h>

#include "core/window.h"
#include "core/path_util.h"
#include "core/gl_shader_dialect.h"
#include "graphics/gl_shader_pipeline.h"
#include "graphics/gl_render_target.h"
#include "core/gl_present_state.inc"
#include "graphics/image_loader.h"
#include "graphics/gpu_resource_state.h"
#include "graphics/gl_state_guard_gl.inc"
#include "core/input.h"
#include "graphics/skia_wrapper.h"

#define MAX_GL_PROGRAMS 128
#define MAX_GL_PASSES 64
#define MAX_GL_UNIFORM_CACHE 64
#define MAX_GL_RENDER_TARGETS 32
#define MAX_GL_BUFFERS 256
#define MAX_GL_TEXTURES 128
#define MAX_GL_VERTEX_LAYOUTS 64
#define MAX_GL_LAYOUT_ATTRS 8

#define MAX_GL_PASS_UNIFORMS 32
#define MAX_GL_PASS_TEXTURES 8
#define MAX_GL_PENDING_BUFFER_UPDATES 32
#define GL_FRAME_ARENA_SIZE (1u << 20) 

typedef struct
{
    bool in_use;
    char name[64];
    GLint location;
} GLUniformCacheEntry;

typedef enum
{
    GL_PASS_UNIFORM_1I = 0,
    GL_PASS_UNIFORM_1F,
    GL_PASS_UNIFORM_2F,
    GL_PASS_UNIFORM_3F,
    GL_PASS_UNIFORM_4F,
    GL_PASS_UNIFORM_1IV,
    GL_PASS_UNIFORM_1FV,
    GL_PASS_UNIFORM_2FV,
    GL_PASS_UNIFORM_3FV,
    GL_PASS_UNIFORM_4FV,
    GL_PASS_UNIFORM_MAT3FV,
    GL_PASS_UNIFORM_MAT4FV
} GLPassUniformKind;

typedef struct
{
    GLint location;
    GLPassUniformKind kind;
    int count;
    const void *data;
} GLPassUniform;

typedef struct
{
    int texture_unit;
    int kind; 
    int source_id;
    GLint location;
} GLPassTexture;

typedef struct
{
    int buffer_id;
    size_t offset;
    size_t size;
    const void *data;
} GLPassBufferUpdate;

typedef struct
{
    GLPassUniform uniforms[MAX_GL_PASS_UNIFORMS];
    int uniform_count;
    GLPassTexture textures[MAX_GL_PASS_TEXTURES];
    int texture_count;
} GLPendingProgramState;

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
    GLPendingProgramState pending;
} GLProgramSlot;

typedef GpuRenderTargetSlot GLRenderTarget;

typedef struct
{
    bool is_screen;
    int width;
    int height;
    bool has_depth;
} GLBoundRenderTarget;

struct CanvasTexture
{
    SkiaDrawingDesk drawingDesk;
    GLuint texture_id;
    GLuint fbo_id;
    int width;
    int height;
};

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

typedef enum
{
    GL_TEX_BIND_RENDER_TARGET = 0,
    GL_TEX_BIND_TEXTURE_2D = 1,
    GL_TEX_BIND_TEXTURE_CUBE = 2
} GLTextureBindingKind;

typedef enum
{
    GL_PASS_FULLSCREEN = 0,
    GL_PASS_REGION = 1,
    GL_PASS_MESH = 2
} GLPassKind;

typedef struct
{
    int program_id;
    GLPassKind kind;
    int target_id;
    float x, y, w, h;

    int layout_id;
    GLenum mesh_mode;
    int mesh_first;
    int mesh_count;
    int mesh_instance_count;
    bool mesh_depth_test;
    bool mesh_depth_write;
    GLenum mesh_cull;
    int mesh_blend;

    const GLPassUniform *uniforms;
    int uniform_count;
    const GLPassTexture *textures;
    int texture_count;
    const GLPassBufferUpdate *buffer_updates;
    int buffer_update_count;
} GLPass;

struct Window
{
    CanvasTexture *default_canvas;
    SkiaCanvas *skia_gpu_canvas;

    int width;
    int height;
    float dpi_scale;
    bool should_close;

    double start_time;      
    double last_frame_time; 
    double delta_time;      
    double frame_time;      

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl_context;
    bool gl_initialized;
    GLuint canvas_texture;
    GLuint canvas_fbo;
    GLuint blit_program;
    GLuint quad_vao;     
    GLuint quad_vbo_pos; 
    GLuint quad_vbo_tex; 
    GLuint pass_textures[2];
    GLuint pass_fbos[2];

    GLProgramSlot programs[MAX_GL_PROGRAMS];
    GLPass passes[MAX_GL_PASSES];
    int pass_count;

    GLRenderTarget render_targets[MAX_GL_RENDER_TARGETS];

    uint8_t *frame_arena;
    size_t frame_arena_used;

    GLPassBufferUpdate pending_buffer_updates[MAX_GL_PENDING_BUFFER_UPDATES];
    int pending_buffer_update_count;

    GLBufferSlot buffers[MAX_GL_BUFFERS];
    GLTextureSlot textures[MAX_GL_TEXTURES];
    GLVertexLayout vertex_layouts[MAX_GL_VERTEX_LAYOUTS];
    bool screen_rendered_this_frame;

    char project_dir[PATH_MAX];
    char error_msg[512];
};

EM_JS(int, js_hw_get_canvas_width, (void), {
    var dpr = window.devicePixelRatio || 1;
    var el = document.getElementById('canvas');
    return (el ? el.clientWidth : window.innerWidth) * dpr;
});

EM_JS(int, js_hw_get_canvas_height, (void), {
    var dpr = window.devicePixelRatio || 1;
    var el = document.getElementById('canvas');
    return (el ? el.clientHeight : window.innerHeight) * dpr;
});

EM_JS(float, js_hw_get_dpi_scale, (void), {
    return window.devicePixelRatio || 1.0;
});

EM_JS(void, js_hw_set_title, (const char *title), {
    document.title = UTF8ToString(title);
});

EM_JS(void, js_hw_setup_canvas, (int width, int height), {
    var c = document.getElementById('canvas');
    if (c)
    {
        c.width = width;
        c.height = height;
    }
});

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

static bool resolve_project_path(Window *window, const char *relative_path,
                                 char *resolved_path, size_t resolved_size)
{
    int written;

    if (!window || !relative_path || !resolved_path)
        return false;

    if (!path_is_safe_relative(relative_path))
    {
        set_error(window, "Shader paths must be relative and cannot contain '.' or '..' segments");
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

static const char *k_blit_vertex_shader =
    "#version 300 es\n"
    "in vec2 a_position;\n"
    "in vec2 a_texCoord;\n"
    "out vec2 v_texCoord;\n"
    "void main() {\n"
    "  v_texCoord = a_texCoord;\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static const char *k_blit_fragment_shader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_canvas;\n"
    "in vec2 v_texCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  fragColor = texture(u_canvas, v_texCoord);\n"
    "}\n";

static bool init_webgl(Window *window)
{
    EmscriptenWebGLContextAttributes attrs;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;

    if (window->gl_initialized)
        return true;

    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2; 
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.premultipliedAlpha = 0;
    attrs.antialias = 0;
    attrs.preserveDrawingBuffer = 1; 
    attrs.depth = 1;
    attrs.stencil = 8;

    ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0)
    {
        fprintf(stderr, "[budo-web] Failed to create WebGL 2 context (err=%d)\n", (int)ctx);
        return false;
    }

    emscripten_webgl_make_context_current(ctx);
    window->gl_context = ctx;

    {
        GLint max_texture_units = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        if (max_texture_units < 8)
        {
            fprintf(stderr, "[budo-web] Need at least 8 fragment texture units, got %d\n", max_texture_units);
            emscripten_webgl_destroy_context(ctx);
            window->gl_context = 0;
            return false;
        }
    }

    glViewport(0, 0, window->width, window->height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    window->gl_initialized = true;
    printf("[budo-web] WebGL 2 context created\n");
    return true;
}

static void ensure_gl_current(Window *window)
{
    if (window && window->gl_context)
        emscripten_webgl_make_context_current(window->gl_context);
}

static GLuint create_program_from_source(Window *window, const char *vertex_source,
                                         const char *fragment_source, bool require_app_dialect)
{
    const GlShaderPipelineApi api = {
        (uint32_t (*)(uint32_t))glCreateShader,
        (void (*)(uint32_t, int, const char *const *, const int *))glShaderSource,
        (void (*)(uint32_t))glCompileShader,
        (void (*)(uint32_t, uint32_t, int *))glGetShaderiv,
        (void (*)(uint32_t, int, int *, char *))glGetShaderInfoLog,
        (void (*)(uint32_t))glDeleteShader,
        (uint32_t (*)(void))glCreateProgram,
        (void (*)(uint32_t, uint32_t))glAttachShader,
        (void (*)(uint32_t, uint32_t, const char *))glBindAttribLocation,
        (void (*)(uint32_t))glLinkProgram,
        (void (*)(uint32_t, uint32_t, int *))glGetProgramiv,
        (void (*)(uint32_t, int, int *, char *))glGetProgramInfoLog,
        (void (*)(uint32_t))glDeleteProgram};
    const GlShaderPipelineConstants constants = {
        GL_VERTEX_SHADER, GL_FRAGMENT_SHADER,
        GL_COMPILE_STATUS, GL_LINK_STATUS, GL_TRUE};
    ensure_gl_current(window);
    return (GLuint)gl_shader_pipeline_create(
        &api, &constants, BUDO_GLSL_TARGET_ES300,
        vertex_source, fragment_source, require_app_dialect,
        window->error_msg, sizeof(window->error_msg));
}

static bool create_pass_chain_buffers(Window *window)
{
    int i;

    if (!window || window->width <= 0 || window->height <= 0)
        return false;

    ensure_gl_current(window);

    for (i = 0; i < 2; i++)
    {
        if (window->pass_textures[i] == 0)
            glGenTextures(1, &window->pass_textures[i]);
        if (window->pass_textures[i] == 0)
        {
            set_error(window, "Failed to create post-process texture");
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, window->pass_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, window->width, window->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        if (window->pass_fbos[i] == 0)
            glGenFramebuffers(1, &window->pass_fbos[i]);
        if (window->pass_fbos[i] == 0)
        {
            set_error(window, "Failed to create post-process framebuffer");
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, window->pass_fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, window->pass_textures[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            set_error(window, "Post-process framebuffer is not complete");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

static void create_quad_vao(Window *window)
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

    glGenVertexArrays(1, &window->quad_vao);
    glBindVertexArray(window->quad_vao);

    glGenBuffers(1, &window->quad_vbo_pos);
    glBindBuffer(GL_ARRAY_BUFFER, window->quad_vbo_pos);
    glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glGenBuffers(1, &window->quad_vbo_tex);
    glBindBuffer(GL_ARRAY_BUFFER, window->quad_vbo_tex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static bool init_gl_pipeline(Window *window)
{
    if (!init_webgl(window))
        return false;

    ensure_gl_current(window);

    window->blit_program = create_program_from_source(window,
                                                      k_blit_vertex_shader,
                                                      k_blit_fragment_shader,
                                                      false);
    if (window->blit_program == 0)
    {
        fprintf(stderr, "[budo-web] Failed to create blit program: %s\n",
                window->error_msg);
        return false;
    }

    if (!create_pass_chain_buffers(window))
        return false;

    create_quad_vao(window);

    return true;
}

static void sync_window_default_canvas_aliases(Window *window)
{
    if (!window)
        return;
    if (!window->default_canvas)
    {
        window->skia_gpu_canvas = NULL;
        window->canvas_texture = 0;
        window->canvas_fbo = 0;
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

CanvasTexture *window_canvas_texture_create(int width, int height)
{
    CanvasTexture *canvas_texture;
    unsigned int texture_id = 0;
    unsigned int fbo_id = 0;
    SkiaCanvas *canvas;

    if (width <= 0 || height <= 0)
        return NULL;

    canvas_texture = (CanvasTexture *)calloc(1, sizeof(CanvasTexture));
    if (!canvas_texture)
        return NULL;

    canvas = skia_canvas_create_gl_offscreen(width, height, &texture_id, &fbo_id);
    if (!canvas)
    {
        free(canvas_texture);
        return NULL;
    }

    canvas_texture->drawingDesk.canvas = canvas;
    canvas_texture->drawingDesk.active_paint = skia_paint_create();
    canvas_texture->texture_id = texture_id;
    canvas_texture->fbo_id = fbo_id;
    canvas_texture->width = width;
    canvas_texture->height = height;
    return canvas_texture;
}

void window_canvas_texture_destroy(CanvasTexture *canvas_texture)
{
    if (!canvas_texture)
        return;
    if (canvas_texture->drawingDesk.canvas)
        skia_canvas_destroy(canvas_texture->drawingDesk.canvas);
    free(canvas_texture);
}

bool window_canvas_texture_resize(CanvasTexture *canvas_texture, int width, int height)
{
    unsigned int texture_id = 0;
    unsigned int fbo_id = 0;
    SkiaCanvas *canvas;

    if (!canvas_texture || width <= 0 || height <= 0)
        return false;
    if (canvas_texture->width == width && canvas_texture->height == height)
        return true;

    canvas = skia_canvas_create_gl_offscreen(width, height, &texture_id, &fbo_id);
    if (!canvas)
        return false;

    if (canvas_texture->drawingDesk.canvas)
        skia_canvas_destroy(canvas_texture->drawingDesk.canvas);

    canvas_texture->drawingDesk.canvas = canvas;
    canvas_texture->texture_id = texture_id;
    canvas_texture->fbo_id = fbo_id;
    canvas_texture->width = width;
    canvas_texture->height = height;
    return true;
}

SkiaCanvas *window_canvas_texture_get_canvas(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->drawingDesk.canvas : NULL;
}

SkiaDrawingDesk *window_canvas_texture_get_drawingdesk(CanvasTexture *canvas_texture)
{
    return canvas_texture ? &canvas_texture->drawingDesk : NULL;
}

uint32_t window_canvas_texture_get_gl_texture(CanvasTexture *canvas_texture)
{
    return canvas_texture ? (uint32_t)canvas_texture->texture_id : 0;
}

uint32_t window_canvas_texture_get_gl_framebuffer(CanvasTexture *canvas_texture)
{
    return canvas_texture ? (uint32_t)canvas_texture->fbo_id : 0;
}

int window_canvas_texture_get_width(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->width : 0;
}

int window_canvas_texture_get_height(CanvasTexture *canvas_texture)
{
    return canvas_texture ? canvas_texture->height : 0;
}

static void flush_canvas_texture_transition(void *opaque)
{
    CanvasTexture *canvas_texture = (CanvasTexture *)opaque;

    skia_canvas_reset_gl_context(canvas_texture->drawingDesk.canvas);
    skia_canvas_flush(canvas_texture->drawingDesk.canvas);
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

void window_canvas_texture_flush(CanvasTexture *canvas_texture)
{

    if (!canvas_texture || !canvas_texture->drawingDesk.canvas)
        return;

    budo_gl_state_guard_run(flush_canvas_texture_transition, canvas_texture);
}

static GLRenderTarget *get_render_target_slot(Window *window, int target_id)
{
    return window ? (GLRenderTarget *)gpu_resource_slot_lookup(
                        window->render_targets, sizeof(window->render_targets[0]),
                        MAX_GL_RENDER_TARGETS, target_id)
                  : NULL;
}

static void execute_mesh_pass(Window *window, GLPass *pass);

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
        strncpy(slot->uniform_cache[empty_index].name, name,
                sizeof(slot->uniform_cache[empty_index].name) - 1);
        slot->uniform_cache[empty_index].name[sizeof(slot->uniform_cache[empty_index].name) - 1] = '\0';
        slot->uniform_cache[empty_index].location = location;
        return location;
    }

    return glGetUniformLocation(slot->program, name);
}

static void apply_uniform_value(GLint location, GLPassUniformKind kind, int count, const void *data)
{
    const GLint *iv;
    const GLfloat *fv;

    switch (kind)
    {
    case GL_PASS_UNIFORM_1I:
        iv = (const GLint *)data;
        glUniform1i(location, iv[0]);
        break;
    case GL_PASS_UNIFORM_1F:
        fv = (const GLfloat *)data;
        glUniform1f(location, fv[0]);
        break;
    case GL_PASS_UNIFORM_2F:
        fv = (const GLfloat *)data;
        glUniform2f(location, fv[0], fv[1]);
        break;
    case GL_PASS_UNIFORM_3F:
        fv = (const GLfloat *)data;
        glUniform3f(location, fv[0], fv[1], fv[2]);
        break;
    case GL_PASS_UNIFORM_4F:
        fv = (const GLfloat *)data;
        glUniform4f(location, fv[0], fv[1], fv[2], fv[3]);
        break;
    case GL_PASS_UNIFORM_1IV:
        glUniform1iv(location, count, (const GLint *)data);
        break;
    case GL_PASS_UNIFORM_1FV:
        glUniform1fv(location, count, (const GLfloat *)data);
        break;
    case GL_PASS_UNIFORM_2FV:
        glUniform2fv(location, count, (const GLfloat *)data);
        break;
    case GL_PASS_UNIFORM_3FV:
        glUniform3fv(location, count, (const GLfloat *)data);
        break;
    case GL_PASS_UNIFORM_4FV:
        glUniform4fv(location, count, (const GLfloat *)data);
        break;
    case GL_PASS_UNIFORM_MAT3FV:
        glUniformMatrix3fv(location, count, GL_FALSE, (const GLfloat *)data);
        break;
    case GL_PASS_UNIFORM_MAT4FV:
        glUniformMatrix4fv(location, count, GL_FALSE, (const GLfloat *)data);
        break;
    }
}

static bool apply_program_uniform_immediate(Window *window, int program_id, const char *name,
                                            GLPassUniformKind kind, int count,
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

    ensure_gl_current(window);
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
                                GLPassUniformKind kind, int count,
                                const void *data, size_t bytes)
{
    (void)bytes;
    return apply_program_uniform_immediate(window, program_id, name, kind, count, data);
}

static bool upload_buffer_sub_data_immediate(Window *window, GLBufferSlot *buffer,
                                             size_t offset, const void *data, size_t size)
{
    if (!buffer || buffer->target == 0)
    {
        set_error(window, "Buffer has no data uploaded yet");
        return false;
    }

    ensure_gl_current(window);
    glBindBuffer(buffer->target, buffer->id);
    glBufferSubData(buffer->target, (GLintptr)offset, (GLsizeiptr)size, data);
    glBindBuffer(buffer->target, 0);
    return true;
}

static GLBufferSlot *get_buffer_slot(Window *window, int buffer_id);
static GLTextureSlot *get_texture_slot(Window *window, int texture_id);

static void *frame_arena_alloc(Window *window, size_t size, size_t align)
{
    size_t base;
    if (!window || !window->frame_arena || size == 0)
        return NULL;
    if (align == 0)
        align = 1;
    base = (window->frame_arena_used + (align - 1)) & ~(align - 1);
    if (base > GL_FRAME_ARENA_SIZE || size > GL_FRAME_ARENA_SIZE - base)
    {
        set_error(window, "GL frame arena exhausted (too much per-frame uniform / buffer-update data)");
        return NULL;
    }
    window->frame_arena_used = base + size;
    return window->frame_arena + base;
}

static const void *frame_arena_dup(Window *window, const void *data, size_t size)
{
    void *p = frame_arena_alloc(window, size, 16);
    if (!p)
        return NULL;
    memcpy(p, data, size);
    return p;
}

static GLPassUniform *find_or_alloc_pending_uniform(Window *window, GLProgramSlot *slot, GLint location)
{
    int i;
    for (i = 0; i < slot->pending.uniform_count; i++)
    {
        if (slot->pending.uniforms[i].location == location)
            return &slot->pending.uniforms[i];
    }
    if (slot->pending.uniform_count >= MAX_GL_PASS_UNIFORMS)
    {
        set_error(window, "Too many distinct uniforms staged for one program in one frame (max 32)");
        return NULL;
    }
    return &slot->pending.uniforms[slot->pending.uniform_count++];
}

static GLPassTexture *find_or_alloc_pending_texture(Window *window, GLProgramSlot *slot, int texture_unit)
{
    int i;
    for (i = 0; i < slot->pending.texture_count; i++)
    {
        if (slot->pending.textures[i].texture_unit == texture_unit)
            return &slot->pending.textures[i];
    }
    if (slot->pending.texture_count >= MAX_GL_PASS_TEXTURES)
    {
        set_error(window, "Too many texture bindings staged for one program in one frame (max 8)");
        return NULL;
    }
    return &slot->pending.textures[slot->pending.texture_count++];
}

static bool stage_program_uniform(Window *window, int program_id, const char *name,
                                  GLPassUniformKind kind, int count,
                                  const void *data, size_t bytes)
{
    GLProgramSlot *slot = get_program_slot(window, program_id);
    GLint location;
    GLPassUniform *u;
    const void *copy;

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
    ensure_gl_current(window);
    location = get_cached_uniform_location(slot, name);
    if (location < 0)
    {
        snprintf(window->error_msg, sizeof(window->error_msg), "Uniform not found: %s", name);
        return false;
    }
    copy = frame_arena_dup(window, data, bytes);
    if (!copy)
        return false;
    u = find_or_alloc_pending_uniform(window, slot, location);
    if (!u)
        return false;
    u->location = location;
    u->kind = kind;
    u->count = count;
    u->data = copy;
    return true;
}

static bool stage_program_texture(Window *window, int program_id, const char *uniform_name,
                                  int source_id, int texture_unit, int kind)
{
    GLProgramSlot *slot = get_program_slot(window, program_id);
    GLPassTexture *tb;
    GLint location;

    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!uniform_name || !uniform_name[0])
    {
        set_error(window, "Uniform name cannot be empty");
        return false;
    }
    if (texture_unit < 1 || texture_unit > 7)
    {
        set_error(window, "Texture unit must be between 1 and 7 (unit 0 is reserved for u_canvas)");
        return false;
    }
    ensure_gl_current(window);
    location = get_cached_uniform_location(slot, uniform_name);
    tb = find_or_alloc_pending_texture(window, slot, texture_unit);
    if (!tb)
        return false;
    tb->texture_unit = texture_unit;
    tb->kind = kind;
    tb->source_id = source_id;
    tb->location = location;
    return true;
}

static bool consume_state_into_pass(Window *window, GLProgramSlot *slot, GLPass *pass)
{
    pass->uniforms = NULL;
    pass->uniform_count = 0;
    pass->textures = NULL;
    pass->texture_count = 0;
    pass->buffer_updates = NULL;
    pass->buffer_update_count = 0;

    if (slot->pending.uniform_count > 0)
    {
        size_t bytes = sizeof(GLPassUniform) * (size_t)slot->pending.uniform_count;
        GLPassUniform *snap = (GLPassUniform *)frame_arena_alloc(window, bytes, 8);
        if (!snap)
            return false;
        memcpy(snap, slot->pending.uniforms, bytes);
        pass->uniforms = snap;
        pass->uniform_count = slot->pending.uniform_count;
    }
    if (slot->pending.texture_count > 0)
    {
        size_t bytes = sizeof(GLPassTexture) * (size_t)slot->pending.texture_count;
        GLPassTexture *snap = (GLPassTexture *)frame_arena_alloc(window, bytes, 8);
        if (!snap)
            return false;
        memcpy(snap, slot->pending.textures, bytes);
        pass->textures = snap;
        pass->texture_count = slot->pending.texture_count;
    }
    if (window->pending_buffer_update_count > 0)
    {
        size_t bytes = sizeof(GLPassBufferUpdate) * (size_t)window->pending_buffer_update_count;
        GLPassBufferUpdate *snap = (GLPassBufferUpdate *)frame_arena_alloc(window, bytes, 8);
        if (!snap)
            return false;
        memcpy(snap, window->pending_buffer_updates, bytes);
        pass->buffer_updates = snap;
        pass->buffer_update_count = window->pending_buffer_update_count;
        window->pending_buffer_update_count = 0;
    }
    return true;
}

static void apply_pass_buffer_updates(Window *window, const GLPass *pass)
{
    int i;
    for (i = 0; i < pass->buffer_update_count; i++)
    {
        const GLPassBufferUpdate *u = &pass->buffer_updates[i];
        GLBufferSlot *b = get_buffer_slot(window, u->buffer_id);
        if (!b || b->target == 0)
            continue;
        glBindBuffer(b->target, b->id);
        glBufferSubData(b->target, (GLintptr)u->offset, (GLsizeiptr)u->size, u->data);
        glBindBuffer(b->target, 0);
    }
}

static void apply_pass_uniforms(const GLPass *pass)
{
    int i;
    for (i = 0; i < pass->uniform_count; i++)
    {
        const GLPassUniform *u = &pass->uniforms[i];
        const GLint *iv;
        const GLfloat *fv;
        switch (u->kind)
        {
        case GL_PASS_UNIFORM_1I:
            iv = (const GLint *)u->data;
            glUniform1i(u->location, iv[0]);
            break;
        case GL_PASS_UNIFORM_1F:
            fv = (const GLfloat *)u->data;
            glUniform1f(u->location, fv[0]);
            break;
        case GL_PASS_UNIFORM_2F:
            fv = (const GLfloat *)u->data;
            glUniform2f(u->location, fv[0], fv[1]);
            break;
        case GL_PASS_UNIFORM_3F:
            fv = (const GLfloat *)u->data;
            glUniform3f(u->location, fv[0], fv[1], fv[2]);
            break;
        case GL_PASS_UNIFORM_4F:
            fv = (const GLfloat *)u->data;
            glUniform4f(u->location, fv[0], fv[1], fv[2], fv[3]);
            break;
        case GL_PASS_UNIFORM_1IV:
            glUniform1iv(u->location, u->count, (const GLint *)u->data);
            break;
        case GL_PASS_UNIFORM_1FV:
            glUniform1fv(u->location, u->count, (const GLfloat *)u->data);
            break;
        case GL_PASS_UNIFORM_2FV:
            glUniform2fv(u->location, u->count, (const GLfloat *)u->data);
            break;
        case GL_PASS_UNIFORM_3FV:
            glUniform3fv(u->location, u->count, (const GLfloat *)u->data);
            break;
        case GL_PASS_UNIFORM_4FV:
            glUniform4fv(u->location, u->count, (const GLfloat *)u->data);
            break;
        case GL_PASS_UNIFORM_MAT3FV:
            glUniformMatrix3fv(u->location, u->count, GL_FALSE, (const GLfloat *)u->data);
            break;
        case GL_PASS_UNIFORM_MAT4FV:
            glUniformMatrix4fv(u->location, u->count, GL_FALSE, (const GLfloat *)u->data);
            break;
        }
    }
}

static void apply_pass_textures(Window *window, const GLPass *pass)
{
    int j;
    for (j = 0; j < pass->texture_count; j++)
    {
        const GLPassTexture *tb = &pass->textures[j];
        GLuint gl_tex = 0;
        GLenum gl_target = GL_TEXTURE_2D;
        if (tb->kind == GL_TEX_BIND_RENDER_TARGET)
        {
            GLRenderTarget *rt;
            if (tb->source_id <= 0 || tb->source_id > MAX_GL_RENDER_TARGETS)
                continue;
            if (!window->render_targets[tb->source_id - 1].in_use)
                continue;
            rt = &window->render_targets[tb->source_id - 1];
            gl_tex = rt->texture;
        }
        else
        {
            GLTextureSlot *t = get_texture_slot(window, tb->source_id);
            if (!t)
                continue;
            gl_tex = t->id;
            gl_target = t->target;
        }
        glActiveTexture(GL_TEXTURE0 + (GLenum)tb->texture_unit);
        glBindTexture(gl_target, gl_tex);
        if (tb->location >= 0)
            glUniform1i(tb->location, tb->texture_unit);
    }
}

static void capture_framebuffer_to_texture(Window *window)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, window->canvas_fbo);
    glBlitFramebuffer(0, 0, window->width, window->height,
                      0, 0, window->width, window->height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static bool passes_are_default_fullscreen_only(const Window *window)
{
    int i;

    if (!window || window->pass_count <= 0)
        return false;

    for (i = 0; i < window->pass_count; i++)
    {
        const GLPass *pass = &window->passes[i];
        if (pass->kind != GL_PASS_FULLSCREEN || pass->target_id > 0)
            return false;
    }
    return true;
}

static void gl_draw_fullscreen_quad(Window *window, GLuint program, GLProgramSlot *slot, const GLPass *pass, GLuint source_texture)
{
    GLint sampler_loc;
    GLint resolution_loc;
    GLint time_loc;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture != 0 ? source_texture : window->canvas_texture);

    glBindVertexArray(window->quad_vao);
    glUseProgram(program);

    sampler_loc = slot ? slot->sampler_loc : glGetUniformLocation(program, "u_canvas");
    if (sampler_loc >= 0)
        glUniform1i(sampler_loc, 0);

    resolution_loc = slot ? slot->resolution_loc : glGetUniformLocation(program, "u_resolution");
    if (resolution_loc >= 0)
        glUniform2f(resolution_loc, (GLfloat)window->width, (GLfloat)window->height);

    time_loc = slot ? slot->time_loc : glGetUniformLocation(program, "u_time");
    if (time_loc >= 0)
        glUniform1f(time_loc, (GLfloat)window->frame_time);

    if (pass)
    {
        apply_pass_textures(window, pass);
        apply_pass_uniforms(pass);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glUseProgram(0);
    glBindVertexArray(0);
}

static void prepare_screen_from_canvas(Window *window, bool clear_depth)
{
    if (!window || !window->skia_gpu_canvas || window->screen_rendered_this_frame)
        return;

    flush_skia_canvas_discarding_gl_state(window->skia_gpu_canvas);
    BUDO_GL_PREPARE_SCREEN_PRESENT(window->width, window->height);
    gl_draw_fullscreen_quad(window, window->blit_program, NULL, NULL, window->canvas_texture);

    if (clear_depth)
    {
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
    }
}

static void gl_draw_region_quad(Window *window, GLuint program, GLProgramSlot *slot,
                                float rx, float ry, float rw, float rh,
                                int target_id, const GLPass *pass)
{
    GLRenderTarget *rt = NULL;
    GLint sampler_loc;
    GLint resolution_loc;
    GLint time_loc;
    GLint offset_loc;
    int vp_x, vp_y, vp_w, vp_h;
    int j;

    if (target_id > 0)
    {
        rt = get_render_target_slot(window, target_id);
        if (!rt)
            return;
        glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        vp_x = (int)rx;
        vp_y = rt->height - (int)(ry + rh);
        vp_w = (int)rw;
        vp_h = (int)rh;
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        vp_x = (int)rx;
        vp_y = window->height - (int)(ry + rh);
        vp_w = (int)rw;
        vp_h = (int)rh;
    }

    glViewport(vp_x, vp_y, vp_w, vp_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vp_x, vp_y, vp_w, vp_h);

    if (rt && rt->has_depth)
    {
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    if (pass)
        apply_pass_buffer_updates(window, pass);

    glBindVertexArray(window->quad_vao);
    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, window->canvas_texture);

    sampler_loc = slot ? slot->sampler_loc : glGetUniformLocation(program, "u_canvas");
    if (sampler_loc >= 0)
        glUniform1i(sampler_loc, 0);

    resolution_loc = slot ? slot->resolution_loc : glGetUniformLocation(program, "u_resolution");
    if (resolution_loc >= 0)
        glUniform2f(resolution_loc, rw, rh);

    time_loc = slot ? slot->time_loc : glGetUniformLocation(program, "u_time");
    if (time_loc >= 0)
        glUniform1f(time_loc, (GLfloat)window->frame_time);

    offset_loc = glGetUniformLocation(program, "u_offset");
    if (offset_loc >= 0)
        glUniform2f(offset_loc, rx, ry);

    if (pass)
    {
        apply_pass_textures(window, pass);
        apply_pass_uniforms(pass);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    for (j = 1; j < 8; j++)
    {
        glActiveTexture(GL_TEXTURE0 + (GLenum)j);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    glUseProgram(0);
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    if (rt && rt->has_depth)
        glDisable(GL_DEPTH_TEST);

    if (rt)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Window *window_create(const WindowConfig *config)
{
    Window *window;

    if (!config)
        return NULL;

    window = (Window *)calloc(1, sizeof(Window));
    if (!window)
        return NULL;

    window->frame_arena = (uint8_t *)malloc(GL_FRAME_ARENA_SIZE);
    if (!window->frame_arena)
    {
        fprintf(stderr, "[budo-web] Failed to allocate GL frame arena\n");
        free(window);
        return NULL;
    }
    window->frame_arena_used = 0;

    window->dpi_scale = js_hw_get_dpi_scale();

    if (config->width > 0 && config->height > 0)
    {
        window->width = config->width;
        window->height = config->height;
    }
    else
    {
        window->width = js_hw_get_canvas_width();
        window->height = js_hw_get_canvas_height();
    }

    if (window->width <= 0)
        window->width = 800;
    if (window->height <= 0)
        window->height = 600;

    if (config->project_dir)
    {
        strncpy(window->project_dir, config->project_dir, sizeof(window->project_dir) - 1);
        window->project_dir[sizeof(window->project_dir) - 1] = '\0';
    }

    js_hw_setup_canvas(window->width, window->height);

    if (!init_gl_pipeline(window))
    {
        fprintf(stderr, "[budo-web] Failed to create WebGL 2 context\n");
        free(window);
        return NULL;
    }

    ensure_gl_current(window);
    window->default_canvas = window_canvas_texture_create(window->width, window->height);
    sync_window_default_canvas_aliases(window);
    if (!window->default_canvas)
    {
        fprintf(stderr, "[budo-web] Failed to create Skia GL canvas (%dx%d)\n",
                window->width, window->height);
        free(window->frame_arena);
        free(window);
        return NULL;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
    skia_canvas_reset_gl_context(window->skia_gpu_canvas);

    if (config->title)
        js_hw_set_title(config->title);

    window->start_time = emscripten_get_now();
    window->last_frame_time = window->start_time;

    printf("[budo-web] Window created: %dx%d (dpi_scale=%.1f, Skia GPU)\n",
           window->width, window->height, window->dpi_scale);

    return window;
}

void window_destroy(Window *window)
{
    int i;

    if (!window)
        return;

    if (window->gl_initialized)
    {
        ensure_gl_current(window);

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
        for (i = 0; i < MAX_GL_VERTEX_LAYOUTS; i++)
        {
            if (window->vertex_layouts[i].in_use && window->vertex_layouts[i].vao != 0)
                glDeleteVertexArrays(1, &window->vertex_layouts[i].vao);
        }

        if (window->default_canvas)
        {
            window_canvas_texture_destroy(window->default_canvas);
            window->default_canvas = NULL;
            sync_window_default_canvas_aliases(window);
        }
        for (i = 0; i < 2; i++)
        {
            if (window->pass_fbos[i] != 0)
                glDeleteFramebuffers(1, &window->pass_fbos[i]);
            if (window->pass_textures[i] != 0)
                glDeleteTextures(1, &window->pass_textures[i]);
        }
        if (window->blit_program != 0)
            glDeleteProgram(window->blit_program);
        if (window->quad_vao != 0)
            glDeleteVertexArrays(1, &window->quad_vao);
        if (window->quad_vbo_pos != 0)
            glDeleteBuffers(1, &window->quad_vbo_pos);
        if (window->quad_vbo_tex != 0)
            glDeleteBuffers(1, &window->quad_vbo_tex);
    }

    free(window->frame_arena);
    free(window);
}

bool window_poll_events(Window *window, InputState *input)
{
    double current_time;

    if (!window)
        return false;

    current_time = emscripten_get_now();
    window->delta_time = (current_time - window->last_frame_time) / 1000.0;
    window->last_frame_time = current_time;

    if (input)
    {

        input_update_timing(input, window->delta_time);
    }

    {
        int new_w = js_hw_get_canvas_width();
        int new_h = js_hw_get_canvas_height();
        if (new_w > 0 && new_h > 0 &&
            (new_w != window->width || new_h != window->height))
        {
            window_resize(window, new_w, new_h);
        }
    }

    return !window->should_close;
}

SkiaCanvas *window_get_canvas(Window *window)
{
    if (!window)
        return NULL;
    return window->skia_gpu_canvas;
}

void window_present(Window *window)
{
    if (!window || !window->skia_gpu_canvas)
        return;

    if (window->screen_rendered_this_frame)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
        skia_canvas_reset_gl_context(window->skia_gpu_canvas);
        return;
    }

    flush_skia_canvas_discarding_gl_state(window->skia_gpu_canvas);
    skia_canvas_reset_gl_context(window->skia_gpu_canvas);

    ensure_gl_current(window);

    BUDO_GL_PREPARE_SCREEN_PRESENT(window->width, window->height);

    gl_draw_fullscreen_quad(window, window->blit_program, NULL, NULL, window->canvas_texture);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
    skia_canvas_reset_gl_context(window->skia_gpu_canvas);
}

void window_get_size(Window *window, int *width, int *height)
{
    if (!window)
        return;
    if (width)
        *width = window->width;
    if (height)
        *height = window->height;
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
    if (window && title)
        js_hw_set_title(title);
}

double window_get_time(Window *window)
{
    if (!window)
        return 0.0;
    return (emscripten_get_now() - window->start_time) / 1000.0;
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
    window->dpi_scale = js_hw_get_dpi_scale();

    js_hw_setup_canvas(width, height);

    if (window->gl_initialized)
    {
        ensure_gl_current(window);
        glViewport(0, 0, width, height);
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
        create_pass_chain_buffers(window);
        glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
        skia_canvas_reset_gl_context(window->skia_gpu_canvas);
    }
}

void window_begin_frame(Window *window, double time_seconds)
{
    int i;
    if (!window)
        return;
    window->frame_time = time_seconds;
    window->pass_count = 0;
    window->pending_buffer_update_count = 0;
    window->frame_arena_used = 0;
    window->screen_rendered_this_frame = false;
    for (i = 0; i < MAX_GL_PROGRAMS; i++)
    {
        window->programs[i].pending.uniform_count = 0;
        window->programs[i].pending.texture_count = 0;
    }
    if (window->skia_gpu_canvas)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
        skia_canvas_reset_gl_context(window->skia_gpu_canvas);
    }
}

int window_gl_create_program(Window *window, const char *vertex_path, const char *fragment_path)
{
    char vertex_full_path[PATH_MAX];
    char fragment_full_path[PATH_MAX];
    char *vertex_source = NULL;
    char *fragment_source = NULL;
    if (!window)
        return -1;

    if (!window->gl_initialized)
    {
        set_error(window, "WebGL not initialized");
        return -1;
    }

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

    if (!window->gl_initialized)
    {
        set_error(window, "WebGL not initialized");
        return -1;
    }

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

    ensure_gl_current(window);
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
            memset(window->programs[i].uniform_cache, 0,
                   sizeof(window->programs[i].uniform_cache));
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
        if (window)
            set_error(window, "Invalid shader program id");
        return false;
    }

    ensure_gl_current(window);
    glDeleteProgram(slot->program);
    slot->program = 0;
    slot->in_use = false;
    slot->pending.uniform_count = 0;
    slot->pending.texture_count = 0;
    return true;
}

bool window_gl_use_program(Window *window, int program_id)
{
    GLProgramSlot *slot;

    if (!window || !window->gl_initialized)
        return false;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    ensure_gl_current(window);
    glUseProgram(slot->program);
    return true;
}

bool window_gl_bind_screen(Window *window)
{
    if (!window || !window->gl_initialized)
        return false;

    ensure_gl_current(window);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window->width, window->height);
    return true;
}

bool window_gl_bind_render_target_immediate(Window *window, int target_id)
{
    GLRenderTarget *rt;

    if (!window || !window->gl_initialized)
        return false;
    if (target_id <= 0)
        return window_gl_bind_screen(window);

    rt = get_render_target_slot(window, target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    ensure_gl_current(window);
    glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glViewport(0, 0, rt->width, rt->height);
    return true;
}

bool window_gl_draw_fullscreen(Window *window, int program_id)
{
    GLProgramSlot *slot;

    if (!window)
        return false;

    if (!window->gl_initialized)
    {
        set_error(window, "WebGL not initialized");
        return false;
    }

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    flush_skia_canvas_discarding_gl_state(window->skia_gpu_canvas);
    BUDO_GL_PREPARE_SCREEN_PRESENT(window->width, window->height);
    gl_draw_fullscreen_quad(window, slot->program, slot, NULL, window->canvas_texture);
    window->screen_rendered_this_frame = true;
    return true;
}

bool window_gl_draw_fullscreen_immediate(Window *window, int program_id, uint32_t source_texture)
{
    GLProgramSlot *slot = NULL;
    GLuint program = 0;

    if (!window || !window->gl_initialized)
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
    }

    if (program == 0)
    {
        set_error(window, "No shader program is bound");
        return false;
    }

    if (source_texture == 0 && window->skia_gpu_canvas)
        flush_skia_canvas_preserving_gl_state(window->skia_gpu_canvas);
    gl_draw_fullscreen_quad(window, program, slot, NULL,
                            source_texture != 0 ? (GLuint)source_texture : window->canvas_texture);
    window->screen_rendered_this_frame = true;
    return true;
}

bool window_gl_set_uniform_1i(Window *window, int program_id, const char *name, int value)
{
    GLint v = (GLint)value;
    if (!window || !window->gl_initialized)
        return false;
    return set_program_uniform(window, program_id, name, GL_PASS_UNIFORM_1I, 1, &v, sizeof(v));
}

bool window_gl_set_uniform_1f(Window *window, int program_id, const char *name, float value)
{
    GLfloat v = value;
    if (!window || !window->gl_initialized)
        return false;
    return set_program_uniform(window, program_id, name, GL_PASS_UNIFORM_1F, 1, &v, sizeof(v));
}

bool window_gl_set_uniform_2f(Window *window, int program_id, const char *name, float v0, float v1)
{
    GLfloat v[2] = {v0, v1};
    if (!window || !window->gl_initialized)
        return false;
    return set_program_uniform(window, program_id, name, GL_PASS_UNIFORM_2F, 1, v, sizeof(v));
}

bool window_gl_set_uniform_3f(Window *window, int program_id, const char *name, float v0, float v1, float v2)
{
    GLfloat v[3] = {v0, v1, v2};
    if (!window || !window->gl_initialized)
        return false;
    return set_program_uniform(window, program_id, name, GL_PASS_UNIFORM_3F, 1, v, sizeof(v));
}

bool window_gl_set_uniform_4f(Window *window, int program_id, const char *name, float v0, float v1, float v2, float v3)
{
    GLfloat v[4] = {v0, v1, v2, v3};
    if (!window || !window->gl_initialized)
        return false;
    return set_program_uniform(window, program_id, name, GL_PASS_UNIFORM_4F, 1, v, sizeof(v));
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
    (uint32_t (*)(uint32_t))glCheckFramebufferStatus,
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

    if (!window || !window->gl_initialized || width <= 0 || height <= 0)
        return -1;

    ensure_gl_current(window);

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
    GLRenderTarget *rt;

    if (!window || !window->gl_initialized)
        return false;

    rt = get_render_target_slot(window, target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    ensure_gl_current(window);

    gl_render_target_destroy(&render_target_api, rt);
    return true;
}

bool window_gl_resize_render_target(Window *window, int target_id, int width, int height)
{
    GLRenderTarget *rt;
    if (!window || !window->gl_initialized || width <= 0 || height <= 0)
        return false;

    rt = get_render_target_slot(window, target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    ensure_gl_current(window);

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

    if (!window || !window->gl_initialized)
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

    (void)slot;
    return window_gl_draw_region_immediate(window, program_id, x, y, w, h, target_id);
}

bool window_gl_draw_region_immediate(Window *window, int program_id,
                                     float x, float y, float w, float h,
                                     int target_id)
{
    GLProgramSlot *slot;

    if (!window || !window->gl_initialized)
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

    if (target_id <= 0)
        prepare_screen_from_canvas(window, false);
    else if (window->skia_gpu_canvas)
        flush_skia_canvas_preserving_gl_state(window->skia_gpu_canvas);
    gl_draw_region_quad(window, slot->program, slot, x, y, w, h, target_id, NULL);
    if (target_id <= 0)
        window->screen_rendered_this_frame = true;
    return true;
}

bool window_gl_bind_texture(Window *window, int program_id,
                            const char *uniform_name,
                            int render_target_id, int texture_unit)
{
    if (!window || !window->gl_initialized)
        return false;
    if (!get_render_target_slot(window, render_target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }
    return window_gl_bind_texture_immediate(window, program_id, uniform_name,
                                            render_target_id, texture_unit);
}

static bool immediate_bind_sampler_texture(Window *window, int program_id,
                                           const char *uniform_name,
                                           GLuint texture_id, GLenum texture_target,
                                           int texture_unit)
{
    GLProgramSlot *slot;
    GLint uniform_loc;

    if (!window || !window->gl_initialized)
        return false;
    if (!uniform_name || !uniform_name[0])
    {
        set_error(window, "Texture uniform name cannot be empty");
        return false;
    }
    if (texture_unit < 0 || texture_unit > 7)
    {
        set_error(window, "Texture unit must be in range 0..7");
        return false;
    }

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    ensure_gl_current(window);
    glUseProgram(slot->program);
    glActiveTexture(GL_TEXTURE0 + (GLenum)texture_unit);
    glBindTexture(texture_target, texture_id);
    if (texture_target == GL_TEXTURE_2D)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    uniform_loc = glGetUniformLocation(slot->program, uniform_name);
    if (uniform_loc >= 0)
        glUniform1i(uniform_loc, texture_unit);
    glActiveTexture(GL_TEXTURE0);
    return true;
}

bool window_gl_bind_texture_immediate(Window *window, int program_id,
                                      const char *uniform_name,
                                      int render_target_id, int texture_unit)
{
    GLRenderTarget *rt;

    if (!window || !window->gl_initialized)
        return false;
    rt = get_render_target_slot(window, render_target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }
    return immediate_bind_sampler_texture(window, program_id, uniform_name,
                                          rt->texture, GL_TEXTURE_2D, texture_unit);
}

bool window_gl_bind_canvas_texture(Window *window, int program_id,
                                   const char *uniform_name,
                                   CanvasTexture *canvas_texture,
                                   int texture_unit)
{
    return window_gl_bind_canvas_texture_immediate(window, program_id, uniform_name,
                                                   canvas_texture, texture_unit);
}

bool window_gl_bind_canvas_texture_immediate(Window *window, int program_id,
                                             const char *uniform_name,
                                             CanvasTexture *canvas_texture,
                                             int texture_unit)
{
    uint32_t texture_id;

    if (!window || !window->gl_initialized)
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

    return immediate_bind_sampler_texture(window, program_id, uniform_name,
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

static GLenum gl_tex_internal_format(WindowGLTexFormat f)
{
    switch (f)
    {
    case WINDOW_GL_TEX_RGB8:
        return GL_RGB8;
    case WINDOW_GL_TEX_R8:
        return GL_R8;
    default:
        return GL_RGBA8;
    }
}

static GLenum gl_tex_upload_format(WindowGLTexFormat f)
{
    switch (f)
    {
    case WINDOW_GL_TEX_RGB8:
        return GL_RGB;
    case WINDOW_GL_TEX_R8:
        return GL_RED;
    default:
        return GL_RGBA;
    }
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
    if (!window || !window->gl_initialized)
        return -1;
    ensure_gl_current(window);
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
    ensure_gl_current(window);
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
    ensure_gl_current(window);
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
    if (!window || !window->gl_initialized || width <= 0 || height <= 0)
        return -1;
    slot = alloc_texture_slot(window);
    if (slot < 0)
    {
        set_error(window, "Maximum number of textures reached");
        return -1;
    }
    ensure_gl_current(window);
    glGenTextures(1, &id);
    if (id == 0)
    {
        set_error(window, "glGenTextures failed");
        return -1;
    }
    internal_fmt = gl_tex_internal_format(format);
    upload_fmt = gl_tex_upload_format(format);
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

int window_gl_create_texture_cube(Window *window, int size,
                                  WindowGLTexFormat format,
                                  const void *const faces[6])
{
    int slot, i;
    GLuint id = 0;
    GLenum internal_fmt, upload_fmt;
    if (!window || !window->gl_initialized || size <= 0 || !faces)
        return -1;
    slot = alloc_texture_slot(window);
    if (slot < 0)
    {
        set_error(window, "Maximum number of textures reached");
        return -1;
    }
    ensure_gl_current(window);
    glGenTextures(1, &id);
    if (id == 0)
    {
        set_error(window, "glGenTextures failed");
        return -1;
    }
    internal_fmt = gl_tex_internal_format(format);
    upload_fmt = gl_tex_upload_format(format);
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
    ensure_gl_current(window);
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
    ensure_gl_current(window);
    glDeleteTextures(1, &t->id);
    memset(t, 0, sizeof(*t));
    return true;
}

int window_gl_create_vertex_layout(Window *window)
{
    int i;
    if (!window || !window->gl_initialized)
        return -1;
    ensure_gl_current(window);
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
        if (layout->attrs[i].active && layout->attrs[i].location == location)
            break;
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
    ensure_gl_current(window);
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
        if (!window || !window->gl_initialized)                                \
            return false;                                                      \
        if (count < 1)                                                         \
            count = 1;                                                         \
        bytes = sizeof(ctype) * (size_t)(components_per_elem) * (size_t)count; \
        return set_program_uniform(window, program_id, name, kind_enum, count, \
                                   values, bytes);                             \
    }

UNIFORM_ARRAY_SETTER(matrix3fv, float, GL_PASS_UNIFORM_MAT3FV, 9)
UNIFORM_ARRAY_SETTER(matrix4fv, float, GL_PASS_UNIFORM_MAT4FV, 16)
UNIFORM_ARRAY_SETTER(1iv, int, GL_PASS_UNIFORM_1IV, 1)
UNIFORM_ARRAY_SETTER(1fv, float, GL_PASS_UNIFORM_1FV, 1)
UNIFORM_ARRAY_SETTER(2fv, float, GL_PASS_UNIFORM_2FV, 2)
UNIFORM_ARRAY_SETTER(3fv, float, GL_PASS_UNIFORM_3FV, 3)
UNIFORM_ARRAY_SETTER(4fv, float, GL_PASS_UNIFORM_4FV, 4)

#undef UNIFORM_ARRAY_SETTER

static bool bind_texture_common(Window *window, int program_id,
                                const char *uniform_name, int texture_id,
                                int texture_unit, GLTextureBindingKind kind,
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
    (void)kind;
    return immediate_bind_sampler_texture(window, program_id, uniform_name,
                                          t->id, required_target, texture_unit);
}

bool window_gl_bind_texture_2d(Window *window, int program_id,
                               const char *uniform_name,
                               int texture_id, int texture_unit)
{
    return bind_texture_common(window, program_id, uniform_name, texture_id,
                               texture_unit, GL_TEX_BIND_TEXTURE_2D, GL_TEXTURE_2D);
}

bool window_gl_bind_texture_cube(Window *window, int program_id,
                                 const char *uniform_name,
                                 int texture_id, int texture_unit)
{
    return bind_texture_common(window, program_id, uniform_name, texture_id,
                               texture_unit, GL_TEX_BIND_TEXTURE_CUBE, GL_TEXTURE_CUBE_MAP);
}

bool window_gl_bind_texture_2d_immediate(Window *window, int program_id,
                                         const char *uniform_name,
                                         int texture_id, int texture_unit)
{
    return window_gl_bind_texture_2d(window, program_id, uniform_name,
                                     texture_id, texture_unit);
}

bool window_gl_bind_texture_cube_immediate(Window *window, int program_id,
                                           const char *uniform_name,
                                           int texture_id, int texture_unit)
{
    return window_gl_bind_texture_cube(window, program_id, uniform_name,
                                       texture_id, texture_unit);
}

bool window_gl_draw_mesh(Window *window, int program_id, int layout_id,
                         WindowGLPrimitive mode, int first, int count,
                         int target_id,
                         const WindowGLDrawState *state,
                         int instance_count)
{
    if (!window || !window->gl_initialized)
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

bool window_gl_draw_mesh_immediate(Window *window, int program_id, int layout_id,
                                   WindowGLPrimitive mode, int first, int count,
                                   int target_id,
                                   const WindowGLDrawState *state,
                                   int instance_count)
{
    GLPass pass;
    GLenum cull_gl = GL_NONE;

    if (!window || !window->gl_initialized)
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

    memset(&pass, 0, sizeof(pass));
    pass.program_id = program_id;
    pass.kind = GL_PASS_MESH;
    pass.target_id = (target_id > 0) ? target_id : -1;
    pass.layout_id = layout_id;
    pass.mesh_mode = gl_prim_to_gl(mode);
    pass.mesh_first = first;
    pass.mesh_count = count;
    pass.mesh_instance_count = instance_count;
    pass.mesh_depth_test = state ? state->depth_test : true;
    pass.mesh_depth_write = state ? state->depth_write : true;
    pass.mesh_cull = cull_gl;
    pass.mesh_blend = state ? (int)state->blend : (int)WINDOW_GL_BLEND_ALPHA;
    if (target_id <= 0)
        prepare_screen_from_canvas(window, pass.mesh_depth_test);
    execute_mesh_pass(window, &pass);
    if (target_id <= 0)
        window->screen_rendered_this_frame = true;
    return true;
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

static void execute_mesh_pass(Window *window, GLPass *pass)
{
    GLProgramSlot *slot;
    GLVertexLayout *layout;
    GLRenderTarget *rt = NULL;
    int i, j;
    int viewport_w, viewport_h;

    if (!window || !pass)
        return;
    slot = get_program_slot(window, pass->program_id);
    layout = get_layout_slot(window, pass->layout_id);
    if (!slot || !layout)
        return;

    if (pass->target_id > 0)
    {
        rt = get_render_target_slot(window, pass->target_id);
        if (!rt)
            return;
        glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        viewport_w = rt->width;
        viewport_h = rt->height;
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        viewport_w = window->width;
        viewport_h = window->height;
    }
    glViewport(0, 0, viewport_w, viewport_h);

    apply_pass_buffer_updates(window, pass);

    glUseProgram(slot->program);

    {
        GLint loc;
        loc = glGetUniformLocation(slot->program, "u_resolution");
        if (loc >= 0)
            glUniform2f(loc, (GLfloat)viewport_w, (GLfloat)viewport_h);
        loc = glGetUniformLocation(slot->program, "u_time");
        if (loc >= 0)
            glUniform1f(loc, (GLfloat)window->frame_time);
    }

    apply_pass_uniforms(pass);
    apply_pass_textures(window, pass);
    (void)j;

    if (pass->mesh_depth_test)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        if (rt && rt->has_depth)
            glClear(GL_DEPTH_BUFFER_BIT);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(pass->mesh_depth_write ? GL_TRUE : GL_FALSE);

    if (pass->mesh_cull != GL_NONE)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(pass->mesh_cull);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    apply_blend_mode(pass->mesh_blend);

    glBindVertexArray(layout->vao);

    if (layout->needs_rebind)
    {
        for (i = 0; i < MAX_GL_LAYOUT_ATTRS; i++)
        {
            glDisableVertexAttribArray((GLuint)i);
            glVertexAttribDivisor((GLuint)i, 0);
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
            glVertexAttribDivisor((GLuint)a->location, (GLuint)(a->divisor > 0 ? a->divisor : 0));
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
            const void *offset_ptr = (const void *)(intptr_t)((size_t)pass->mesh_first * element_size);
            if (pass->mesh_instance_count > 1)
                glDrawElementsInstanced(pass->mesh_mode, pass->mesh_count,
                                        layout->index_gl_type, offset_ptr,
                                        pass->mesh_instance_count);
            else
                glDrawElements(pass->mesh_mode, pass->mesh_count,
                               layout->index_gl_type, offset_ptr);
        }
    }
    else
    {
        if (pass->mesh_instance_count > 1)
            glDrawArraysInstanced(pass->mesh_mode, pass->mesh_first,
                                  pass->mesh_count, pass->mesh_instance_count);
        else
            glDrawArrays(pass->mesh_mode, pass->mesh_first, pass->mesh_count);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    for (j = 1; j < 8; j++)
    {
        glActiveTexture(GL_TEXTURE0 + (GLenum)j);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(0);
    if (rt)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int window_gl_get_attrib_location(Window *window, int program_id, const char *name)
{
    GLProgramSlot *slot;
    if (!window || !window->gl_initialized)
        return -1;
    slot = get_program_slot(window, program_id);
    if (!slot || !name || !name[0])
        return -1;
    ensure_gl_current(window);
    return (int)glGetAttribLocation(slot->program, name);
}