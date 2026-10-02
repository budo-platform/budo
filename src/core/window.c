#include "window.h"
#include "core/path_util.h"
#include "core/gl_shader_dialect.h"
#include "graphics/gl_shader_pipeline.h"
#include "graphics/gl_render_target.h"
#include "graphics/image_loader.h"
#include "graphics/gpu_resource_state.h"

#include <SDL2/SDL.h>
#include "core/gl_desktop.h"

#include "core/gl_present_state.inc"
#include "graphics/gl_state_guard_gl.inc"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graphics/gl_window_types.inc"

static bool immediate_bind_sampler_texture(Window *window, int program_id,
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

#define BUDO_GL_WINDOW_READY(window) ((window) != NULL)
#define BUDO_GL_WINDOW_MAKE_CURRENT(window) ((void)(window))
#include "graphics/gl_window_resources.inc"

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

static void immediate_gl_use_program(GLuint program)
{
    glUseProgram(program);
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

static void prepare_screen_for_mesh(Window *window)
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

    if (!budo_gl_desktop_load())
    {
        SDL_GL_DeleteContext(window->gl_context);
        SDL_DestroyWindow(window->sdl_window);
        free(window);
        SDL_Quit();
        return NULL;
    }

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

static GLenum gl_tex_internal_format(WindowGLTexFormat f)
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

static GLenum gl_tex_upload_format(WindowGLTexFormat f)
{
    return gl_tex_internal_format(f);
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