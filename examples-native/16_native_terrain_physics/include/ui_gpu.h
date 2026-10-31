#ifndef NATIVE_TERRAIN_UI_GPU_H
#define NATIVE_TERRAIN_UI_GPU_H

#include <budo/budo.h>

#define UI_GPU_MAX_VERTICES 30000

typedef struct UiGpuVertex
{
    float x, y;
    float r, g, b, a;
} UiGpuVertex;

typedef struct UiGpu
{
    BudoShaderProgram *program;
    BudoGpuBuffer *buffer;
    BudoMesh *mesh;
    UiGpuVertex vertices[UI_GPU_MAX_VERTICES];
    uint32_t count;
    float width;
    float height;
} UiGpu;

bool ui_gpu_create(UiGpu *ui, BudoHost *host);
void ui_gpu_destroy(UiGpu *ui);
void ui_gpu_begin(UiGpu *ui, float width, float height);
void ui_gpu_rect(UiGpu *ui, float x, float y, float width, float height,
                 uint32_t color);
void ui_gpu_line(UiGpu *ui, float x1, float y1, float x2, float y2,
                 float width, uint32_t color);
void ui_gpu_circle(UiGpu *ui, float x, float y, float radius, float width,
                   uint32_t color);
void ui_gpu_text(UiGpu *ui, float x, float y, float size, uint32_t color,
                 const char *text);
BudoStatus ui_gpu_draw(UiGpu *ui, BudoCanvas *canvas);

#endif