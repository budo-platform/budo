#include "graphics/gl_state_guard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <stdbool.h>
#include <stdint.h>

enum
{
    C_FRAMEBUFFER_BINDING = 1,
    C_FRAMEBUFFER,
    C_VIEWPORT,
    C_PROGRAM,
    C_VAO_BINDING,
    C_ARRAY_BUFFER_BINDING,
    C_ARRAY_BUFFER,
    C_ELEMENT_BUFFER_BINDING,
    C_ELEMENT_BUFFER,
    C_RENDERBUFFER_BINDING,
    C_RENDERBUFFER,
    C_ACTIVE_TEXTURE,
    C_TEXTURE_BINDING_2D,
    C_TEXTURE_2D,
    C_TEXTURE_BINDING_CUBE,
    C_TEXTURE_CUBE,
    C_MAX_TEXTURE_UNITS,
    C_BLEND,
    C_DEPTH_TEST,
    C_CULL_FACE,
    C_SCISSOR_TEST,
    C_BLEND_SRC_RGB,
    C_BLEND_DST_RGB,
    C_BLEND_SRC_ALPHA,
    C_BLEND_DST_ALPHA,
    C_BLEND_EQ_RGB,
    C_BLEND_EQ_ALPHA,
    C_DEPTH_FUNC,
    C_CULL_MODE,
    C_FRONT_FACE,
    C_SCISSOR_BOX,
    C_COLOR_MASK,
    C_DEPTH_MASK,
};

#define C_TEXTURE0 100u

typedef struct FakeGlState
{
    int framebuffer;
    int viewport[4];
    int program;
    int vao;
    int array_buffer;
    int element_buffer;
    int renderbuffer;
    int active_texture;
    int texture_2d[3];
    int texture_cube[3];
    bool blend;
    bool depth;
    bool cull;
    bool scissor;
    int blend_src_rgb;
    int blend_dst_rgb;
    int blend_src_alpha;
    int blend_dst_alpha;
    int blend_eq_rgb;
    int blend_eq_alpha;
    int depth_func;
    int cull_mode;
    int front_face;
    int scissor_box[4];
    unsigned char color_mask[4];
    unsigned char depth_mask;
} FakeGlState;

static FakeGlState state;
static bool transition_called;

static int active_unit(void)
{
    return state.active_texture - C_TEXTURE0;
}

static void fake_get_integer(uint32_t name, int *values)
{
    switch (name)
    {
    case C_FRAMEBUFFER_BINDING: *values = state.framebuffer; break;
    case C_VIEWPORT: memcpy(values, state.viewport, sizeof(state.viewport)); break;
    case C_PROGRAM: *values = state.program; break;
    case C_VAO_BINDING: *values = state.vao; break;
    case C_ARRAY_BUFFER_BINDING: *values = state.array_buffer; break;
    case C_ELEMENT_BUFFER_BINDING: *values = state.element_buffer; break;
    case C_RENDERBUFFER_BINDING: *values = state.renderbuffer; break;
    case C_ACTIVE_TEXTURE: *values = state.active_texture; break;
    case C_MAX_TEXTURE_UNITS: *values = 3; break;
    case C_TEXTURE_BINDING_2D: *values = state.texture_2d[active_unit()]; break;
    case C_TEXTURE_BINDING_CUBE: *values = state.texture_cube[active_unit()]; break;
    case C_BLEND_SRC_RGB: *values = state.blend_src_rgb; break;
    case C_BLEND_DST_RGB: *values = state.blend_dst_rgb; break;
    case C_BLEND_SRC_ALPHA: *values = state.blend_src_alpha; break;
    case C_BLEND_DST_ALPHA: *values = state.blend_dst_alpha; break;
    case C_BLEND_EQ_RGB: *values = state.blend_eq_rgb; break;
    case C_BLEND_EQ_ALPHA: *values = state.blend_eq_alpha; break;
    case C_DEPTH_FUNC: *values = state.depth_func; break;
    case C_CULL_MODE: *values = state.cull_mode; break;
    case C_FRONT_FACE: *values = state.front_face; break;
    case C_SCISSOR_BOX: memcpy(values, state.scissor_box, sizeof(state.scissor_box)); break;
    default: assert(false);
    }
}

static void fake_get_boolean(uint32_t name, unsigned char *values)
{
    if (name == C_COLOR_MASK)
        memcpy(values, state.color_mask, sizeof(state.color_mask));
    else if (name == C_DEPTH_MASK)
        *values = state.depth_mask;
    else
        assert(false);
}

static bool fake_is_enabled(uint32_t capability)
{
    switch (capability)
    {
    case C_BLEND: return state.blend;
    case C_DEPTH_TEST: return state.depth;
    case C_CULL_FACE: return state.cull;
    case C_SCISSOR_TEST: return state.scissor;
    default: assert(false); return false;
    }
}

static void fake_set_enabled(uint32_t capability, bool enabled)
{
    switch (capability)
    {
    case C_BLEND: state.blend = enabled; break;
    case C_DEPTH_TEST: state.depth = enabled; break;
    case C_CULL_FACE: state.cull = enabled; break;
    case C_SCISSOR_TEST: state.scissor = enabled; break;
    default: assert(false);
    }
}

