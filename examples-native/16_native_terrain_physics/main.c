#include <budo/budo.h>

#include "math3d.h"
#include "physics.h"
#include "terrain.h"
#include "ui_gpu.h"
#include "car_models.h"

#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define TERRAIN_RECENTER_STEP 12.0f
#define MAX_CARS 4
#define MAX_SCENE_VERTICES 8192
#define PALM_COUNT 36

typedef enum CameraMode
{
    CAMERA_ORBIT,
    CAMERA_FOLLOW,
    CAMERA_FPV
} CameraMode;
typedef CarModelVertex CarVertex;
typedef struct ColorVertex
{
    float position[3], color[4];
} ColorVertex;
typedef struct GpuMesh
{
    BudoGpuBuffer *vertices, *indices;
    BudoMesh *mesh;
} GpuMesh;
typedef struct DynamicMesh
{
    BudoShaderProgram *program;
    BudoGpuBuffer *buffer;
    BudoMesh *mesh;
    ColorVertex data[MAX_SCENE_VERTICES];
    uint32_t count;
} DynamicMesh;
typedef struct Palm
{
    float x, y, z, rotation, scale;
} Palm;
typedef struct RallyTarget
{
    float x, y, z, vx, vy, road_x, road_speed;
    int sign;
} RallyTarget;
typedef struct Lighting
{
    float sky[3], fog[3], light[3], direction[3], ambient, night;
} Lighting;

typedef struct AppState
{
    BudoShaderProgram *terrain_program, *car_program;
    GpuMesh terrain_mesh, object_mesh, vehicle_meshes[MAX_CARS];
    size_t vehicle_index_counts[MAX_CARS];
    size_t vehicle_model_indices[MAX_CARS];
    DynamicMesh lines, triangles;
    UiGpu ui;
    TerrainVertex terrain_vertices[TERRAIN_VERTEX_COUNT];
    uint16_t terrain_indices[TERRAIN_INDEX_COUNT];
    PhysicsSettings physics;
    Car cars[MAX_CARS];
    int car_count;
    CarForces player_forces;
    Palm palms[PALM_COUNT];
    int palm_center_x, palm_center_y;
    RallyTarget rally;
    CameraMode camera_mode;
    float camera_rotation, follow_rotation, follow_offset, camera_pitch, camera_distance;
    float terrain_center_x, terrain_center_y;
    float joystick_x, joystick_y, joystick_dx, joystick_dy, joystick_radius;
    int joystick_touch_id;
    int selected_parameter;
    bool show_target, graphics_ready, last_pointer_down, mobile;
    double smoothed_fps;
} AppState;

static const CarVertex car_vertices[] = {
    {{-.5f, -.5f, -.5f}, {0, -1, 0}}, {{.5f, -.5f, -.5f}, {0, -1, 0}}, {{.5f, -.5f, .5f}, {0, -1, 0}}, {{-.5f, -.5f, .5f}, {0, -1, 0}}, {{.5f, .5f, -.5f}, {0, 1, 0}}, {{-.5f, .5f, -.5f}, {0, 1, 0}}, {{-.5f, .5f, .5f}, {0, 1, 0}}, {{.5f, .5f, .5f}, {0, 1, 0}}, {{-.5f, .5f, -.5f}, {-1, 0, 0}}, {{-.5f, -.5f, -.5f}, {-1, 0, 0}}, {{-.5f, -.5f, .5f}, {-1, 0, 0}}, {{-.5f, .5f, .5f}, {-1, 0, 0}}, {{.5f, -.5f, -.5f}, {1, 0, 0}}, {{.5f, .5f, -.5f}, {1, 0, 0}}, {{.5f, .5f, .5f}, {1, 0, 0}}, {{.5f, -.5f, .5f}, {1, 0, 0}}, {{-.5f, -.5f, .5f}, {0, 0, 1}}, {{.5f, -.5f, .5f}, {0, 0, 1}}, {{.5f, .5f, .5f}, {0, 0, 1}}, {{-.5f, .5f, .5f}, {0, 0, 1}}, {{-.5f, .5f, -.5f}, {0, 0, -1}}, {{.5f, .5f, -.5f}, {0, 0, -1}}, {{.5f, -.5f, -.5f}, {0, 0, -1}}, {{-.5f, -.5f, -.5f}, {0, 0, -1}}};
static const uint16_t car_indices[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
static const char color_vs[] = "#version 300 es\nprecision highp float;\nin vec3 a_position;in vec4 a_color;uniform mat4 u_mvp;out vec4 v_color;void main(){v_color=a_color;gl_Position=u_mvp*vec4(a_position,1.0);}\n";
static const char color_fs[] = "#version 300 es\nprecision highp float;\nin vec4 v_color;out vec4 out_color;void main(){out_color=v_color;}\n";

static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 0x27d4eb2du ^ (uint32_t)y * 0x165667b1u ^ 0x9e3779b1u;
    h = (h ^ (h >> 15)) * 0x85ebca6bu;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    return h ^ (h >> 16);
}
static float rand2(int x, int y) { return (float)hash2(x, y) / 4294967296.0f; }
static void log_error(BudoHost *host, const char *where)
{
    const BudoError *e = budo_host_last_error(host);
    char m[640];
    snprintf(m, sizeof(m), "%s: %s", where, e && e->message ? e->message : "unknown");
    budo_host_log(host, BUDO_LOG_ERROR, m);
}
static void release_mesh(GpuMesh *m)
{
    if (m->mesh)
        budo_mesh_destroy(m->mesh);
    if (m->indices)
        budo_gpu_buffer_destroy(m->indices);
    if (m->vertices)
        budo_gpu_buffer_destroy(m->vertices);
    memset(m, 0, sizeof(*m));
}
static void release_dynamic(DynamicMesh *m)
{
    if (m->mesh)
        budo_mesh_destroy(m->mesh);
    if (m->buffer)
        budo_gpu_buffer_destroy(m->buffer);
    if (m->program)
        budo_shader_program_destroy(m->program);
    memset(m, 0, sizeof(*m));
}

