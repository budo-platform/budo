#include "graphics/gl_render_target.h"

#include <stdio.h>

static uint32_t framebuffer_status;
static int deleted_textures;
static int deleted_framebuffers;
static int deleted_renderbuffers;
static uint32_t next_id;

static void generate(int count, uint32_t *ids)
{
    while (count-- > 0)
        *ids++ = ++next_id;
}
static void bind(uint32_t target, uint32_t id)
{
    (void)target;
    (void)id;
}
static void parameter(uint32_t target, uint32_t name, int value)
{
    (void)target;
    (void)name;
    (void)value;
}
static void image(uint32_t target, int level, int internal, int width,
                  int height, int border, uint32_t format, uint32_t type,
                  const void *pixels)
{
    (void)target;
    (void)level;
    (void)internal;
    (void)width;
    (void)height;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
}
static void framebuffer_texture(uint32_t target, uint32_t attachment,
                                uint32_t texture_target, uint32_t texture,
                                int level)
{
    (void)target;
    (void)attachment;
    (void)texture_target;
    (void)texture;
    (void)level;
}
static uint32_t check_framebuffer(uint32_t target)
{
    (void)target;
    return framebuffer_status;
}
static void storage(uint32_t target, uint32_t format, int width, int height)
{
    (void)target;
    (void)format;
    (void)width;
    (void)height;
}
static void framebuffer_renderbuffer(uint32_t target, uint32_t attachment,
                                     uint32_t renderbuffer_target,
                                     uint32_t renderbuffer)
{
    (void)target;
    (void)attachment;
    (void)renderbuffer_target;
    (void)renderbuffer;
}
static void delete_texture(int count, const uint32_t *ids)
{
    (void)ids;
    deleted_textures += count;
}
static void delete_framebuffer(int count, const uint32_t *ids)
{
    (void)ids;
    deleted_framebuffers += count;
}
static void delete_renderbuffer(int count, const uint32_t *ids)
{
    (void)ids;
    deleted_renderbuffers += count;
}

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "gl_render_target_test: %s\n", message);
    return 1;
}

int main(void)
{
    const GlRenderTargetApi api = {
        generate, bind, parameter, image, delete_texture,
        generate, bind, framebuffer_texture, check_framebuffer,
        delete_framebuffer, generate, bind, storage,
        framebuffer_renderbuffer, delete_renderbuffer};
    const GlRenderTargetConstants constants = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99};
    GpuRenderTargetSlot slot = {0};

    framebuffer_status = 99;
    if (check(gl_render_target_create(&api, &constants, &slot, 320, 200, true),
              "valid target creation failed") ||
        check(slot.in_use && slot.width == 320 && slot.height == 200 &&
                  slot.texture && slot.fbo && slot.depth_rbo,
              "created target state differs") ||
        check(gl_render_target_resize(&api, &constants, &slot, 640, 480),
              "valid resize failed") ||
        check(slot.width == 640 && slot.height == 480,
              "resize dimensions were not committed"))
        return 1;

    gl_render_target_destroy(&api, &slot);
    if (check(!slot.in_use && deleted_textures == 1 &&
                  deleted_framebuffers == 1 && deleted_renderbuffers == 1,
              "destroy did not release and clear all resources"))
        return 1;

    framebuffer_status = 0;
    if (check(!gl_render_target_create(&api, &constants, &slot, 64, 64, true),
              "incomplete framebuffer was accepted") ||
        check(!slot.in_use && slot.texture == 0 && slot.fbo == 0 &&
                  slot.depth_rbo == 0,
              "failed creation did not roll back slot state"))
        return 1;

    puts("gl_render_target_test: ok");
    return 0;
}