static void fake_bind_framebuffer(uint32_t target, uint32_t value)
{ assert(target == C_FRAMEBUFFER); state.framebuffer = (int)value; }
static void fake_viewport(int x, int y, int w, int h)
{ state.viewport[0] = x; state.viewport[1] = y; state.viewport[2] = w; state.viewport[3] = h; }
static void fake_use_program(uint32_t value) { state.program = (int)value; }
static void fake_bind_vao(uint32_t value) { state.vao = (int)value; }
static void fake_bind_buffer(uint32_t target, uint32_t value)
{
    if (target == C_ARRAY_BUFFER) state.array_buffer = (int)value;
    else if (target == C_ELEMENT_BUFFER) state.element_buffer = (int)value;
    else assert(false);
}
static void fake_bind_renderbuffer(uint32_t target, uint32_t value)
{ assert(target == C_RENDERBUFFER); state.renderbuffer = (int)value; }
static void fake_active_texture(uint32_t value) { state.active_texture = (int)value; }
static void fake_bind_texture(uint32_t target, uint32_t value)
{
    if (target == C_TEXTURE_2D) state.texture_2d[active_unit()] = (int)value;
    else if (target == C_TEXTURE_CUBE) state.texture_cube[active_unit()] = (int)value;
    else assert(false);
}
static void fake_blend_func(uint32_t sr, uint32_t dr, uint32_t sa, uint32_t da)
{ state.blend_src_rgb = sr; state.blend_dst_rgb = dr; state.blend_src_alpha = sa; state.blend_dst_alpha = da; }
static void fake_blend_equation(uint32_t rgb, uint32_t alpha)
{ state.blend_eq_rgb = rgb; state.blend_eq_alpha = alpha; }
static void fake_depth_func(uint32_t value) { state.depth_func = (int)value; }
static void fake_cull_face(uint32_t value) { state.cull_mode = (int)value; }
static void fake_front_face(uint32_t value) { state.front_face = (int)value; }
static void fake_scissor(int x, int y, int w, int h)
{ state.scissor_box[0] = x; state.scissor_box[1] = y; state.scissor_box[2] = w; state.scissor_box[3] = h; }
static void fake_color_mask(bool r, bool g, bool b, bool a)
{ state.color_mask[0] = r; state.color_mask[1] = g; state.color_mask[2] = b; state.color_mask[3] = a; }
static void fake_depth_mask(bool value) { state.depth_mask = value; }

static const GlStateGuardApi api = {
    fake_get_integer, fake_get_boolean, fake_is_enabled, fake_set_enabled,
    fake_bind_framebuffer, fake_viewport, fake_use_program, fake_bind_vao,
    fake_bind_buffer, fake_bind_renderbuffer, fake_active_texture,
    fake_bind_texture, fake_blend_func, fake_blend_equation, fake_depth_func,
    fake_cull_face, fake_front_face, fake_scissor, fake_color_mask,
    fake_depth_mask,
};

static const GlStateGuardConstants constants = {
    C_FRAMEBUFFER_BINDING, C_FRAMEBUFFER, C_VIEWPORT, C_PROGRAM,
    C_VAO_BINDING, C_ARRAY_BUFFER_BINDING, C_ARRAY_BUFFER,
    C_ELEMENT_BUFFER_BINDING, C_ELEMENT_BUFFER, C_RENDERBUFFER_BINDING,
    C_RENDERBUFFER, C_ACTIVE_TEXTURE, C_TEXTURE0, C_TEXTURE_BINDING_2D,
    C_TEXTURE_2D, C_TEXTURE_BINDING_CUBE, C_TEXTURE_CUBE,
    C_MAX_TEXTURE_UNITS, C_BLEND, C_DEPTH_TEST, C_CULL_FACE,
    C_SCISSOR_TEST, C_BLEND_SRC_RGB, C_BLEND_DST_RGB, C_BLEND_SRC_ALPHA,
    C_BLEND_DST_ALPHA, C_BLEND_EQ_RGB, C_BLEND_EQ_ALPHA, C_DEPTH_FUNC,
    C_CULL_MODE, C_FRONT_FACE, C_SCISSOR_BOX, C_COLOR_MASK, C_DEPTH_MASK,
};

static void mutate_everything(void *context)
{
    (void)context;
    transition_called = true;
    memset(&state, 0, sizeof(state));
    state.active_texture = C_TEXTURE0;
}

int main(void)
{
    FakeGlState expected = {
        .framebuffer = 7,
        .viewport = {11, 12, 640, 480},
        .program = 13,
        .vao = 14,
        .array_buffer = 15,
        .element_buffer = 16,
        .renderbuffer = 17,
        .active_texture = C_TEXTURE0 + 2,
        .texture_2d = {21, 22, 23},
        .texture_cube = {31, 32, 33},
        .blend = true,
        .depth = false,
        .cull = true,
        .scissor = true,
        .blend_src_rgb = 41,
        .blend_dst_rgb = 42,
        .blend_src_alpha = 43,
        .blend_dst_alpha = 44,
        .blend_eq_rgb = 45,
        .blend_eq_alpha = 46,
        .depth_func = 47,
        .cull_mode = 48,
        .front_face = 49,
        .scissor_box = {50, 51, 52, 53},
        .color_mask = {1, 0, 1, 0},
        .depth_mask = 1,
    };
    state = expected;
    assert(gl_state_guard_run(&api, &constants, mutate_everything, NULL));
    assert(transition_called);
    assert(memcmp(&state, &expected, sizeof(state)) == 0);
    assert(!gl_state_guard_run(NULL, &constants, mutate_everything, NULL));
    puts("GL state guard tests passed");
    return 0;
}