static bool configure_mesh(GpuMesh *mesh, BudoShaderProgram *program, size_t stride, size_t normal_offset)
{
    int32_t p, n;
    return budo_shader_program_attribute_location(program, "a_position", &p) == BUDO_STATUS_OK && budo_shader_program_attribute_location(program, "a_normal", &n) == BUDO_STATUS_OK && budo_mesh_set_vertex_buffer(mesh->mesh, (uint32_t)p, mesh->vertices, 3, BUDO_VERTEX_ATTRIBUTE_FLOAT32, (uint32_t)stride, 0) == BUDO_STATUS_OK && budo_mesh_set_vertex_buffer(mesh->mesh, (uint32_t)n, mesh->vertices, 3, BUDO_VERTEX_ATTRIBUTE_FLOAT32, (uint32_t)stride, (uint32_t)normal_offset) == BUDO_STATUS_OK && budo_mesh_set_index_buffer(mesh->mesh, mesh->indices, BUDO_INDEX_UINT16) == BUDO_STATUS_OK;
}
static bool dynamic_create(DynamicMesh *m, BudoHost *host)
{
    int32_t p, c;
    memset(m, 0, sizeof(*m));
    m->program = budo_shader_program_create(host, color_vs, strlen(color_vs), color_fs, strlen(color_fs));
    m->buffer = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_VERTEX, m->data, sizeof(m->data), BUDO_GPU_USAGE_DYNAMIC);
    m->mesh = budo_mesh_create(host);
    return m->program && m->buffer && m->mesh && budo_shader_program_attribute_location(m->program, "a_position", &p) == BUDO_STATUS_OK && budo_shader_program_attribute_location(m->program, "a_color", &c) == BUDO_STATUS_OK && budo_mesh_set_vertex_buffer(m->mesh, (uint32_t)p, m->buffer, 3, BUDO_VERTEX_ATTRIBUTE_FLOAT32, sizeof(ColorVertex), 0) == BUDO_STATUS_OK && budo_mesh_set_vertex_buffer(m->mesh, (uint32_t)c, m->buffer, 4, BUDO_VERTEX_ATTRIBUTE_FLOAT32, sizeof(ColorVertex), offsetof(ColorVertex, color)) == BUDO_STATUS_OK;
}
static void dynamic_vertex(DynamicMesh *m, float x, float y, float z, float r, float g, float b, float a)
{
    ColorVertex *v;
    if (m->count >= MAX_SCENE_VERTICES)
        return;
    v = &m->data[m->count++];
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->color[0] = r;
    v->color[1] = g;
    v->color[2] = b;
    v->color[3] = a;
}
static void dynamic_line(DynamicMesh *m, Vec3 a, Vec3 b, float r, float g, float bl, float alpha)
{
    dynamic_vertex(m, a.x, a.y, a.z, r, g, bl, alpha);
    dynamic_vertex(m, b.x, b.y, b.z, r, g, bl, alpha);
}
static void dynamic_tri(DynamicMesh *m, Vec3 a, Vec3 b, Vec3 c, float r, float g, float bl, float alpha)
{
    dynamic_vertex(m, a.x, a.y, a.z, r, g, bl, alpha);
    dynamic_vertex(m, b.x, b.y, b.z, r, g, bl, alpha);
    dynamic_vertex(m, c.x, c.y, c.z, r, g, bl, alpha);
}
static BudoStatus dynamic_draw(DynamicMesh *m, BudoCanvas *canvas, const float mvp[16], BudoPrimitive primitive, bool depth, bool write, BudoBlendMode blend)
{
    BudoMeshDrawInfo d = {0};
    if (!m->count)
        return BUDO_STATUS_OK;
    if (budo_gpu_buffer_update(m->buffer, 0, m->data, m->count * sizeof(ColorVertex)) != BUDO_STATUS_OK)
        return BUDO_STATUS_APPLICATION_ERROR;
    budo_shader_program_set_uniform_matrix4(m->program, "u_mvp", mvp);
    d.struct_size = sizeof(d);
    d.primitive = primitive;
    d.count = m->count;
    d.depth_test = depth;
    d.depth_write = write;
    d.cull = BUDO_CULL_NONE;
    d.blend = blend;
    return budo_canvas_draw_mesh(canvas, m->program, m->mesh, &d);
}

static float road_y_raw(float x) { return terrain_fbm(x * .0065f, 41.7f, 3, .56f) * 42 + terrain_fbm(x * .017f + 113, -21.3f, 2, .5f) * 11 + sinf(x * .021f + 1.9f) * 7.5f; }
static Vec3 road_center(float x) { return vec3(x, road_y_raw(x) - road_y_raw(0), 0); }
static Vec3 road_point(float x, float lateral, float lift)
{
    Vec3 a = road_center(x - 2), b = road_center(x + 2), c = road_center(x);
    Vec3 t = vec3_normalize(vec3_sub(b, a));
    float px = c.x - t.y * lateral, py = c.y + t.x * lateral;
    return vec3(px, py, terrain_height(px, py) + lift);
}
static void rally_reset(RallyTarget *r, float player_x)
{
    r->sign = (hash2((int)player_x, 17) & 1) ? 1 : -1;
    r->road_x = player_x + r->sign * 55;
    r->road_speed = 0;
    {
        Vec3 p = road_center(r->road_x);
        r->x = p.x;
        r->y = p.y;
        r->z = terrain_height(p.x, p.y);
    }
}
static void rally_update(RallyTarget *r, float dt)
{
    float oldx = r->x, oldy = r->y, target = r->sign * 42.0f;
    r->road_speed += (target - r->road_speed) * (1 - expf(-1.8f * dt));
    r->road_x += r->road_speed * dt;
    {
        Vec3 p = road_center(r->road_x);
        r->x = p.x;
        r->y = p.y;
        r->z = terrain_height(p.x, p.y);
    }
    if (dt > .0001f)
    {
        r->vx = (r->x - oldx) / dt;
        r->vy = (r->y - oldy) / dt;
    }
}

