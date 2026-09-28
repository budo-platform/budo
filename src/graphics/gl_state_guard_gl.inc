#include "graphics/gl_state_guard.h"

static void budo_gl_guard_get_integer(uint32_t name, int *values)
{
    glGetIntegerv((GLenum)name, (GLint *)values);
}

static void budo_gl_guard_get_boolean(uint32_t name, unsigned char *values)
{
    glGetBooleanv((GLenum)name, (GLboolean *)values);
}

static bool budo_gl_guard_is_enabled(uint32_t capability)
{
    return glIsEnabled((GLenum)capability) == GL_TRUE;
}

static void budo_gl_guard_set_enabled(uint32_t capability, bool enabled)
{
    if (enabled)
        glEnable((GLenum)capability);
    else
        glDisable((GLenum)capability);
}

static void budo_gl_guard_bind_framebuffer(uint32_t target, uint32_t framebuffer)
{
    glBindFramebuffer((GLenum)target, (GLuint)framebuffer);
}

static void budo_gl_guard_set_viewport(int x, int y, int width, int height)
{
    glViewport(x, y, width, height);
}

static void budo_gl_guard_use_program(uint32_t program)
{
    glUseProgram((GLuint)program);
}

static void budo_gl_guard_bind_vertex_array(uint32_t vertex_array)
{
    glBindVertexArray((GLuint)vertex_array);
}

static void budo_gl_guard_bind_buffer(uint32_t target, uint32_t buffer)
{
    glBindBuffer((GLenum)target, (GLuint)buffer);
}

static void budo_gl_guard_bind_renderbuffer(uint32_t target, uint32_t renderbuffer)
{
    glBindRenderbuffer((GLenum)target, (GLuint)renderbuffer);
}

static void budo_gl_guard_active_texture(uint32_t texture)
{
    glActiveTexture((GLenum)texture);
}

static void budo_gl_guard_bind_texture(uint32_t target, uint32_t texture)
{
    glBindTexture((GLenum)target, (GLuint)texture);
}

static void budo_gl_guard_set_blend_func(uint32_t src_rgb, uint32_t dst_rgb,
                                         uint32_t src_alpha, uint32_t dst_alpha)
{
    glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb,
                        (GLenum)src_alpha, (GLenum)dst_alpha);
}

static void budo_gl_guard_set_blend_equation(uint32_t rgb, uint32_t alpha)
{
    glBlendEquationSeparate((GLenum)rgb, (GLenum)alpha);
}

static void budo_gl_guard_set_depth_func(uint32_t function)
{
    glDepthFunc((GLenum)function);
}

static void budo_gl_guard_set_cull_face(uint32_t mode)
{
    glCullFace((GLenum)mode);
}

static void budo_gl_guard_set_front_face(uint32_t mode)
{
    glFrontFace((GLenum)mode);
}

static void budo_gl_guard_set_scissor(int x, int y, int width, int height)
{
    glScissor(x, y, width, height);
}

static void budo_gl_guard_set_color_mask(bool red, bool green, bool blue,
                                         bool alpha)
{
    glColorMask(red ? GL_TRUE : GL_FALSE, green ? GL_TRUE : GL_FALSE,
                blue ? GL_TRUE : GL_FALSE, alpha ? GL_TRUE : GL_FALSE);
}

static void budo_gl_guard_set_depth_mask(bool enabled)
{
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

static const GlStateGuardApi budo_gl_state_guard_api = {
    budo_gl_guard_get_integer,
    budo_gl_guard_get_boolean,
    budo_gl_guard_is_enabled,
    budo_gl_guard_set_enabled,
    budo_gl_guard_bind_framebuffer,
    budo_gl_guard_set_viewport,
    budo_gl_guard_use_program,
    budo_gl_guard_bind_vertex_array,
    budo_gl_guard_bind_buffer,
    budo_gl_guard_bind_renderbuffer,
    budo_gl_guard_active_texture,
    budo_gl_guard_bind_texture,
    budo_gl_guard_set_blend_func,
    budo_gl_guard_set_blend_equation,
    budo_gl_guard_set_depth_func,
    budo_gl_guard_set_cull_face,
    budo_gl_guard_set_front_face,
    budo_gl_guard_set_scissor,
    budo_gl_guard_set_color_mask,
    budo_gl_guard_set_depth_mask,
};

static const GlStateGuardConstants budo_gl_state_guard_constants = {
    GL_FRAMEBUFFER_BINDING,
    GL_FRAMEBUFFER,
    GL_VIEWPORT,
    GL_CURRENT_PROGRAM,
    GL_VERTEX_ARRAY_BINDING,
    GL_ARRAY_BUFFER_BINDING,
    GL_ARRAY_BUFFER,
    GL_ELEMENT_ARRAY_BUFFER_BINDING,
    GL_ELEMENT_ARRAY_BUFFER,
    GL_RENDERBUFFER_BINDING,
    GL_RENDERBUFFER,
    GL_ACTIVE_TEXTURE,
    GL_TEXTURE0,
    GL_TEXTURE_BINDING_2D,
    GL_TEXTURE_2D,
    GL_TEXTURE_BINDING_CUBE_MAP,
    GL_TEXTURE_CUBE_MAP,
    GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
    GL_BLEND,
    GL_DEPTH_TEST,
    GL_CULL_FACE,
    GL_SCISSOR_TEST,
    GL_BLEND_SRC_RGB,
    GL_BLEND_DST_RGB,
    GL_BLEND_SRC_ALPHA,
    GL_BLEND_DST_ALPHA,
    GL_BLEND_EQUATION_RGB,
    GL_BLEND_EQUATION_ALPHA,
    GL_DEPTH_FUNC,
    GL_CULL_FACE_MODE,
    GL_FRONT_FACE,
    GL_SCISSOR_BOX,
    GL_COLOR_WRITEMASK,
    GL_DEPTH_WRITEMASK,
};

static bool budo_gl_state_guard_run(GlStateGuardTransition transition,
                                    void *context)
{
    return gl_state_guard_run(&budo_gl_state_guard_api,
                              &budo_gl_state_guard_constants,
                              transition, context);
}
