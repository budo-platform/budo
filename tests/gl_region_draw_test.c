#include "core/gl_desktop.h"
#include "core/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char vertex_shader[] =
    "#version 300 es\n"
    "in vec2 a_position;\n"
    "in vec2 a_texCoord;\n"
    "out vec2 uv;\n"
    "void main() { uv = a_texCoord; gl_Position = vec4(a_position, 0.0, 1.0); }\n";

static const char fragment_shader[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() { color = vec4(uv, 0.0, 1.0); }\n";

int main(void)
{
    WindowConfig config = {.title = "Region draw test", .project_dir = ".",
                           .width = 64, .height = 64};
    Window *window = window_create(&config);
    unsigned char bottom_left[4], top_right[4], outside[4], alpha_pixel[4];
    int program, target;
    GLenum error;
    GLuint pack_buffer;
    GLint target_fbo, target_texture, value, viewport[4];
    int width, height;
    uint8_t *pixels = NULL;
    int result = 1;

    if (!window)
    {
        fprintf(stderr, "gl_region_draw_test: could not create a GL window\n");
        return 77;
    }

    program = window_gl_create_program_from_source(
        window, vertex_shader, strlen(vertex_shader),
        fragment_shader, strlen(fragment_shader));
    target = window_gl_create_render_target(window, 64, 64, false);
    if (program <= 0 || target <= 0 ||
        !window_gl_bind_render_target_immediate(window, target))
    {
        fprintf(stderr, "gl_region_draw_test: setup failed: %s\n", window_gl_get_error(window));
        goto done;
    }

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        fprintf(stderr, "gl_region_draw_test: setup GL error 0x%x\n", error);
        goto done;
    }
    if (!window_gl_draw_region_immediate(window, program, 16, 16, 32, 32, target))
    {
        fprintf(stderr, "gl_region_draw_test: draw failed: %s\n", window_gl_get_error(window));
        goto done;
    }
    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        fprintf(stderr, "gl_region_draw_test: GL error 0x%x\n", error);
        goto done;
    }
    if (!window_gl_bind_render_target_immediate(window, target))
    {
        fprintf(stderr, "gl_region_draw_test: binding failed: %s\n", window_gl_get_error(window));
        goto done;
    }
    glReadPixels(17, 17, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bottom_left);
    glReadPixels(46, 46, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top_right);
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        fprintf(stderr, "gl_region_draw_test: readback GL error 0x%x\n", error);
        goto done;
    }
    if (bottom_left[0] > 40 || bottom_left[1] < 215 ||
        top_right[0] < 215 || top_right[1] > 40 ||
        outside[0] != 0 || outside[1] != 0 || outside[2] != 0)
    {
        fprintf(stderr, "gl_region_draw_test: unexpected region pixels: "
                "%u,%u %u,%u outside %u,%u,%u\n",
                bottom_left[0], bottom_left[1], top_right[0], top_right[1],
                outside[0], outside[1], outside[2]);
        goto done;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1, 1);
    glClearColor(0.7f, 0.2f, 0.3f, 0.25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha_pixel);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &target_fbo);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          &target_texture);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glViewport(3, 4, 17, 19);
    glGenBuffers(1, &pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER, 65536, NULL, GL_STREAM_READ);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 70);
    glPixelStorei(GL_PACK_SKIP_ROWS, 2);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, 0, 0);
    if (window_gl_read_render_target_pixels(window, target, &width, &height, &pixels))
    {
        fprintf(stderr, "gl_region_draw_test: incomplete target accepted\n");
        goto done;
    }
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value);
    if (value != 0) goto state_failure;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &value);
    if ((GLuint)value != pack_buffer) goto state_failure;
    glGetIntegerv(GL_PACK_SKIP_ROWS, &value);
    if (value != 2) goto state_failure;
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, (GLuint)target_texture, 0);
    if (!window_gl_read_render_target_pixels(window, target, &width, &height, &pixels) ||
        width != 64 || height != 64 ||
        memcmp(pixels + ((63 - 17) * 64 + 17) * 4, bottom_left, 4) ||
        memcmp(pixels + ((63 - 46) * 64 + 46) * 4, top_right, 4) ||
        alpha_pixel[3] != 64 ||
        memcmp(pixels + 63 * 64 * 4, alpha_pixel, 4))
    {
        fprintf(stderr, "gl_region_draw_test: target readback failed: %s\n", window_gl_get_error(window));
        goto done;
    }
    free(pixels);
    pixels = NULL;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value);
    if (value != 0) goto state_failure;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    if (value != target_fbo) goto state_failure;
    glGetIntegerv(GL_READ_BUFFER, &value);
    if (value != GL_BACK) goto state_failure;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &value);
    if ((GLuint)value != pack_buffer) goto state_failure;
    glGetIntegerv(GL_PACK_ALIGNMENT, &value);
    if (value != 8) goto state_failure;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &value);
    if (value != 70) goto state_failure;
    glGetIntegerv(GL_PACK_SKIP_ROWS, &value);
    if (value != 2) goto state_failure;
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &value);
    if (value != 3) goto state_failure;
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[0] != 3 || viewport[1] != 4 ||
        viewport[2] != 17 || viewport[3] != 19) goto state_failure;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)target_fbo);
    glGetIntegerv(GL_READ_BUFFER, &value);
    if (value != GL_NONE) goto state_failure;
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glDeleteBuffers(1, &pack_buffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    if (!window_gl_resize_render_target(window, target, 7, 5) ||
        !window_gl_read_render_target_pixels(window, target, &width, &height, &pixels) ||
        width != 7 || height != 5)
    {
        fprintf(stderr, "gl_region_draw_test: resized target readback failed\n");
        goto done;
    }
    free(pixels);
    pixels = NULL;
    if (!window_gl_destroy_render_target(window, target) ||
        window_gl_read_render_target_pixels(window, target, &width, &height, &pixels) ||
        window_gl_read_render_target_pixels(window, 0, &width, &height, &pixels))
    {
        fprintf(stderr, "gl_region_draw_test: invalid target accepted\n");
        goto done;
    }
    result = 0;
    goto done;
state_failure:
    fprintf(stderr, "gl_region_draw_test: readback did not preserve GL state\n");
done:
    free(pixels);
    window_destroy(window);
    return result;
}