static void generate_palms(AppState *s, float player_x, float player_y)
{
    const float cell_size = 24.0f;
    int center_x = (int)floorf(player_x / cell_size);
    int center_y = (int)floorf(player_y / cell_size);
    int cursor = 0;
    int dy, dx;
    if (center_x == s->palm_center_x && center_y == s->palm_center_y)
        return;
    s->palm_center_x = center_x;
    s->palm_center_y = center_y;
    for (dy = -3; dy < 3; ++dy)
    {
        for (dx = -3; dx < 3; ++dx)
        {
            int cell_x = center_x + dx;
            int cell_y = center_y + dy;
            Palm *p = &s->palms[cursor++];
            p->x = (cell_x + .12f + rand2(cell_x, cell_y * 17 + 91) * .76f) * cell_size;
            p->y = (cell_y + .12f + rand2(cell_x * 19 + 92, cell_y) * .76f) * cell_size;
            p->z = terrain_height(p->x, p->y);
            p->rotation = rand2(cell_x * 23 + 93, cell_y * 29) * 2 * PI;
            p->scale = .8f + rand2(cell_x * 31 + 94, cell_y * 37) * .45f;
        }
    }
}
static void model_matrix(float out[16], float x, float y, float z, float rotation, float sx, float sy, float sz)
{
    float c = cosf(rotation), q = sinf(rotation);
    mat4_identity(out);
    out[0] = c * sx;
    out[1] = q * sx;
    out[4] = -q * sy;
    out[5] = c * sy;
    out[10] = sz;
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

static BudoStatus app_initialize(BudoHost *host, void **app_state)
{
    AppState *s = calloc(1, sizeof(*s));
    BudoGraphicsCapabilities caps = {0};
    if (!s)
        return BUDO_STATUS_OUT_OF_MEMORY;
    caps.struct_size = sizeof(caps);
    if (!budo_host_graphics_capabilities(host, &caps) || (caps.feature_flags & BUDO_GRAPHICS_FEATURE_MESH_DRAWING) == 0)
    {
        free(s);
        return BUDO_STATUS_UNSUPPORTED;
    }
    physics_reset(&s->physics);
    car_reset(&s->cars[0], 0, 0, 0, false, .85f, .2f, .2f);
    car_reset(&s->cars[1], 6, 4, PI, true, .2f, .45f, .9f);
    car_reset(&s->cars[2], -10, 7, PI * .35f, true, .95f, .72f, .18f);
    car_reset(&s->cars[3], 12, -9, -PI * .65f, true, .24f, .78f, .38f);
    s->car_count = MAX_CARS;
    for (int i = 0; i < s->car_count; ++i)
    {
        char message[128];
        s->vehicle_model_indices[i] =
            (size_t)(hash2(i + 17, 0x4b31) % (uint32_t)budo_car_model_count);
        snprintf(message, sizeof(message), "Vehicle %d uses Kenney model: %s",
                 i + 1, budo_car_models[s->vehicle_model_indices[i]].name);
        budo_host_log(host, BUDO_LOG_INFO, message);
    }
    s->camera_mode = CAMERA_FOLLOW;
    s->camera_rotation = PI / 4;
    s->follow_rotation = s->camera_rotation;
    s->follow_offset = PI;
    s->camera_pitch = PI / 6;
    s->camera_distance = 25;
    s->joystick_touch_id = -1;
    s->show_target = true;
    rally_reset(&s->rally, 0);
    s->palm_center_x = INT_MIN;
    s->palm_center_y = INT_MIN;
    generate_palms(s, s->cars[0].x, s->cars[0].y);
    *app_state = s;
    return BUDO_STATUS_OK;
}

static void release_graphics(AppState *s)
{
    int i;
    ui_gpu_destroy(&s->ui);
    release_dynamic(&s->triangles);
    release_dynamic(&s->lines);
    for (i = 0; i < s->car_count; ++i)
        release_mesh(&s->vehicle_meshes[i]);
    release_mesh(&s->object_mesh);
    release_mesh(&s->terrain_mesh);
    if (s->car_program)
        budo_shader_program_destroy(s->car_program);
    if (s->terrain_program)
        budo_shader_program_destroy(s->terrain_program);
    s->car_program = s->terrain_program = NULL;
    s->graphics_ready = false;
}
static void app_surface_created(BudoHost *host, void *app_state)
{
    AppState *s = app_state;
    int i;
    bool vehicles_ready = true;
    terrain_build(s->terrain_vertices, s->terrain_indices, s->cars[0].x, s->cars[0].y);
    s->terrain_center_x = s->terrain_center_y = 0;
    s->terrain_program = budo_shader_program_create_from_files(host, "terrain.vert", "terrain.frag");
    s->car_program = budo_shader_program_create_from_files(host, "car.vert", "car.frag");
    s->terrain_mesh.vertices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_VERTEX, s->terrain_vertices, sizeof(s->terrain_vertices), BUDO_GPU_USAGE_DYNAMIC);
    s->terrain_mesh.indices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_INDEX, s->terrain_indices, sizeof(s->terrain_indices), BUDO_GPU_USAGE_STATIC);
    s->terrain_mesh.mesh = budo_mesh_create(host);
    s->object_mesh.vertices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_VERTEX, car_vertices, sizeof(car_vertices), BUDO_GPU_USAGE_STATIC);
    s->object_mesh.indices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_INDEX, car_indices, sizeof(car_indices), BUDO_GPU_USAGE_STATIC);
    s->object_mesh.mesh = budo_mesh_create(host);
    for (i = 0; i < s->car_count; ++i)
    {
        const CarModelData *model = &budo_car_models[s->vehicle_model_indices[i]];
        GpuMesh *mesh = &s->vehicle_meshes[i];
        mesh->vertices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_VERTEX,
                                                model->vertices,
                                                model->vertex_count * sizeof(*model->vertices),
                                                BUDO_GPU_USAGE_STATIC);
        mesh->indices = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_INDEX,
                                               model->indices,
                                               model->index_count * sizeof(*model->indices),
                                               BUDO_GPU_USAGE_STATIC);
        mesh->mesh = budo_mesh_create(host);
        s->vehicle_index_counts[i] = model->index_count;
        if (!mesh->vertices || !mesh->indices || !mesh->mesh ||
            !configure_mesh(mesh, s->car_program, sizeof(CarModelVertex),
                            offsetof(CarModelVertex, normal)))
            vehicles_ready = false;
    }
    if (!s->terrain_program || !s->car_program || !s->terrain_mesh.vertices || !s->terrain_mesh.indices || !s->terrain_mesh.mesh || !s->object_mesh.vertices || !s->object_mesh.indices || !s->object_mesh.mesh || !configure_mesh(&s->terrain_mesh, s->terrain_program, sizeof(TerrainVertex), offsetof(TerrainVertex, normal)) || !configure_mesh(&s->object_mesh, s->car_program, sizeof(CarVertex), offsetof(CarVertex, normal)) || !vehicles_ready || !dynamic_create(&s->lines, host) || !dynamic_create(&s->triangles, host) || !ui_gpu_create(&s->ui, host))
    {
        log_error(host, "Creating scene resources failed");
        release_graphics(s);
        return;
    }
    s->graphics_ready = true;
}

