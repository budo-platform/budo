#include <budo/budo.h>

#include <stdlib.h>

typedef struct AppState
{
    uint64_t frames;
    float x;
    float y;
    uint32_t width;
    uint32_t height;
    BudoPaint *paint;
} AppState;

static BudoStatus app_initialize(BudoHost *host, void **app_state)
{
    AppState *state = calloc(1, sizeof(*state));
    if (!state)
    {
        budo_host_set_error(host, BUDO_STATUS_OUT_OF_MEMORY,
                            "Could not allocate native application state");
        return BUDO_STATUS_OUT_OF_MEMORY;
    }
    *app_state = state;
    budo_host_log(host, BUDO_LOG_INFO, "Native C example initialized");
    return BUDO_STATUS_OK;
}

static void app_surface_created(BudoHost *host, void *app_state)
{
    AppState *state = app_state;
    state->paint = budo_paint_create(host);
    if (!state->paint)
        return;
    budo_paint_set_anti_alias(state->paint, true);
    budo_paint_set_color(state->paint, BUDO_COLOR_RGB(46, 196, 182));
}

static void app_resize(BudoHost *host, void *app_state,
                       const BudoSurfaceInfo *surface)
{
    AppState *state = app_state;
    (void)host;
    state->width = surface->width;
    state->height = surface->height;
    if (state->x == 0.0f && state->y == 0.0f)
    {
        state->x = surface->width * 0.5f;
        state->y = surface->height * 0.5f;
    }
}

static void app_frame(BudoHost *host, void *app_state,
                      const BudoFrameInfo *frame)
{
    AppState *state = app_state;
    BudoPointer pointer;
    BudoCanvas *canvas = budo_host_canvas(host);
    float speed = 240.0f * (float)frame->delta_seconds;

    state->frames = frame->frame_index + 1;
    if (budo_input_pointer(frame->input, &pointer))
    {
        if (pointer.down[BUDO_POINTER_LEFT] || pointer.x || pointer.y)
        {
            state->x = (float)pointer.x;
            state->y = (float)pointer.y;
        }
        budo_paint_set_color(state->paint,
                             pointer.down[BUDO_POINTER_LEFT]
                                 ? BUDO_COLOR_RGB(255, 107, 107)
                                 : BUDO_COLOR_RGB(46, 196, 182));
    }
    if (budo_input_key_down(frame->input, BUDO_KEY_LEFT))
        state->x -= speed;
    if (budo_input_key_down(frame->input, BUDO_KEY_RIGHT))
        state->x += speed;
    if (budo_input_key_down(frame->input, BUDO_KEY_UP))
        state->y -= speed;
    if (budo_input_key_down(frame->input, BUDO_KEY_DOWN))
        state->y += speed;

    if (!canvas || !state->paint)
        return;
    budo_canvas_clear(canvas, BUDO_COLOR_RGB(17, 24, 39));
    budo_canvas_draw_circle(canvas, state->x, state->y, 36.0f, state->paint);
    budo_paint_set_style(state->paint, BUDO_PAINT_STROKE);
    budo_paint_set_stroke_width(state->paint, 3.0f);
    budo_canvas_draw_round_rect(canvas, 20.0f, 20.0f,
                                (float)state->width - 40.0f,
                                (float)state->height - 40.0f,
                                12.0f, 12.0f, state->paint);
    budo_paint_set_style(state->paint, BUDO_PAINT_FILL);
}

static void app_context_lost(BudoHost *host, void *app_state)
{
    AppState *state = app_state;
    (void)host;
    budo_paint_destroy(state->paint);
    state->paint = NULL;
}

static void app_shutdown(BudoHost *host, void *app_state)
{
    AppState *state = app_state;
    budo_host_log(host, BUDO_LOG_INFO, "Native C example shut down");
    budo_paint_destroy(state->paint);
    free(state);
}

static const BudoApplication application = {
    .struct_size = sizeof(BudoApplication),
    .api_version = BUDO_NATIVE_API_VERSION,
    .sdk_version = BUDO_VERSION_STRING,
    .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID,
    .name = "Native C Example",
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