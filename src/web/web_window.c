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

#include "graphics/gl_window_types.inc"

#define MAX_GL_PASS_UNIFORMS 32
#define MAX_GL_PASS_TEXTURES 8

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

    GLProgramSlot programs[MAX_GL_PROGRAMS];

    GLRenderTarget render_targets[MAX_GL_RENDER_TARGETS];

    GLBufferSlot buffers[MAX_GL_BUFFERS];
    GLTextureSlot textures[MAX_GL_TEXTURES];
    GLVertexLayout vertex_layouts[MAX_GL_VERTEX_LAYOUTS];
    bool screen_rendered_this_frame;

    char project_dir[PATH_MAX];
    char error_msg[512];
};

static void ensure_gl_current(Window *window);
#define BUDO_GL_WINDOW_READY(window) ((window) && (window)->gl_initialized)
#define BUDO_GL_WINDOW_MAKE_CURRENT(window) ensure_gl_current(window)
#include "graphics/gl_window_resources.inc"

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

static void gl_draw_fullscreen_quad(Window *window, GLuint program, GLProgramSlot *slot, GLuint source_texture)
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
    gl_draw_fullscreen_quad(window, window->blit_program, NULL, window->canvas_texture);

    if (clear_depth)
    {
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
    }
}

static void prepare_screen_for_mesh(Window *window)
{
    prepare_screen_from_canvas(window, true);
}

static void gl_draw_region_quad(Window *window, GLuint program, GLProgramSlot *slot,
                                float rx, float ry, float rw, float rh,
                                int target_id)
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
        if (window->blit_program != 0)
            glDeleteProgram(window->blit_program);
        if (window->quad_vao != 0)
            glDeleteVertexArrays(1, &window->quad_vao);
        if (window->quad_vbo_pos != 0)
            glDeleteBuffers(1, &window->quad_vbo_pos);
        if (window->quad_vbo_tex != 0)
            glDeleteBuffers(1, &window->quad_vbo_tex);
    }

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

    gl_draw_fullscreen_quad(window, window->blit_program, NULL, window->canvas_texture);
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
        glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
        skia_canvas_reset_gl_context(window->skia_gpu_canvas);
    }
}

void window_begin_frame(Window *window, double time_seconds)
{
    if (!window)
        return;
    window->frame_time = time_seconds;
    window->screen_rendered_this_frame = false;
    if (window->skia_gpu_canvas)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, window->canvas_fbo);
        skia_canvas_reset_gl_context(window->skia_gpu_canvas);
    }
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
    gl_draw_fullscreen_quad(window, slot->program, slot, window->canvas_texture);
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
    gl_draw_fullscreen_quad(window, program, slot,
                            source_texture != 0 ? (GLuint)source_texture : window->canvas_texture);
    window->screen_rendered_this_frame = true;
    return true;
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
    gl_draw_region_quad(window, slot->program, slot, x, y, w, h, target_id);
    if (target_id <= 0)
        window->screen_rendered_this_frame = true;
    return true;
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