static void update_terrain(AppState *s)
{
    float x = floorf(s->cars[0].x / TERRAIN_RECENTER_STEP) * TERRAIN_RECENTER_STEP, y = floorf(s->cars[0].y / TERRAIN_RECENTER_STEP) * TERRAIN_RECENTER_STEP;
    if (x == s->terrain_center_x && y == s->terrain_center_y)
        return;
    terrain_build(s->terrain_vertices, s->terrain_indices, s->cars[0].x, s->cars[0].y);
    budo_gpu_buffer_update(s->terrain_mesh.vertices, 0, s->terrain_vertices, sizeof(s->terrain_vertices));
    s->terrain_center_x = x;
    s->terrain_center_y = y;
}
static bool point_in(float x, float y, float cx, float cy, float w, float h) { return x >= cx - w * .5f && x <= cx + w * .5f && y >= cy - h * .5f && y <= cy + h * .5f; }
static void cycle_camera(AppState *s) { s->camera_mode = (CameraMode)((s->camera_mode + 1) % 3); }
static void adjust_parameter(AppState *s, int index, float amount)
{
    switch (index)
    {
    case 0: s->physics.mass = clampf(s->physics.mass + amount * .5f, .1f, 40); break;
    case 1: s->physics.gravity = clampf(s->physics.gravity + amount * .5f, 0, 60); break;
    case 2: s->physics.move_force = clampf(s->physics.move_force + amount, 0, 170); break;
    case 3: s->physics.linear_drag = clampf(s->physics.linear_drag + amount * .05f, 0, 10); break;
    case 4: s->physics.rolling_friction = clampf(s->physics.rolling_friction + amount * .1f, 0, 20); break;
    case 5: s->physics.max_speed = clampf(s->physics.max_speed + amount, 1, 240); break;
    case 6: s->physics.turn_rate = clampf(s->physics.turn_rate + amount * .1f, .2f, 16); break;
    case 7: s->physics.vehicle_scale = clampf(s->physics.vehicle_scale + amount * .1f, .5f, 5); break;
    }
}

static CarControls controls(AppState *s, const BudoFrameInfo *f, float dt)
{
    CarControls c = {0};
    const BudoInput *in = f->input;
    BudoPointer p;
    uint32_t i, count = budo_input_touch_count(in);
    float w = (float)f->surface.width, h = (float)f->surface.height;
    s->joystick_radius = fminf(w, h) * .12f;
    s->joystick_x = s->joystick_radius + 40;
    s->joystick_y = h - s->joystick_radius - 70;
    if (budo_input_key_down(in, BUDO_KEY_W) || budo_input_key_down(in, BUDO_KEY_UP))
        c.throttle++;
    if (budo_input_key_down(in, BUDO_KEY_S) || budo_input_key_down(in, BUDO_KEY_DOWN))
        c.throttle--;
    if (budo_input_key_down(in, BUDO_KEY_A) || budo_input_key_down(in, BUDO_KEY_LEFT))
        c.steer--;
    if (budo_input_key_down(in, BUDO_KEY_D) || budo_input_key_down(in, BUDO_KEY_RIGHT))
        c.steer++;
    if (budo_input_key_pressed(in, BUDO_KEY_V))
        cycle_camera(s);
    if (budo_input_key_pressed(in, BUDO_KEY_BACKSPACE))
        physics_reset(&s->physics);
    if (budo_input_key_pressed(in, BUDO_KEY_SPACE))
    {
        car_reset(&s->cars[0], 0, 0, 0, false, .85f, .2f, .2f);
    }
    for (i = 0; i < 8; i++)
        if (budo_input_key_pressed(in, (BudoKey)(BUDO_KEY_1 + i)))
            s->selected_parameter = (int)i;
    for (i = 0; i < count; i++)
    {
        BudoTouchPoint t;
        if (!budo_input_touch(in, i, &t))
            continue;
        if (t.x < (int)w / 2)
        {
            float dx = (t.x - s->joystick_x) / s->joystick_radius, dy = (t.y - s->joystick_y) / s->joystick_radius, m = hypotf(dx, dy);
            s->joystick_touch_id = t.id;
            if (m > .15f)
            {
                float k = fminf(m, 1) / m;
                s->joystick_dx = dx * k;
                s->joystick_dy = dy * k;
                c.steer += s->joystick_dx;
                c.throttle -= s->joystick_dy;
            }
            if (t.released)
            {
                s->joystick_touch_id = -1;
                s->joystick_dx = s->joystick_dy = 0;
            }
        }
        else
        {
            float bx = w - 120, by = h - 320;
            if (t.pressed && t.x > w - 430 && t.x < w - 10 &&
                t.y >= 118 && t.y < 366)
            {
                int row = (t.y - 118) / 31;
                if (row >= 0 && row < 8)
                {
                    s->selected_parameter = row;
                    if (t.x > w - 54)
                        adjust_parameter(s, row, 1);
                    else if (t.x > w - 98)
                        adjust_parameter(s, row, -1);
                }
                continue;
            }
            if (point_in(t.x, t.y, bx, by - 98, 150, 44) && t.pressed)
                cycle_camera(s);
            if (point_in(t.x, t.y, bx, by - 150, 210, 44) && t.pressed)
                s->show_target = !s->show_target;
            if (point_in(t.x, t.y, bx - 55, by, 90, 64))
                s->follow_offset -= 1.5f * dt;
            if (point_in(t.x, t.y, bx + 55, by, 90, 64))
                s->follow_offset += 1.5f * dt;
            if (point_in(t.x, t.y, bx, by + 78, 100, 64))
                s->camera_pitch = clampf(s->camera_pitch + .8f * dt, .1f, 1.47f);
            if (point_in(t.x, t.y, bx, by + 156, 100, 64))
                s->camera_pitch = clampf(s->camera_pitch - .8f * dt, .1f, 1.47f);
            if (point_in(t.x, t.y, bx - 55, by + 234, 90, 64))
                s->camera_distance = clampf(s->camera_distance - 10 * dt, 8, 50);
            if (point_in(t.x, t.y, bx + 55, by + 234, 90, 64))
                s->camera_distance = clampf(s->camera_distance + 10 * dt, 8, 50);
        }
    }
    if (budo_input_pointer(in, &p))
    {
        bool click = p.pressed[BUDO_POINTER_LEFT];
        if (click && point_in(p.x, p.y, w - 120, h - 418, 150, 44))
            cycle_camera(s);
        if (click && point_in(p.x, p.y, w - 120, h - 470, 210, 44))
            s->show_target = !s->show_target;
        if (click && p.x > w - 430 && p.x < w - 10 &&
            p.y >= 118 && p.y < 366)
        {
            int row = (p.y - 118) / 31;
            if (row >= 0 && row < 8)
            {
                s->selected_parameter = row;
                if (p.x > w - 54)
                    adjust_parameter(s, row, 1);
                else if (p.x > w - 98)
                    adjust_parameter(s, row, -1);
            }
        }
    }
    {
        float rot = ((budo_input_key_down(in, BUDO_KEY_E) ? 1 : 0) - (budo_input_key_down(in, BUDO_KEY_Q) ? 1 : 0)) * 1.5f * dt;
        if (s->camera_mode == CAMERA_FOLLOW)
            s->follow_offset += rot;
        else
            s->camera_rotation += rot;
    }
    if (budo_input_key_down(in, BUDO_KEY_R))
        s->camera_pitch = clampf(s->camera_pitch + .8f * dt, .1f, 1.47f);
    if (budo_input_key_down(in, BUDO_KEY_F))
        s->camera_pitch = clampf(s->camera_pitch - .8f * dt, .1f, 1.47f);
    if (budo_input_key_down(in, BUDO_KEY_T))
        s->camera_distance = clampf(s->camera_distance - 10 * dt, 8, 50);
    if (budo_input_key_down(in, BUDO_KEY_G))
        s->camera_distance = clampf(s->camera_distance + 10 * dt, 8, 50);
    c.throttle = clampf(c.throttle, -1, 1);
    c.steer = clampf(c.steer, -1, 1);
    return c;
}

