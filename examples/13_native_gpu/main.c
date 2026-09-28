#include <budo/budo.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vertex
{
    float x;
    float y;
    float u;
    float v;
} Vertex;

typedef struct AppState
{
    BudoShaderProgram *program;
    BudoGpuBuffer *vertices;
    BudoGpuBuffer *indices;
    BudoTexture *texture;
    BudoMesh *mesh;
    bool gpu_available;
} AppState;

static const char vertex_shader[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 a_position;\n"
    "in vec2 a_uv;\n"
    "uniform float u_angle;\n"
    "uniform float u_aspect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  float c = cos(u_angle);\n"
    "  float s = sin(u_angle);\n"
    "  vec2 p = mat2(c, -s, s, c) * a_position;\n"
    "  p.x /= u_aspect;\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char fragment_shader[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_texture;\n"
    "out vec4 out_color;\n"
    "void main() { out_color = texture(u_texture, v_uv); }\n";

static void log_last_error(BudoHost *host, const char *operation)
{
    const BudoError *error = budo_host_last_error(host);
    char message[640];
    snprintf(message, sizeof(message), "%s: %s", operation,
             error && error->message ? error->message : "unknown error");
    budo_host_log(host, BUDO_LOG_ERROR, message);
}

static void release_gpu(AppState *state)
{
    if (state->mesh)
        budo_mesh_destroy(state->mesh);
    if (state->texture)
        budo_texture_destroy(state->texture);
    if (state->indices)
        budo_gpu_buffer_destroy(state->indices);
    if (state->vertices)
        budo_gpu_buffer_destroy(state->vertices);
    if (state->program)
        budo_shader_program_destroy(state->program);
    state->mesh = NULL;
    state->texture = NULL;
    state->indices = NULL;
    state->vertices = NULL;
    state->program = NULL;
}

static BudoStatus app_initialize(BudoHost *host, void **app_state)
{
    BudoGraphicsCapabilities capabilities = {0};
    AppState *state = calloc(1, sizeof(*state));
    if (!state)
        return BUDO_STATUS_OUT_OF_MEMORY;
    capabilities.struct_size = sizeof(capabilities);
    state->gpu_available =
        budo_host_graphics_capabilities(host, &capabilities) &&
        (capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_MESH_DRAWING) != 0;
    if (!state->gpu_available)
    {
        free(state);
        budo_host_set_error(host, BUDO_STATUS_UNSUPPORTED,
                            "Native GPU mesh drawing is unavailable");
        return BUDO_STATUS_UNSUPPORTED;
    }
    *app_state = state;
    return BUDO_STATUS_OK;
}

static void app_surface_created(BudoHost *host, void *app_state)
{
    AppState *state = app_state;
    static const Vertex vertices[] = {
        {-0.65f, -0.65f, 0.0f, 1.0f},
        {0.65f, -0.65f, 1.0f, 1.0f},
        {0.65f, 0.65f, 1.0f, 0.0f},
        {-0.65f, 0.65f, 0.0f, 0.0f},
    };
    static const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    static const uint8_t pixels[] = {
        255,
        107,
        107,
        255,
        46,
        196,
        182,
        255,
        46,
        196,
        182,
        255,
        255,
        209,
        102,
        255,
    };
    int32_t position_location;
    int32_t uv_location;

    state->program = budo_shader_program_create(
        host, vertex_shader, strlen(vertex_shader),
        fragment_shader, strlen(fragment_shader));
    state->vertices = budo_gpu_buffer_create(
        host, BUDO_GPU_BUFFER_VERTEX, vertices, sizeof(vertices),
        BUDO_GPU_USAGE_STATIC);
    state->indices = budo_gpu_buffer_create(
        host, BUDO_GPU_BUFFER_INDEX, indices, sizeof(indices),
        BUDO_GPU_USAGE_STATIC);
    state->texture = budo_texture_2d_create_rgba8(
        host, 2, 2, pixels, sizeof(pixels));
    state->mesh = budo_mesh_create(host);
    if (!state->program || !state->vertices || !state->indices ||
        !state->texture || !state->mesh ||
        budo_shader_program_attribute_location(
            state->program, "a_position", &position_location) != BUDO_STATUS_OK ||
        budo_shader_program_attribute_location(
            state->program, "a_uv", &uv_location) != BUDO_STATUS_OK ||
        budo_mesh_set_vertex_buffer(
            state->mesh, (uint32_t)position_location, state->vertices, 2,
            BUDO_VERTEX_ATTRIBUTE_FLOAT32, sizeof(Vertex), 0) != BUDO_STATUS_OK ||
        budo_mesh_set_vertex_buffer(
            state->mesh, (uint32_t)uv_location, state->vertices, 2,
            BUDO_VERTEX_ATTRIBUTE_FLOAT32, sizeof(Vertex),
            offsetof(Vertex, u)) != BUDO_STATUS_OK ||
        budo_mesh_set_index_buffer(state->mesh, state->indices,
                                   BUDO_INDEX_UINT16) != BUDO_STATUS_OK)
    {
        log_last_error(host, "Creating GPU resources failed");
        release_gpu(state);
    }
}

static void app_frame(BudoHost *host, void *app_state,
                      const BudoFrameInfo *frame)
{
    AppState *state = app_state;
    BudoCanvas *canvas = budo_host_canvas(host);
    BudoMeshDrawInfo draw = {0};
    float aspect;
    if (!canvas || !state->program || !state->mesh)
        return;
    aspect = frame->surface.height > 0
                 ? (float)frame->surface.width / (float)frame->surface.height
                 : 1.0f;
    budo_canvas_clear(canvas, BUDO_COLOR_RGB(17, 24, 39));
    budo_shader_program_set_uniform_1f(
        state->program, "u_angle", (float)(frame->timestamp_ms * 0.0005));
    budo_shader_program_set_uniform_1f(state->program, "u_aspect", aspect);
    budo_shader_program_bind_texture(state->program, "u_texture",
                                     state->texture, 1);
    draw.struct_size = sizeof(draw);
    draw.primitive = BUDO_PRIMITIVE_TRIANGLES;
    draw.count = 6;
    draw.depth_test = false;
    draw.depth_write = false;
    draw.cull = BUDO_CULL_NONE;
    draw.blend = BUDO_BLEND_ALPHA;
    if (budo_canvas_draw_mesh(canvas, state->program, state->mesh, &draw) !=
        BUDO_STATUS_OK)
        log_last_error(host, "Drawing GPU mesh failed");
}

static void app_context_lost(BudoHost *host, void *app_state)
{
    (void)host;
    release_gpu(app_state);
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
    .name = "Native GPU Example",
    .initialize = app_initialize,
    .surface_created = app_surface_created,
    .frame = app_frame,
    .context_lost = app_context_lost,
    .shutdown = app_shutdown,
};

const BudoApplication *budo_get_application(void)
{
    return &application;
}