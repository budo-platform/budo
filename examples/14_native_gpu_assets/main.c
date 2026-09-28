#include <budo/budo.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct Vertex
{
    float x, y, u, v;
} Vertex;
typedef struct AppState
{
    BudoShaderProgram *program;
    BudoGpuBuffer *vertices;
    BudoGpuBuffer *indices;
    BudoTexture *texture;
    BudoMesh *mesh;
} AppState;

static const uint8_t image_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

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
    if (!budo_host_graphics_capabilities(host, &capabilities) ||
        !(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_SHADER_FILES) ||
        !(capabilities.feature_flags & BUDO_GRAPHICS_FEATURE_ENCODED_IMAGE_TEXTURES))
    {
        free(state);
        budo_host_set_error(host, BUDO_STATUS_UNSUPPORTED,
                            "Shader assets or encoded image textures are unavailable");
        return BUDO_STATUS_UNSUPPORTED;
    }
    *app_state = state;
    return BUDO_STATUS_OK;
}

static void app_surface_created(BudoHost *host, void *app_state)
{
    AppState *state = app_state;
    static const Vertex vertices[] = {
        {-0.65f, -0.65f, 0, 1}, {0.65f, -0.65f, 1, 1}, {0.65f, 0.65f, 1, 0}, {-0.65f, 0.65f, 0, 0}};
    static const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    int32_t position, uv;
    state->program = budo_shader_program_create_from_files(
        host, "shader.vert", "shader.frag");
    state->texture = budo_texture_2d_create_from_encoded_data(
        host, image_png, sizeof(image_png));
    state->vertices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_VERTEX,
                                             vertices, sizeof(vertices), BUDO_GPU_USAGE_STATIC);
    state->indices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_INDEX,
                                            indices, sizeof(indices), BUDO_GPU_USAGE_STATIC);
    state->mesh = budo_mesh_create(host);
    if (!state->program || !state->texture || !state->vertices ||
        !state->indices || !state->mesh ||
        budo_shader_program_attribute_location(state->program, "a_position", &position) ||
        budo_shader_program_attribute_location(state->program, "a_uv", &uv) ||
        budo_mesh_set_vertex_buffer(state->mesh, (uint32_t)position,
                                    state->vertices, 2, BUDO_VERTEX_ATTRIBUTE_FLOAT32,
                                    sizeof(Vertex), 0) ||
        budo_mesh_set_vertex_buffer(state->mesh, (uint32_t)uv,
                                    state->vertices, 2, BUDO_VERTEX_ATTRIBUTE_FLOAT32,
                                    sizeof(Vertex), offsetof(Vertex, u)) ||
        budo_mesh_set_index_buffer(state->mesh, state->indices,
                                   BUDO_INDEX_UINT16))
        release_gpu(state);
}

static void app_frame(BudoHost *host, void *app_state, const BudoFrameInfo *frame)
{
    AppState *state = app_state;
    BudoCanvas *canvas = budo_host_canvas(host);
    BudoMeshDrawInfo draw = {0};
    float aspect;
    if (!canvas || !state->program || !state->mesh)
        return;
    aspect = frame->surface.height ? (float)frame->surface.width /
                                         (float)frame->surface.height
                                   : 1.0f;
    budo_canvas_clear(canvas, BUDO_COLOR_RGB(17, 24, 39));
    budo_shader_program_set_uniform_1f(state->program, "u_angle",
                                       (float)(frame->timestamp_ms * 0.0005));
    budo_shader_program_set_uniform_1f(state->program, "u_aspect", aspect);
    budo_shader_program_bind_texture(state->program, "u_texture", state->texture, 1);
    draw.struct_size = sizeof(draw);
    draw.primitive = BUDO_PRIMITIVE_TRIANGLES;
    draw.count = 6;
    draw.blend = BUDO_BLEND_ALPHA;
    budo_canvas_draw_mesh(canvas, state->program, state->mesh, &draw);
}

static void app_context_lost(BudoHost *host, void *state)
{
    (void)host;
    release_gpu(state);
}
static void app_shutdown(BudoHost *host, void *state)
{
    (void)host;
    free(state);
}

static const BudoApplication application = {
    .struct_size = sizeof(BudoApplication), .api_version = BUDO_NATIVE_API_VERSION, .sdk_version = BUDO_VERSION_STRING, .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID, .name = "Native GPU Assets Example", .initialize = app_initialize, .surface_created = app_surface_created, .frame = app_frame, .context_lost = app_context_lost, .shutdown = app_shutdown};
const BudoApplication *budo_get_application(void) { return &application; }