static void lighting(double ms, Lighting *l)
{
    float phase = (float)(ms / 90000.0 * 2 * PI + PI * .3), sun = sinf(phase), day = clampf((sun + .18f) / .60f, 0, 1);
    day = day * day * (3 - 2 * day);
    float twilight = (1 - fabsf(day * 2 - 1)) * clampf((sun + .35f) / .60f, 0, 1);
    l->ambient = .10f + day * .33f + twilight * .08f;
    l->sky[0] = .07f + day * .84f + twilight * .15f;
    l->sky[1] = .09f + day * .72f + twilight * .05f;
    l->sky[2] = .18f + day * .42f;
    l->fog[0] = .10f + day * .81f;
    l->fog[1] = .12f + day * .69f;
    l->fog[2] = .22f + day * .38f;
    l->light[0] = .28f + day * .72f;
    l->light[1] = .36f + day * .50f;
    l->light[2] = .68f;
    l->direction[0] = cosf(phase) * .62f;
    l->direction[1] = .36f;
    l->direction[2] = fmaxf(.1f, fabsf(sun));
    {
        float n = sqrtf(l->direction[0] * l->direction[0] + l->direction[1] * l->direction[1] + l->direction[2] * l->direction[2]);
        l->direction[0] /= n;
        l->direction[1] /= n;
        l->direction[2] /= n;
    }
    l->night = 1 - clampf((day - .16f) / .3f, 0, 1);
}
static void shader_scene(BudoShaderProgram *p, const float mvp[16], const float *model, Vec3 eye, const Lighting *l)
{
    budo_shader_program_set_uniform_matrix4(p, "u_mvp", mvp);
    if (model)
        budo_shader_program_set_uniform_matrix4(p, "u_model", model);
    budo_shader_program_set_uniform_3f(p, "u_eye", eye.x, eye.y, eye.z);
    budo_shader_program_set_uniform_3f(p, "u_fog_color", l->fog[0], l->fog[1], l->fog[2]);
    budo_shader_program_set_uniform_3f(p, "u_light_direction", l->direction[0], l->direction[1], l->direction[2]);
    budo_shader_program_set_uniform_3f(p, "u_light_dir", l->direction[0], l->direction[1], l->direction[2]);
    budo_shader_program_set_uniform_3f(p, "u_light_color", l->light[0], l->light[1], l->light[2]);
    budo_shader_program_set_uniform_1f(p, "u_ambient", l->ambient);
    budo_shader_program_set_uniform_1f(p, "u_fog_start", 38);
    budo_shader_program_set_uniform_1f(p, "u_fog_end", 105);
}

