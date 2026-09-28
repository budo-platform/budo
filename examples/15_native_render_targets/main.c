#include <budo/budo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AppState
{
    BudoShaderProgram *program;
    BudoRenderTarget *target_a;
    BudoRenderTarget *target_b;
    BudoPaint *paint;
    uint32_t target_width;
    uint32_t target_height;
} AppState;

static const char vertex_shader[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 a_position;\n"
    "in vec2 a_texCoord;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "  v_uv = a_texCoord;\n"
    "}\n";

static const char fragment_shader[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D u_canvas;\n"
    "uniform vec2 u_resolution;\n"
    "in vec2 v_uv;\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "  vec4 color = texture(u_canvas, v_uv);\n"
    "  float vignette = smoothstep(0.8, 0.2, length(v_uv - 0.5));\n"
    "  out_color = vec4(color.rgb * (0.75 + 0.25 * vignette), color.a);\n"
    "}\n";

static void log_last_error(BudoHost *host, const char *operation)
{
    const BudoError *error = budo_host_last_error(host);
    char message[640];
    snprintf(message, sizeof(message), "%s: %s", operation,
             error && error->message ? error->message : "unknown error");
    budo_host_log(host, BUDO_LOG_ERROR, message);
}

static void release_graphics(AppState *state)
{
    if (state->target_b)
        budo_render_target_destroy(state->target_b);
    if (state->target_a)
        budo_render_target_destroy(state->target_a);
    if (state->program)
        budo_shader_program_destroy(state->program);
    if (state->paint)
        budo_paint_destroy(state->paint);
    state->target_b = NULL;
    state->target_a = NULL;
    state->program = NULL;
    state->paint = NULL;
}

static BudoStatus app_initialize(BudoHost *host, void **app_state)
{
    BudoGraphicsCapabilities capabilities = {0};
    AppState *state = calloc(1, sizeof(*state));
    BudoGraphicsFeatureFlags required =
        BUDO_GRAPHICS_FEATURE_SHADER_PROGRAMS |
        BUDO_GRAPHICS_FEATURE_RENDER_TARGET_RGBA8 |
        BUDO_GRAPHICS_FEATURE_FULLSCREEN_PASSES;
    if (!state)
        return BUDO_STATUS_OUT_OF_MEMORY;
    capabilities.struct_size = sizeof(capabilities);
    if (!budo_host_graphics_capabilities(host, &capabilities) ||
        (capabilities.feature_flags & required) != required)
    {
        free(state);
        budo_host_set_error(host, BUDO_STATUS_UNSUPPORTED,
                            "Native render-target fullscreen passes are unavailable");
        return BUDO_STATUS_UNSUPPORTED;
    }
    *app_state = state;
    return BUDO_STATUS_OK;
}

static void app_surface_created(BudoHost *host, void *app_state)
{
    AppState *state = app_state;
    state->program = budo_shader_program_create(
        host, vertex_shader, strlen(vertex_shader),
        fragment_shader, strlen(fragment_shader));
    state->target_a = budo_render_target_create_rgba8(host, 1, 1);
    state->target_b = budo_render_target_create_rgba8(host, 1, 1);
    state->paint = budo_paint_create(host);
    if (!state->program || !state->target_a || !state->target_b || !state->paint)
    {
        log_last_error(host, "Creating render-target resources failed");
        release_graphics(state);
    }
}

static void app_resize(BudoHost *host, void *app_state,
                       const BudoSurfaceInfo *surface)
{
    AppState *state = app_state;
    uint32_t width = surface->width > 1 ? surface->width / 2 : 1;
    uint32_t height = surface->height > 1 ? surface->height / 2 : 1;
    if (!state->target_a || !state->target_b ||
        (width == state->target_width && height == state->target_height))
        return;
    if (budo_render_target_resize(state->target_a, width, height) != BUDO_STATUS_OK ||
        budo_render_target_resize(state->target_b, width, height) != BUDO_STATUS_OK)
    {
        log_last_error(host, "Resizing render targets failed");
        return;
    }
    state->target_width = width;
    state->target_height = height;
}

static void app_frame(BudoHost *host, void *app_state,
                      const BudoFrameInfo *frame)
{
    AppState *state = app_state;
    BudoCanvas *canvas = budo_host_canvas(host);
    float width = (float)frame->surface.width;
    float height = (float)frame->surface.height;
    if (!canvas || !state->program || !state->target_a ||
        !state->target_b || !state->paint)
        return;

    budo_canvas_clear(canvas, BUDO_COLOR_RGB(15, 23, 42));
    budo_paint_set_color(state->paint, BUDO_COLOR_RGB(239, 68, 68));
    budo_canvas_draw_rect(canvas, 0, 0, width * 0.55f, height * 0.45f,
                          state->paint);
    budo_paint_set_color(state->paint, BUDO_COLOR_RGB(45, 212, 191));
    budo_canvas_draw_circle(canvas, width * 0.72f, height * 0.68f,
                            height * 0.18f, state->paint);
    budo_paint_set_color(state->paint, BUDO_COLOR_RGB(250, 204, 21));
    budo_canvas_draw_rect(canvas, width * 0.08f, height * 0.72f,
                          width * 0.24f, height * 0.12f, state->paint);

    if (budo_canvas_draw_fullscreen_pass(canvas, state->program, NULL,
                                         state->target_a) != BUDO_STATUS_OK ||
        budo_canvas_draw_fullscreen_pass(canvas, state->program, state->target_a,
                                         state->target_b) != BUDO_STATUS_OK ||
        budo_canvas_draw_fullscreen_pass(canvas, state->program, state->target_b,
                                         NULL) != BUDO_STATUS_OK)
        log_last_error(host, "Drawing fullscreen pass chain failed");
}

static void app_context_lost(BudoHost *host, void *app_state)
{
    (void)host;
    release_graphics(app_state);
}

static void app_shutdown(BudoHost *host, void *app_state)
{
    (void)host;
    free(app_state);
}

static const BudoApplication application = {
    .struct_size = sizeof(BudoApplication),
    .api_version = BUDO_NATIVE_API_VERSION,
    .sdk_version = BUDO_VERSION_STRING,
    .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID,
    .name = "Native Render Targets Example",
    .initialize = app_initialize,
    .surface_created = app_surface_created,
    .resize = app_resize,
    .frame = app_frame,
    .context_lost = app_context_lost,
    .shutdown = app_shutdown,
};

const BudoApplication *budo_get_application(void)
{
    return &application;
}