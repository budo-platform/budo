#include "video/video_texture.h"
#include "core/window.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static int created;
static int updated;
static int destroyed;
static int fail_update;
static int fail_destroy_id;
static int next_texture_id = 10;

int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    (void)window;
    assert(width > 0 && height > 0);
    assert(format == WINDOW_GL_TEX_RGBA8);
    assert(pixels);
    created++;
    return next_texture_id++;
}

bool window_gl_update_texture_2d(Window *window, int texture_id, int x, int y,
                                 int width, int height, const void *pixels)
{
    (void)window;
    assert(texture_id > 0);
    assert(x == 0 && y == 0);
    assert(width > 0 && height == 2);
    assert(pixels);
    updated++;
    return !fail_update;
}

bool window_gl_destroy_texture(Window *window, int texture_id)
{
    (void)window;
    assert(texture_id > 0);
    destroyed++;
    return texture_id != fail_destroy_id;
}

int main(void)
{
    uint8_t pixels[3 * 2 * 4] = {0};
    char error[128];
    VideoTexture texture = {0};
    VideoFrame frame = {
        .rgba = pixels,
        .size = 2 * 2 * 4,
        .width = 2,
        .height = 2,
        .stride = 2 * 4,
    };

    assert(video_texture_upload((Window *)1, &texture, &frame, error, sizeof(error)));
    assert(error[0] == '\0');
    assert(texture.texture_id == 10 && texture.width == 2 && texture.height == 2);
    assert(created == 1 && updated == 0 && destroyed == 0);

    assert(video_texture_upload((Window *)1, &texture, &frame, error, sizeof(error)));
    assert(texture.texture_id == 10 && created == 1 && updated == 1);
    fail_update = 1;
    assert(!video_texture_upload((Window *)1, &texture, &frame, error, sizeof(error)));
    assert(texture.texture_id == 10 && texture.width == 2 && updated == 2);
    fail_update = 0;

    frame.width = 3;
    frame.size = sizeof(pixels);
    frame.stride = 3 * 4;
    assert(video_texture_upload((Window *)1, &texture, &frame, error, sizeof(error)));
    assert(texture.texture_id == 11 && texture.width == 3 && texture.height == 2);
    assert(created == 2 && destroyed == 1);

    frame.size--;
    assert(!video_texture_upload((Window *)1, &texture, &frame, error, sizeof(error)));
    assert(strcmp(error, "Video frame is not tightly packed RGBA8") == 0);
    assert(texture.texture_id == 11 && created == 2 && destroyed == 1);
    frame.size++;

    fail_destroy_id = 11;
    frame.width = 2;
    frame.size = 2 * 2 * 4;
    frame.stride = 2 * 4;
    assert(!video_texture_upload((Window *)1, &texture, &frame, error, sizeof(error)));
    assert(texture.texture_id == 11 && texture.width == 3);
    fail_destroy_id = 0;

    assert(!video_texture_close(NULL, &texture));
    assert(texture.texture_id == 11 && texture.width == 3 && texture.height == 2);

    assert(video_texture_close((Window *)1, &texture));
    assert(texture.texture_id == 0 && texture.width == 0 && texture.height == 0);
    assert(video_texture_close((Window *)1, &texture));
    assert(destroyed == 4);
    return 0;
}