static void build_road(AppState *s, double ms)
{
    float start = s->cars[0].x - 130, end = s->cars[0].x + 130, x, flow = (float)ms * .018f;
    s->lines.count = 0;
    for (x = start + 6; x <= end; x += 6)
    {
        float phase = fmodf(x - flow, 20);
        if (phase < 0)
            phase += 20;
        if (phase < 11)
        {
            dynamic_line(&s->lines, road_point(x - 6, 0, .46f), road_point(x, 0, .46f), 1, .9f, .46f, 1);
            dynamic_line(&s->lines, road_point(x - 6, .18f, .48f), road_point(x, .18f, .48f), 1, 1, .85f, 1);
        }
    }
}
static void arrow(DynamicMesh *m, Vec3 p, Vec3 force, float scale, float r, float g, float b)
{
    Vec3 end = vec3_add(p, vec3_scale(force, scale));
    Vec3 d = vec3_normalize(force), side = vec3_normalize(fabsf(d.z) < .9f ? vec3(d.y, -d.x, 0) : vec3(0, d.z, -d.y));
    Vec3 base = vec3_sub(end, vec3_scale(d, .7f));
    dynamic_line(m, p, end, r, g, b, 1);
    dynamic_line(m, end, vec3_add(base, vec3_scale(side, .25f)), r, g, b, 1);
    dynamic_line(m, end, vec3_sub(base, vec3_scale(side, .25f)), r, g, b, 1);
}
static void build_effects(AppState *s)
{
    int i, seg;
    s->triangles.count = 0;
    for (i = 0; i < s->car_count; i++)
    {
        Car *c = &s->cars[i];
        float radius = 1.5f * s->physics.vehicle_scale * .85f;
        Vec3 center = vec3(c->x, c->y, terrain_height(c->x, c->y) + .1f);
        for (seg = 0; seg < 20; seg++)
        {
            float a = seg * 2 * PI / 20, b = (seg + 1) * 2 * PI / 20;
            Vec3 p = vec3(c->x + cosf(a) * radius, c->y + sinf(a) * radius, terrain_height(c->x + cosf(a) * radius, c->y + sinf(a) * radius) + .1f);
            Vec3 q = vec3(c->x + cosf(b) * radius, c->y + sinf(b) * radius, terrain_height(c->x + cosf(b) * radius, c->y + sinf(b) * radius) + .1f);
            dynamic_tri(&s->triangles, center, p, q, 0, 0, 0, .24f);
        }
    }
    if (s->show_target)
    {
        Vec3 p = vec3(s->cars[0].x, s->cars[0].y, s->cars[0].z + s->physics.vehicle_scale * .6f);
        arrow(&s->lines, p, s->player_forces.gravity, .08f, 1, .27f, .27f);
        arrow(&s->lines, p, s->player_forces.reaction, .08f, .27f, 1, .27f);
        arrow(&s->lines, p, s->player_forces.gravity_tangent, .08f, 1, 1, .27f);
        arrow(&s->lines, p, s->player_forces.resultant, .08f, .27f, .87f, 1);
    }
    if (s->cars[0].grounded && s->triangles.count < MAX_SCENE_VERTICES - 24)
    {
        Car *c = &s->cars[0];
        Vec3 f = vec3(cosf(c->heading), sinf(c->heading), 0), r = vec3(f.y, -f.x, 0);
        for (i = -1; i <= 1; i += 2)
        {
            Vec3 origin = vec3(c->x + f.x * 2.0f + r.x * i * .45f, c->y + f.y * 2.0f + r.y * i * .45f, terrain_height(c->x + f.x * 2.0f + r.x * i * .45f, c->y + f.y * 2.0f + r.y * i * .45f) + .14f);
            Vec3 left = vec3(origin.x + f.x * 15 - r.x * 3, origin.y + f.y * 15 - r.y * 3, terrain_height(origin.x + f.x * 15 - r.x * 3, origin.y + f.y * 15 - r.y * 3) + .14f);
            Vec3 right = vec3(origin.x + f.x * 15 + r.x * 3, origin.y + f.y * 15 + r.y * 3, terrain_height(origin.x + f.x * 15 + r.x * 3, origin.y + f.y * 15 + r.y * 3) + .14f);
            dynamic_tri(&s->triangles, origin, left, right, 1, .8f, .3f, .18f);
        }
    }
}

static void draw_model(AppState *s, BudoCanvas *canvas, GpuMesh *mesh,
                       uint32_t index_count, const float vp[16],
                       const float model[16], Vec3 eye, const Lighting *l,
                       float r, float g, float b)
{
    float mvp[16];
    BudoMeshDrawInfo d = {0};
    mat4_multiply(mvp, vp, model);
    shader_scene(s->car_program, mvp, model, eye, l);
    budo_shader_program_set_uniform_3f(s->car_program, "u_color", r, g, b);
    budo_shader_program_set_uniform_1f(s->car_program, "u_headlight_on", l->night);
    d.struct_size = sizeof(d);
    d.primitive = BUDO_PRIMITIVE_TRIANGLES;
    d.count = index_count;
    d.depth_test = true;
    d.depth_write = true;
    d.cull = BUDO_CULL_BACK;
    d.blend = BUDO_BLEND_NONE;
    budo_canvas_draw_mesh(canvas, s->car_program, mesh->mesh, &d);
}
static void draw_cube(AppState *s, BudoCanvas *canvas, const float vp[16], const float model[16], Vec3 eye, const Lighting *l, float r, float g, float b)
{
    draw_model(s, canvas, &s->object_mesh, 36, vp, model, eye, l, r, g, b);
}
static void draw_world_objects(AppState *s, BudoCanvas *canvas, const float vp[16], Vec3 eye, const Lighting *l)
{
    int i;
    float model[16];
    for (i = 0; i < PALM_COUNT; i++)
    {
        Palm *p = &s->palms[i];
        float dx = p->x - s->cars[0].x, dy = p->y - s->cars[0].y;
        if (dx * dx + dy * dy > 78 * 78)
            continue;
        model_matrix(model, p->x, p->y, p->z + 2.2f * p->scale, p->rotation, .45f * p->scale, .45f * p->scale, 4.4f * p->scale);
        draw_cube(s, canvas, vp, model, eye, l, .45f, .27f, .12f);
        model_matrix(model, p->x, p->y, p->z + 5.0f * p->scale, p->rotation, 3.0f * p->scale, 3.0f * p->scale, .35f * p->scale);
        draw_cube(s, canvas, vp, model, eye, l, .12f, .48f, .18f);
    }
    if (s->show_target)
    {
        model_matrix(model, s->rally.x, s->rally.y, s->rally.z + 3.2f, 0, 3.2f, 3.2f, 3.2f);
        draw_cube(s, canvas, vp, model, eye, l, .82f, .86f, .9f);
    }
}

static const char *mode_name(CameraMode m) { return m == CAMERA_ORBIT ? "ORBIT" : m == CAMERA_FOLLOW ? "FOLLOW"
                                                                                                     : "FPV"; }
