#include <budo/budo.h>

#include <type_traits>

static_assert(std::is_standard_layout<BudoApplication>::value,
              "The public descriptor must remain a C-compatible layout");
static_assert(std::is_same<decltype(&budo_host_log),
                           void (*)(BudoHost *, BudoLogLevel,
                                    const char *)>::value,
              "Public functions must retain C-compatible signatures");
static_assert(std::is_standard_layout<BudoGraphicsCapabilities>::value,
              "Graphics capabilities must remain a C-compatible layout");
static_assert(std::is_standard_layout<BudoMeshDrawInfo>::value,
              "Mesh draw information must remain a C-compatible layout");
static_assert(std::is_same<decltype(&budo_texture_2d_get_size),
                           BudoStatus (*)(BudoTexture *, uint32_t *,
                                          uint32_t *)>::value,
              "Texture size query must retain a C-compatible signature");
static_assert(std::is_same<decltype(&budo_canvas_draw_fullscreen_pass),
                           BudoStatus (*)(BudoCanvas *, BudoShaderProgram *,
                                          BudoRenderTarget *,
                                          BudoRenderTarget *)>::value,
              "Fullscreen pass must retain a C-compatible signature");
static_assert(std::is_same<decltype(&budo_canvas_draw_region_pass),
                           BudoStatus (*)(BudoCanvas *, BudoShaderProgram *,
                                          BudoRenderTarget *,
                                          BudoRenderTarget *, int32_t, int32_t,
                                          uint32_t, uint32_t)>::value,
              "Region pass must retain a C-compatible signature");
static_assert(BUDO_NATIVE_API_VERSION_MINOR == 5u,
              "Public native API version should cover window-less applications");

int main()
{
    BudoFrameInfo frame{};
    frame.struct_size = sizeof(frame);
    return frame.struct_size == 0;
}
