#include "ui_gpu.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static const char vertex_shader[] =
    "#version 300 es\nprecision highp float;\n"
    "in vec2 a_position; in vec4 a_color; out vec4 v_color;\n"
    "void main(){ gl_Position=vec4(a_position,0.0,1.0); v_color=a_color; }\n";
static const char fragment_shader[] =
    "#version 300 es\nprecision highp float;\n"
    "in vec4 v_color; out vec4 out_color;\n"
    "void main(){ out_color=v_color; }\n";

static const unsigned char font[96][5] = {
    {0, 0, 0, 0, 0}, {0, 0, 95, 0, 0}, {0, 7, 0, 7, 0}, {20, 127, 20, 127, 20}, {36, 42, 127, 42, 18}, {35, 19, 8, 100, 98}, {54, 73, 85, 34, 80}, {0, 5, 3, 0, 0}, {0, 28, 34, 65, 0}, {0, 65, 34, 28, 0}, {20, 8, 62, 8, 20}, {8, 8, 62, 8, 8}, {0, 80, 48, 0, 0}, {8, 8, 8, 8, 8}, {0, 96, 96, 0, 0}, {32, 16, 8, 4, 2}, {62, 81, 73, 69, 62}, {0, 66, 127, 64, 0}, {66, 97, 81, 73, 70}, {33, 65, 69, 75, 49}, {24, 20, 18, 127, 16}, {39, 69, 69, 69, 57}, {60, 74, 73, 73, 48}, {1, 113, 9, 5, 3}, {54, 73, 73, 73, 54}, {6, 73, 73, 41, 30}, {0, 54, 54, 0, 0}, {0, 86, 54, 0, 0}, {8, 20, 34, 65, 0}, {20, 20, 20, 20, 20}, {0, 65, 34, 20, 8}, {2, 1, 81, 9, 6}, {50, 73, 121, 65, 62}, {126, 17, 17, 17, 126}, {127, 73, 73, 73, 54}, {62, 65, 65, 65, 34}, {127, 65, 65, 34, 28}, {127, 73, 73, 73, 65}, {127, 9, 9, 9, 1}, {62, 65, 73, 73, 122}, {127, 8, 8, 8, 127}, {0, 65, 127, 65, 0}, {32, 64, 65, 63, 1}, {127, 8, 20, 34, 65}, {127, 64, 64, 64, 64}, {127, 2, 12, 2, 127}, {127, 4, 8, 16, 127}, {62, 65, 65, 65, 62}, {127, 9, 9, 9, 6}, {62, 65, 81, 33, 94}, {127, 9, 25, 41, 70}, {70, 73, 73, 73, 49}, {1, 1, 127, 1, 1}, {63, 64, 64, 64, 63}, {31, 32, 64, 32, 31}, {63, 64, 56, 64, 63}, {99, 20, 8, 20, 99}, {7, 8, 112, 8, 7}, {97, 81, 73, 69, 67}, {0, 127, 65, 65, 0}, {2, 4, 8, 16, 32}, {0, 65, 65, 127, 0}, {4, 2, 1, 2, 4}, {64, 64, 64, 64, 64}, {0, 1, 2, 4, 0}, {32, 84, 84, 84, 120}, {127, 72, 68, 68, 56}, {56, 68, 68, 68, 32}, {56, 68, 68, 72, 127}, {56, 84, 84, 84, 24}, {8, 126, 9, 1, 2}, {12, 82, 82, 82, 62}, {127, 8, 4, 4, 120}, {0, 68, 125, 64, 0}, {32, 64, 68, 61, 0}, {127, 16, 40, 68, 0}, {0, 65, 127, 64, 0}, {124, 4, 24, 4, 120}, {124, 8, 4, 4, 120}, {56, 68, 68, 68, 56}, {124, 20, 20, 20, 8}, {8, 20, 20, 24, 124}, {124, 8, 4, 4, 8}, {72, 84, 84, 84, 32}, {4, 63, 68, 64, 32}, {60, 64, 64, 32, 124}, {28, 32, 64, 32, 28}, {60, 64, 48, 64, 60}, {68, 40, 16, 40, 68}, {12, 80, 80, 80, 60}, {68, 100, 84, 76, 68}, {0, 8, 54, 65, 0}, {0, 0, 127, 0, 0}, {0, 65, 54, 8, 0}, {16, 8, 8, 16, 8}, {127, 65, 65, 65, 127}};

static void color_values(uint32_t color, float *r, float *g, float *b, float *a)
{
    *a = ((color >> 24) & 255) / 255.0f;
    *r = ((color >> 16) & 255) / 255.0f;
    *g = ((color >> 8) & 255) / 255.0f;
    *b = (color & 255) / 255.0f;
}

static void vertex(UiGpu *ui, float x, float y, uint32_t color)
{
    UiGpuVertex *v;
    if (ui->count >= UI_GPU_MAX_VERTICES)
        return;
    v = &ui->vertices[ui->count++];
    v->x = x / ui->width * 2.0f - 1.0f;
    v->y = 1.0f - y / ui->height * 2.0f;
    color_values(color, &v->r, &v->g, &v->b, &v->a);
}