static void ui_button(UiGpu *u, float x, float y, float w, float h, const char *label, bool active)
{
    float text_width = (float)strlen(label) * (12.0f * 2.0f / 7.0f) * 6.0f;
    ui_gpu_rect(u, x - w * .5f, y - h * .5f, w, h, active ? 0x9944ddff : 0x553c4655);
    ui_gpu_text(u, x - text_width * .5f, y - 12, 12, 0xffffffff, label);
}
static void build_ui(AppState *s, const BudoFrameInfo *f)
{
    UiGpu *u = &s->ui;
    float w = f->surface.width, h = f->surface.height, bx = w - 120, by = h - 320;
    char text[256];
    int i;
    static const char *names[] = {"MASS", "GRAVITY", "THROTTLE", "AIR DRAG", "FRICTION", "MAX SPEED", "TURN RATE", "CAR SIZE"};
    float values[] = {s->physics.mass, s->physics.gravity, s->physics.move_force, s->physics.linear_drag, s->physics.rolling_friction, s->physics.max_speed, s->physics.turn_rate, s->physics.vehicle_scale};
    ui_gpu_begin(u, w, h);
    ui_gpu_text(u, 10, 10, 18, 0xffffffff, "TERRAIN PHYSICS");
    ui_gpu_text(u, 10, 52, 10, 0xffd0d0d0,
                "WASD DRIVE  Q/E ROTATE\nR/F ANGLE  T/G ZOOM  V VIEW");
    ui_gpu_rect(u, 8, 102, 132, 34, 0x99000000);
    snprintf(text, sizeof(text), "FPS %.0F", s->smoothed_fps);
    ui_gpu_text(u, 14, 107, 12, s->smoothed_fps >= 50 ? 0xff7cff8b : s->smoothed_fps >= 30 ? 0xffffe066
                                                                                          : 0xffff6666,
                text);
    ui_gpu_line(u, 70, 120, 110, 120, 3, 0xffff4444);
    ui_gpu_text(u, 114, 114, 10, 0xffff4444, "X");
    ui_gpu_line(u, 70, 120, 70, 82, 3, 0xff44ff44);
    ui_gpu_text(u, 64, 68, 10, 0xff44ff44, "Y");
    ui_gpu_line(u, 70, 120, 45, 145, 3, 0xff4488ff);
    ui_gpu_text(u, 31, 146, 10, 0xff4488ff, "Z");
    ui_gpu_rect(u, 14, 151, 155, 126, 0x99000000);
    ui_gpu_text(u, 60, 157, 11, 0xffffffff, "RADAR");
    ui_gpu_circle(u, 91, 218, 50, 2, 0x88ffffff);
    ui_gpu_circle(u, 91, 218, 25, 1, 0x44ffffff);
    ui_gpu_line(u, 41, 218, 141, 218, 1, 0x44ffffff);
    ui_gpu_line(u, 91, 168, 91, 268, 1, 0x44ffffff);
    for (i = 1; i < s->car_count; i++)
    {
        float dx = s->cars[i].x - s->cars[0].x, dy = s->cars[i].y - s->cars[0].y, ch = cosf(s->cars[0].heading), sh = sinf(s->cars[0].heading), fw = dx * ch + dy * sh, rt = dx * sh - dy * ch, d = hypotf(fw, rt), k = (d > 85 ? 85 : d) / (d > .001f ? d : 1) * 50 / 85;
        ui_gpu_circle(u, 91 + rt * k, 218 - fw * k, 4, 3, 0xff44a6ff);
    }
    ui_gpu_rect(u, 87, 211, 8, 12, 0xffff4444);
    ui_gpu_rect(u, w - 430, 78, 420, 342, 0xaa000000);
    ui_gpu_text(u, w - 418, 87, 11, 0xffffffff, "PHYSICS  1-8 SELECT");
    for (i = 0; i < 8; i++)
    {
        if (i == s->selected_parameter)
            ui_gpu_rect(u, w - 422, 118 + i * 31, 404, 27, 0x5544ddff);
        snprintf(text, sizeof(text), "%d %-9s %5.1F", i + 1, names[i], values[i]);
        ui_gpu_text(u, w - 414, 122 + i * 31, 10, i == s->selected_parameter ? 0xffffffff : 0xffdddddd, text);
        ui_gpu_rect(u, w - 96, 119 + i * 31, 38, 25, 0x664f5968);
        ui_gpu_rect(u, w - 52, 119 + i * 31, 38, 25, 0x664f5968);
        ui_gpu_text(u, w - 86, 121 + i * 31, 10, 0xffffffff, "-");
        ui_gpu_text(u, w - 44, 121 + i * 31, 10, 0xffffffff, "+");
    }
    ui_gpu_text(u, w - 414, 376, 9, 0xffffe89a, "BACKSPACE RESET");
    snprintf(text, sizeof(text), "POS %.1F %.1F %.1F  ALT %.2F  VZ %.2F", s->cars[0].x, s->cars[0].y, s->cars[0].z, s->cars[0].z - terrain_height(s->cars[0].x, s->cars[0].y), s->cars[0].vz);
    ui_gpu_text(u, 10, h - 52, 10, 0xffffffff, text);
    snprintf(text, sizeof(text), "CAM %s  ANG %.0F  DIST %.1F", mode_name(s->camera_mode), s->camera_pitch * 180 / PI, s->camera_distance);
    ui_gpu_text(u, 10, h - 27, 10, 0xffffffff, text);
    if (!s->cars[0].grounded)
    {
        snprintf(text, sizeof(text), "AIRBORNE %.2FS", s->cars[0].airborne_time);
        ui_gpu_text(u, w * .5f - 120, 105, 16, 0xffffcc44, text);
    }
    ui_gpu_circle(u, s->joystick_x, s->joystick_y, s->joystick_radius, 3, 0x88ffffff);
    ui_gpu_circle(u, s->joystick_x + s->joystick_dx * s->joystick_radius * .8f, s->joystick_y + s->joystick_dy * s->joystick_radius * .8f, s->joystick_radius * .4f, 5, 0x9944ddff);
    ui_gpu_text(u, s->joystick_x - 20, s->joystick_y + s->joystick_radius + 12, 10, 0xbbffffff, "MOVE");
    ui_button(u, bx, by - 150, 210, 44, s->show_target ? "TARGET ON" : "TARGET OFF", s->show_target);
    ui_button(u, bx, by - 98, 150, 44, mode_name(s->camera_mode), s->camera_mode != CAMERA_ORBIT);
    ui_button(u, bx - 55, by, 90, 64, "<", false);
    ui_button(u, bx + 55, by, 90, 64, ">", false);
    ui_button(u, bx, by + 78, 100, 64, "UP", false);
    ui_button(u, bx, by + 156, 100, 64, "DOWN", false);
    ui_button(u, bx - 55, by + 234, 90, 64, "+", false);
    ui_button(u, bx + 55, by + 234, 90, 64, "-", false);
    ui_gpu_rect(u, w - 270, 430, 260, 148, 0x99000000);
    ui_gpu_text(u, w - 258, 440, 11, 0xffffffff, "FORCES");
    ui_gpu_line(u, w - 258, 477, w - 220, 477, 4, 0xffff4444);
    ui_gpu_text(u, w - 210, 466, 9, 0xffffffff, "GRAVITY");
    ui_gpu_line(u, w - 258, 505, w - 220, 505, 4, 0xff44ff44);
    ui_gpu_text(u, w - 210, 494, 9, 0xffffffff, "REACTION");
    ui_gpu_line(u, w - 258, 533, w - 220, 533, 4, 0xffffff44);
    ui_gpu_text(u, w - 210, 522, 9, 0xffffffff, "SLOPE");
    ui_gpu_line(u, w - 258, 561, w - 220, 561, 4, 0xff44ddff);
    ui_gpu_text(u, w - 210, 550, 9, 0xffffffff, "INPUT");
}

