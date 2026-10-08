/*
 * GL resource limits and slot types shared by the desktop, web, and Android
 * window backends. Include after the platform GL headers and before the
 * backend's struct Window, which embeds these tables.
 */

#include "graphics/gpu_resource_state.h"

#define MAX_GL_PROGRAMS 128
#define MAX_GL_UNIFORM_CACHE 64
#define MAX_GL_RENDER_TARGETS 32
#define MAX_GL_BUFFERS 256
#define MAX_GL_TEXTURES 128
#define MAX_GL_VERTEX_LAYOUTS 64
#define MAX_GL_LAYOUT_ATTRS 8

typedef struct
{
    bool in_use;
    char name[64];
    GLint location;
} GLUniformCacheEntry;

typedef struct
{
    bool in_use;
    GLuint program;
    GLint sampler_loc;
    GLint resolution_loc;
    GLint time_loc;
    GLint position_loc;
    GLint tex_coord_loc;
    GLUniformCacheEntry uniform_cache[MAX_GL_UNIFORM_CACHE];
} GLProgramSlot;

typedef enum
{
    GL_UNIFORM_1I = 0,
    GL_UNIFORM_1F,
    GL_UNIFORM_2F,
    GL_UNIFORM_3F,
    GL_UNIFORM_4F,
    GL_UNIFORM_1IV,
    GL_UNIFORM_1FV,
    GL_UNIFORM_2FV,
    GL_UNIFORM_3FV,
    GL_UNIFORM_4FV,
    GL_UNIFORM_MAT3FV,
    GL_UNIFORM_MAT4FV
} GLUniformKind;

typedef GpuRenderTargetSlot GLRenderTarget;

/* The framebuffer a draw goes to: the screen or an app render target. */
typedef struct
{
    bool is_screen;
    int width;
    int height;
    bool has_depth;
    GLRenderTarget *slot; /* the app render target, NULL for the screen */
} GLBoundRenderTarget;
typedef GpuBufferSlot GLBufferSlot;
typedef GpuTextureSlot GLTextureSlot;

typedef struct
{
    bool active;
    int location;
    int buffer_id;
    int size; /* components per attribute (1..4) */
    GLenum gl_type;
    bool normalized;
    int stride;
    int offset;
    int divisor;
} GLVertexAttribDesc;

typedef struct
{
    bool in_use;
    GLuint vao;
    GLVertexAttribDesc attrs[MAX_GL_LAYOUT_ATTRS];
    int attr_count;
    int index_buffer_id;  /* 0 = no index buffer */
    GLenum index_gl_type; /* GL_UNSIGNED_SHORT or GL_UNSIGNED_INT */
    bool needs_rebind;
} GLVertexLayout;