bool ui_gpu_create(UiGpu *ui, BudoHost *host)
{
    int32_t position, color;
    memset(ui, 0, sizeof(*ui));
    ui->program = budo_shader_program_create(host, vertex_shader,
                                             strlen(vertex_shader), fragment_shader, strlen(fragment_shader));
    ui->buffer = budo_gpu_buffer_create(host, BUDO_GPU_BUFFER_VERTEX,
                                        ui->vertices, sizeof(ui->vertices), BUDO_GPU_USAGE_DYNAMIC);
    ui->mesh = budo_mesh_create(host);
    return ui->program && ui->buffer && ui->mesh &&
           budo_shader_program_attribute_location(ui->program, "a_position", &position) == BUDO_STATUS_OK &&
           budo_shader_program_attribute_location(ui->program, "a_color", &color) == BUDO_STATUS_OK &&
           budo_mesh_set_vertex_buffer(ui->mesh, (uint32_t)position, ui->buffer, 2, BUDO_VERTEX_ATTRIBUTE_FLOAT32, sizeof(UiGpuVertex), 0) == BUDO_STATUS_OK &&
           budo_mesh_set_vertex_buffer(ui->mesh, (uint32_t)color, ui->buffer, 4, BUDO_VERTEX_ATTRIBUTE_FLOAT32, sizeof(UiGpuVertex), offsetof(UiGpuVertex, r)) == BUDO_STATUS_OK;
}

void ui_gpu_destroy(UiGpu *ui)
{
    if (ui->mesh)
        budo_mesh_destroy(ui->mesh);
    if (ui->buffer)
        budo_gpu_buffer_destroy(ui->buffer);
    if (ui->program)
        budo_shader_program_destroy(ui->program);
    memset(ui, 0, sizeof(*ui));
}
void ui_gpu_begin(UiGpu *ui, float width, float height)
{
    ui->count = 0;
    ui->width = width;
    ui->height = height;
}
void ui_gpu_rect(UiGpu *ui, float x, float y, float w, float h, uint32_t c)
{
    vertex(ui, x, y, c);
    vertex(ui, x + w, y, c);
    vertex(ui, x + w, y + h, c);
    vertex(ui, x, y, c);
    vertex(ui, x + w, y + h, c);
    vertex(ui, x, y + h, c);
}
void ui_gpu_line(UiGpu *ui, float x1, float y1, float x2, float y2, float width, uint32_t c)
{
    float dx = x2 - x1, dy = y2 - y1, l = hypotf(dx, dy);
    float nx, ny;
    if (l < .001f)
        return;
    nx = -dy / l * width * .5f;
    ny = dx / l * width * .5f;
    vertex(ui, x1 + nx, y1 + ny, c);
    vertex(ui, x2 + nx, y2 + ny, c);
    vertex(ui, x2 - nx, y2 - ny, c);
    vertex(ui, x1 + nx, y1 + ny, c);
    vertex(ui, x2 - nx, y2 - ny, c);
    vertex(ui, x1 - nx, y1 - ny, c);
}
void ui_gpu_circle(UiGpu *ui, float x, float y, float radius, float width, uint32_t c)
{
    int i;
    for (i = 0; i < 32; i++)
    {
        float a = i * 6.2831853f / 32, b = (i + 1) * 6.2831853f / 32;
        ui_gpu_line(ui, x + cosf(a) * radius, y + sinf(a) * radius, x + cosf(b) * radius, y + sinf(b) * radius, width, c);
    }
}
void ui_gpu_text(UiGpu *ui, float x, float y, float size, uint32_t c, const char *text)
{
    size *= 2.0f;
    float p = size / 7.0f, origin = x;
    while (text && *text)
    {
        unsigned char ch = (unsigned char)toupper((unsigned char)*text++);
        int col, row;
        if (ch == '\n')
        {
            x = origin;
            y += size + p * 2;
            continue;
        }
        if (ch < 32 || ch > 127)
            ch = '?';
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if (font[ch - 32][col] & (1u << row))
                    ui_gpu_rect(ui, x + col * p, y + row * p, p * .9f, p * .9f, c);
        x += p * 6;
    }
}
BudoStatus ui_gpu_draw(UiGpu *ui, BudoCanvas *canvas)
{
    BudoMeshDrawInfo draw = {0};
    if (!ui->count)
        return BUDO_STATUS_OK;
    if (budo_gpu_buffer_update(ui->buffer, 0, ui->vertices, ui->count * sizeof(UiGpuVertex)) != BUDO_STATUS_OK)
        return BUDO_STATUS_APPLICATION_ERROR;
    draw.struct_size = sizeof(draw);
    draw.primitive = BUDO_PRIMITIVE_TRIANGLES;
    draw.count = ui->count;
    draw.depth_test = false;
    draw.depth_write = false;
    draw.cull = BUDO_CULL_NONE;
    draw.blend = BUDO_BLEND_ALPHA;
    return budo_canvas_draw_mesh(canvas, ui->program, ui->mesh, &draw);
}