static void app_frame(BudoHost *host, void *app_state, const BudoFrameInfo *f)
{
    AppState *s = app_state;
    BudoCanvas *canvas = budo_host_canvas(host);
    BudoMeshDrawInfo terrain = {0};
    Lighting light;
    CarControls player;
    float proj[16], view[16], vp[16], model[16], mvp[16], dt = (float)f->delta_seconds;
    Vec3 eye, target, up = vec3(0, 0, 1);
    int i;
    if (!s->graphics_ready || !canvas || !f->surface.height)
        return;
    if (dt > 0)
    {
        double fps = 1.0 / dt;
        s->smoothed_fps = s->smoothed_fps ? s->smoothed_fps + (fps - s->smoothed_fps) * .12 : fps;
    }
    s->mobile = f->surface.density > 1.25f;
    player = controls(s, f, dt);
    rally_update(&s->rally, dt);
    s->player_forces = car_update(&s->cars[0], &s->physics, dt, player);
    for (i = 1; i < s->car_count; i++)
    {
        CarControls ai = car_ai_controls(&s->cars[i], dt, s->rally.x, s->rally.y, s->rally.vx, s->rally.vy);
        car_update(&s->cars[i], &s->physics, dt, ai);
    }
    generate_palms(s, s->cars[0].x, s->cars[0].y);
    for (i = 0; i < PALM_COUNT; i++)
    {
        int j;
        for (j = 0; j < s->car_count; j++)
            car_resolve_tree_collision(&s->cars[j], &s->physics, s->palms[i].x, s->palms[i].y, .25f * s->palms[i].scale);
    }
    cars_resolve_collisions(s->cars, s->car_count, &s->physics);
    update_terrain(s);
    lighting(f->timestamp_ms, &light);
    if (s->camera_mode == CAMERA_FPV)
    {
        Vec3 right, forward;
        car_visual_axes(&s->cars[0], &right, &forward, &up);
        eye = vec3_add(vec3(s->cars[0].x, s->cars[0].y, s->cars[0].z), vec3_add(vec3_scale(up, s->physics.vehicle_scale * 1.75f), vec3_scale(forward, .5f)));
        target = vec3_add(eye, vec3_scale(forward, 5));
        mat4_perspective(proj, PI / 2.3f, (float)f->surface.width / f->surface.height, .05f, 200);
    }
    else
    {
        float rotation = s->camera_rotation;
        if (s->camera_mode == CAMERA_FOLLOW)
        {
            float desired = s->cars[0].heading + s->follow_offset;
            float delta = desired - s->follow_rotation;
            while (delta > PI)
                delta -= 2 * PI;
            while (delta < -PI)
                delta += 2 * PI;
            s->follow_rotation += delta * (1 - expf(-2.4f * dt));
            rotation = s->follow_rotation;
        }
        target = vec3(s->cars[0].x, s->cars[0].y, s->cars[0].z + s->physics.vehicle_scale * .25f);
        eye = vec3(target.x + s->camera_distance * cosf(s->camera_pitch) * cosf(rotation), target.y + s->camera_distance * cosf(s->camera_pitch) * sinf(rotation), target.z + s->camera_distance * sinf(s->camera_pitch));
        mat4_perspective(proj, PI / 3, (float)f->surface.width / f->surface.height, .1f, 200);
    }
    mat4_look_at(view, eye, target, up);
    mat4_multiply(vp, proj, view);
    budo_canvas_clear(canvas, BUDO_COLOR_RGB((uint8_t)(light.sky[0] * 255), (uint8_t)(light.sky[1] * 255), (uint8_t)(light.sky[2] * 255)));
    shader_scene(s->terrain_program, vp, NULL, eye, &light);
    budo_shader_program_set_uniform_1f(s->terrain_program, "u_height_scale", 3.2f);
    terrain.struct_size = sizeof(terrain);
    terrain.primitive = BUDO_PRIMITIVE_TRIANGLES;
    terrain.count = TERRAIN_INDEX_COUNT;
    terrain.depth_test = true;
    terrain.depth_write = true;
    terrain.cull = BUDO_CULL_NONE;
    terrain.blend = BUDO_BLEND_NONE;
    budo_canvas_draw_mesh(canvas, s->terrain_program, s->terrain_mesh.mesh, &terrain);
    build_road(s, f->timestamp_ms);
    dynamic_draw(&s->lines, canvas, vp, BUDO_PRIMITIVE_LINES, true, false, BUDO_BLEND_NONE);
    build_effects(s);
    dynamic_draw(&s->triangles, canvas, vp, BUDO_PRIMITIVE_TRIANGLES, true, false, BUDO_BLEND_ALPHA);
    draw_world_objects(s, canvas, vp, eye, &light);
    for (i = 0; i < s->car_count; i++)
    {
        if (i == 0 && s->camera_mode == CAMERA_FPV)
            continue;
        car_model_matrix(&s->cars[i], &s->physics, model);
        draw_model(s, canvas, &s->vehicle_meshes[i],
               (uint32_t)s->vehicle_index_counts[i], vp, model, eye,
               &light, s->cars[i].color[0], s->cars[i].color[1],
               s->cars[i].color[2]);
    }
    if (s->show_target)
    {
        s->lines.count = 0;
        build_effects(s);
        dynamic_draw(&s->lines, canvas, vp, BUDO_PRIMITIVE_LINES, true, false, BUDO_BLEND_ALPHA);
    }
    build_ui(s, f);
    if (ui_gpu_draw(&s->ui, canvas) != BUDO_STATUS_OK)
        log_error(host, "Drawing HUD failed");
    (void)mvp;
}

static void app_context_lost(BudoHost *host, void *state)
{
    (void)host;
    release_graphics(state);
}
static void app_shutdown(BudoHost *host, void *state)
{
    (void)host;
    free(state);
}
static const BudoApplication application = {.struct_size = sizeof(BudoApplication), .api_version = BUDO_NATIVE_API_VERSION, .sdk_version = BUDO_VERSION_STRING, .sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID, .name = "Native Terrain Physics", .initialize = app_initialize, .surface_created = app_surface_created, .frame = app_frame, .context_lost = app_context_lost, .shutdown = app_shutdown};
const BudoApplication *budo_get_application(void) { return &application; }