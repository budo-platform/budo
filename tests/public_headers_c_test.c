#include <budo/budo.h>

#include <assert.h>

typedef BudoStatus (*BudoRegionPassFunction)(
    BudoCanvas *, BudoShaderProgram *, BudoRenderTarget *, BudoRenderTarget *,
    int32_t, int32_t, uint32_t, uint32_t);

_Static_assert(_Generic(&budo_canvas_draw_region_pass,
                        BudoRegionPassFunction: 1,
                        default: 0),
               "Region pass must retain a C-compatible signature");

int main(void)
{
    BudoSurfaceInfo surface = {0};
    BudoPointer pointer = {0};
    BudoGraphicsCapabilities graphics = {0};
    BudoMeshDrawInfo draw = {0};
    BudoColor color = BUDO_COLOR_ARGB(255, 1, 2, 3);
    surface.struct_size = sizeof(surface);
    assert(surface.struct_size > 0);
    assert(sizeof(pointer.x) == sizeof(int32_t));
    assert(color == UINT32_C(0xff010203));
    assert(BUDO_PAINT_FILL == 0);
    graphics.struct_size = sizeof(graphics);
    draw.struct_size = sizeof(draw);
    draw.primitive = BUDO_PRIMITIVE_TRIANGLES;
    assert(BUDO_GRAPHICS_FEATURE_MESH_DRAWING != 0);
    assert(BUDO_GRAPHICS_FEATURE_SHADER_FILES != 0);
    assert(BUDO_GRAPHICS_FEATURE_ENCODED_IMAGE_TEXTURES != 0);
    assert(BUDO_GRAPHICS_FEATURE_RENDER_TARGET_RGBA8 != 0);
    assert(BUDO_GRAPHICS_FEATURE_FULLSCREEN_PASSES != 0);
    assert(BUDO_GRAPHICS_FEATURE_REGION_PASSES != 0);
    assert(BUDO_NATIVE_API_VERSION_MINOR == 5u);
    assert(BUDO_GPU_BUFFER_VERTEX == 0);
    assert(graphics.struct_size > 0 && draw.struct_size > 0);
    return 0;
}