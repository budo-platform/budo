/*
 * GL resource management shared by the desktop, web, and Android window
 * backends: slot lookup, buffers, textures, vertex layouts, uniform caching,
 * mesh/region draw entry points, file loading, and program/texture creation
 * over the tables declared in gl_window_types.inc.
 *
 * Include once, after the backend's struct Window. The backend must define:
 *   BUDO_GL_WINDOW_READY(window)        true when GL calls may be issued
 *   BUDO_GL_WINDOW_MAKE_CURRENT(window) make the window's GL context current
 * and provide the usual window_* API declarations (window.h).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helper: set error message ───────────────────────────────────────── */

static void set_error(Window *window, const char *message)
{
    if (!window || !message)
        return;
    strncpy(window->error_msg, message, sizeof(window->error_msg) - 1);
    window->error_msg[sizeof(window->error_msg) - 1] = '\0';
}

static GLProgramSlot *get_program_slot(Window *window, int program_id)
{
    if (!window || program_id <= 0 || program_id > MAX_GL_PROGRAMS)
        return NULL;
    if (!window->programs[program_id - 1].in_use)
        return NULL;
    return &window->programs[program_id - 1];
}

static void flush_skia_canvas_transition(void *opaque)
{
    skia_canvas_flush((SkiaCanvas *)opaque);
}

static void flush_skia_canvas_preserving_gl_state(SkiaCanvas *canvas)
{
    if (canvas)
        budo_gl_state_guard_run(flush_skia_canvas_transition, canvas);
}

static void flush_skia_canvas_discarding_gl_state(SkiaCanvas *canvas)
{
    if (canvas)
        skia_canvas_flush(canvas);
}

static GLRenderTarget *get_render_target_slot(Window *window, int target_id)
{
    return window ? (GLRenderTarget *)gpu_resource_slot_lookup(
                        window->render_targets, sizeof(window->render_targets[0]),
                        MAX_GL_RENDER_TARGETS, target_id)
                  : NULL;
}

static GLint get_cached_uniform_location(GLProgramSlot *slot, const char *name)
{
    int i;
    int empty_index = -1;

    if (!slot || !name || !name[0])
        return -1;

    for (i = 0; i < MAX_GL_UNIFORM_CACHE; i++)
    {
        if (slot->uniform_cache[i].in_use)
        {
            if (strcmp(slot->uniform_cache[i].name, name) == 0)
                return slot->uniform_cache[i].location;
        }
        else if (empty_index < 0)
        {
            empty_index = i;
        }
    }

    if (empty_index >= 0)
    {
        GLint location = glGetUniformLocation(slot->program, name);
        slot->uniform_cache[empty_index].in_use = true;
        strncpy(slot->uniform_cache[empty_index].name, name,
                sizeof(slot->uniform_cache[empty_index].name) - 1);
        slot->uniform_cache[empty_index].name[sizeof(slot->uniform_cache[empty_index].name) - 1] = '\0';
        slot->uniform_cache[empty_index].location = location;
        return location;
    }

    return glGetUniformLocation(slot->program, name);
}

static bool upload_buffer_sub_data_immediate(Window *window, GLBufferSlot *buffer,
                                             size_t offset, const void *data, size_t size)
{
    if (!buffer || buffer->target == 0)
    {
        set_error(window, "Buffer has no data uploaded yet");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glBindBuffer(buffer->target, buffer->id);
    glBufferSubData(buffer->target, (GLintptr)offset, (GLsizeiptr)size, data);
    glBindBuffer(buffer->target, 0);
    return true;
}

const char *window_get_project_dir(Window *window)
{
    return window ? window->project_dir : NULL;
}

bool window_gl_draw_region(Window *window, int program_id,
                           float x, float y, float w, float h,
                           int target_id)
{
    GLProgramSlot *slot;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    (void)slot;
    return window_gl_draw_region_immediate(window, program_id, x, y, w, h, target_id);
}

/* ============================================
 * 3D rendering pipeline (buffers, textures, vertex layouts, mesh draws)
 * WebGL 2 / GLES 3 backend
 * ============================================ */

static GLenum gl_buffer_target_to_gl(WindowGLBufferTarget t)
{
    return (t == WINDOW_GL_BUFFER_INDEX) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
}

static GLenum gl_buffer_usage_to_gl(WindowGLBufferUsage u)
{
    switch (u)
    {
    case WINDOW_GL_USAGE_DYNAMIC:
        return GL_DYNAMIC_DRAW;
    case WINDOW_GL_USAGE_STREAM:
        return GL_STREAM_DRAW;
    default:
        return GL_STATIC_DRAW;
    }
}

static GLenum gl_attr_type_to_gl(WindowGLAttrType t)
{
    switch (t)
    {
    case WINDOW_GL_ATTR_BYTE:
        return GL_BYTE;
    case WINDOW_GL_ATTR_UBYTE:
        return GL_UNSIGNED_BYTE;
    case WINDOW_GL_ATTR_SHORT:
        return GL_SHORT;
    case WINDOW_GL_ATTR_USHORT:
        return GL_UNSIGNED_SHORT;
    case WINDOW_GL_ATTR_INT:
        return GL_INT;
    case WINDOW_GL_ATTR_UINT:
        return GL_UNSIGNED_INT;
    default:
        return GL_FLOAT;
    }
}

static GLenum gl_prim_to_gl(WindowGLPrimitive p)
{
    switch (p)
    {
    case WINDOW_GL_PRIM_TRIANGLE_STRIP:
        return GL_TRIANGLE_STRIP;
    case WINDOW_GL_PRIM_TRIANGLE_FAN:
        return GL_TRIANGLE_FAN;
    case WINDOW_GL_PRIM_LINES:
        return GL_LINES;
    case WINDOW_GL_PRIM_LINE_STRIP:
        return GL_LINE_STRIP;
    case WINDOW_GL_PRIM_POINTS:
        return GL_POINTS;
    default:
        return GL_TRIANGLES;
    }
}

static GLBufferSlot *get_buffer_slot(Window *window, int buffer_id)
{
    return window ? (GLBufferSlot *)gpu_resource_slot_lookup(
                        window->buffers, sizeof(window->buffers[0]),
                        MAX_GL_BUFFERS, buffer_id)
                  : NULL;
}

static GLTextureSlot *get_texture_slot(Window *window, int texture_id)
{
    return window ? (GLTextureSlot *)gpu_resource_slot_lookup(
                        window->textures, sizeof(window->textures[0]),
                        MAX_GL_TEXTURES, texture_id)
                  : NULL;
}

static GLVertexLayout *get_layout_slot(Window *window, int layout_id)
{
    if (!window || layout_id <= 0 || layout_id > MAX_GL_VERTEX_LAYOUTS)
        return NULL;
    if (!window->vertex_layouts[layout_id - 1].in_use)
        return NULL;
    return &window->vertex_layouts[layout_id - 1];
}

/* ---- Buffers ---- */

int window_gl_create_buffer(Window *window)
{
    int i;
    if (!BUDO_GL_WINDOW_READY(window))
        return -1;
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    for (i = 0; i < MAX_GL_BUFFERS; i++)
    {
        if (!window->buffers[i].in_use)
        {
            GLuint id = 0;
            glGenBuffers(1, &id);
            if (id == 0)
            {
                set_error(window, "glGenBuffers failed");
                return -1;
            }
            memset(&window->buffers[i], 0, sizeof(window->buffers[i]));
            window->buffers[i].in_use = true;
            window->buffers[i].id = id;
            window->buffers[i].target = GL_ARRAY_BUFFER;
            return i + 1;
        }
    }
    set_error(window, "Maximum number of buffers reached");
    return -1;
}

bool window_gl_destroy_buffer(Window *window, int buffer_id)
{
    GLBufferSlot *b = get_buffer_slot(window, buffer_id);
    if (!b)
    {
        if (window)
            set_error(window, "Invalid buffer id");
        return false;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glDeleteBuffers(1, &b->id);
    memset(b, 0, sizeof(*b));
    return true;
}

bool window_gl_buffer_data(Window *window, int buffer_id,
                           WindowGLBufferTarget target,
                           const void *data, size_t size,
                           WindowGLBufferUsage usage)
{
    GLBufferSlot *b = get_buffer_slot(window, buffer_id);
    GLenum gl_target;
    if (!b)
    {
        set_error(window, "Invalid buffer id");
        return false;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    gl_target = gl_buffer_target_to_gl(target);
    b->target = gl_target;
    b->size = size;
    glBindBuffer(gl_target, b->id);
    glBufferData(gl_target, (GLsizeiptr)size, data, gl_buffer_usage_to_gl(usage));
    glBindBuffer(gl_target, 0);
    return true;
}

bool window_gl_buffer_sub_data(Window *window, int buffer_id,
                               size_t offset, const void *data, size_t size)
{
    GLBufferSlot *b = get_buffer_slot(window, buffer_id);
    if (!b)
    {
        set_error(window, "Invalid buffer id");
        return false;
    }
    if (b->target == 0)
    {
        set_error(window, "Buffer has no data uploaded yet");
        return false;
    }
    if (size == 0)
        return true;
    return upload_buffer_sub_data_immediate(window, b, offset, data, size);
}

/* ---- Textures ---- */

static int alloc_texture_slot(Window *window)
{
    int i;
    for (i = 0; i < MAX_GL_TEXTURES; i++)
        if (!window->textures[i].in_use)
            return i;
    return -1;
}

int window_gl_create_texture_2d_from_buffer(Window *window, const uint8_t *bytes, size_t byte_count)
{
    int w = 0, h = 0, id;
    uint8_t *pixels;
    if (!window || !bytes || byte_count == 0)
        return -1;
    pixels = image_load_rgba8_from_memory(bytes, byte_count, &w, &h);
    if (!pixels)
    {
        const char *why = image_loader_last_error();
        char msg[512];
        snprintf(msg, sizeof(msg), "loadTexture2DFromBuffer: failed to decode image: %s",
                 why ? why : "unknown error");
        set_error(window, msg);
        return -1;
    }
    id = window_gl_create_texture_2d(window, w, h, WINDOW_GL_TEX_RGBA8, pixels);
    image_free(pixels);
    return id;
}

int window_gl_create_texture_cube_from_buffers(Window *window,
                                               const uint8_t *const bytes[6],
                                               const size_t byte_counts[6])
{
    uint8_t *pixels[6] = {0};
    int w[6] = {0}, h[6] = {0};
    const void *faces[6];
    int i, id = -1, size;
    if (!window || !bytes || !byte_counts)
        return -1;
    for (i = 0; i < 6; ++i)
    {
        if (!bytes[i] || byte_counts[i] == 0)
            goto cleanup;
        pixels[i] = image_load_rgba8_from_memory(bytes[i], byte_counts[i], &w[i], &h[i]);
        if (!pixels[i])
        {
            char msg[512];
            const char *why = image_loader_last_error();
            snprintf(msg, sizeof(msg), "loadTextureCubeFromBuffer: failed to decode face %d: %s",
                     i, why ? why : "unknown error");
            set_error(window, msg);
            goto cleanup;
        }
        faces[i] = pixels[i];
    }
    size = w[0];
    for (i = 0; i < 6; ++i)
    {
        if (w[i] != size || h[i] != size)
        {
            set_error(window, "loadTextureCubeFromBuffer: all 6 faces must be square and the same size");
            goto cleanup;
        }
    }
    id = window_gl_create_texture_cube(window, size, WINDOW_GL_TEX_RGBA8, faces);
cleanup:
    for (i = 0; i < 6; ++i)
        if (pixels[i])
            image_free(pixels[i]);
    return id;
}

bool window_gl_update_texture_2d(Window *window, int texture_id,
                                 int x, int y, int w, int h, const void *pixels)
{
    GLTextureSlot *t = get_texture_slot(window, texture_id);
    if (!t)
    {
        set_error(window, "Invalid texture id");
        return false;
    }
    if (t->target != GL_TEXTURE_2D)
    {
        set_error(window, "Texture is not a 2D texture");
        return false;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, t->format, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool window_gl_destroy_texture(Window *window, int texture_id)
{
    GLTextureSlot *t = get_texture_slot(window, texture_id);
    if (!t)
    {
        if (window)
            set_error(window, "Invalid texture id");
        return false;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glDeleteTextures(1, &t->id);
    memset(t, 0, sizeof(*t));
    return true;
}

/* ---- Vertex layouts ---- */

int window_gl_create_vertex_layout(Window *window)
{
    int i;
    if (!BUDO_GL_WINDOW_READY(window))
        return -1;
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    for (i = 0; i < MAX_GL_VERTEX_LAYOUTS; i++)
    {
        if (!window->vertex_layouts[i].in_use)
        {
            GLuint vao = 0;
            glGenVertexArrays(1, &vao);
            if (vao == 0)
            {
                set_error(window, "glGenVertexArrays failed");
                return -1;
            }
            memset(&window->vertex_layouts[i], 0, sizeof(window->vertex_layouts[i]));
            window->vertex_layouts[i].in_use = true;
            window->vertex_layouts[i].vao = vao;
            window->vertex_layouts[i].needs_rebind = true;
            return i + 1;
        }
    }
    set_error(window, "Maximum number of vertex layouts reached");
    return -1;
}

bool window_gl_set_attribute(Window *window, int layout_id,
                             int location, int buffer_id,
                             int size, WindowGLAttrType type,
                             bool normalized, int stride, int offset, int divisor)
{
    GLVertexLayout *layout = get_layout_slot(window, layout_id);
    GLBufferSlot *b;
    int i;
    if (!layout)
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    b = get_buffer_slot(window, buffer_id);
    if (!b)
    {
        set_error(window, "Invalid buffer id");
        return false;
    }
    if (location < 0 || location > 31)
    {
        set_error(window, "Attribute location out of range");
        return false;
    }
    if (size < 1 || size > 4)
    {
        set_error(window, "Attribute component count must be 1..4");
        return false;
    }
    for (i = 0; i < layout->attr_count; i++)
        if (layout->attrs[i].active && layout->attrs[i].location == location)
            break;
    if (i == layout->attr_count)
    {
        if (layout->attr_count >= MAX_GL_LAYOUT_ATTRS)
        {
            set_error(window, "Maximum attributes per layout reached");
            return false;
        }
        layout->attr_count++;
    }
    layout->attrs[i].active = true;
    layout->attrs[i].location = location;
    layout->attrs[i].buffer_id = buffer_id;
    layout->attrs[i].size = size;
    layout->attrs[i].gl_type = gl_attr_type_to_gl(type);
    layout->attrs[i].normalized = normalized;
    layout->attrs[i].stride = stride;
    layout->attrs[i].offset = offset;
    layout->attrs[i].divisor = divisor;
    layout->needs_rebind = true;
    return true;
}

bool window_gl_set_index_buffer(Window *window, int layout_id,
                                int buffer_id, WindowGLIndexType type)
{
    GLVertexLayout *layout = get_layout_slot(window, layout_id);
    if (!layout)
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (buffer_id <= 0)
    {
        layout->index_buffer_id = 0;
        layout->index_gl_type = 0;
        layout->needs_rebind = true;
        return true;
    }
    if (!get_buffer_slot(window, buffer_id))
    {
        set_error(window, "Invalid index buffer id");
        return false;
    }
    layout->index_buffer_id = buffer_id;
    layout->index_gl_type = (type == WINDOW_GL_INDEX_U32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    layout->needs_rebind = true;
    return true;
}

bool window_gl_destroy_vertex_layout(Window *window, int layout_id)
{
    GLVertexLayout *layout = get_layout_slot(window, layout_id);
    if (!layout)
    {
        if (window)
            set_error(window, "Invalid vertex layout id");
        return false;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    if (layout->vao != 0)
        glDeleteVertexArrays(1, &layout->vao);
    memset(layout, 0, sizeof(*layout));
    return true;
}

/* ---- Mesh draw queueing ---- */

bool window_gl_draw_mesh(Window *window, int program_id, int layout_id,
                         WindowGLPrimitive mode, int first, int count,
                         int target_id,
                         const WindowGLDrawState *state,
                         int instance_count)
{
    if (!BUDO_GL_WINDOW_READY(window))
        return false;
    if (!get_program_slot(window, program_id))
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!get_layout_slot(window, layout_id))
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (count <= 0)
    {
        set_error(window, "Mesh draw count must be > 0");
        return false;
    }
    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }
    return window_gl_draw_mesh_immediate(window, program_id, layout_id,
                                         mode, first, count, target_id,
                                         state, instance_count);
}

/* ---- Mesh pass executor ---- */

static void apply_blend_mode(int mode)
{
    if (mode == WINDOW_GL_BLEND_NONE)
    {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    switch (mode)
    {
    case WINDOW_GL_BLEND_ADD:
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case WINDOW_GL_BLEND_PREMULT:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

/* ---- File loading, program creation, and textures ----
 *
 * These use platform-specific helpers each backend defines (later in its
 * file, with these signatures): shader compilation with dialect handling,
 * and texture format mapping (desktop GL vs GLES). */

static bool resolve_project_path(Window *window, const char *relative_path,
                                 char *resolved_path, size_t resolved_size)
{
    int written;

    if (!window || !relative_path || !resolved_path)
        return false;

    if (!path_is_safe_relative(relative_path))
    {
        set_error(window, "Shader paths must be relative to the application directory and cannot contain '.' or '..' segments");
        return false;
    }

    written = snprintf(resolved_path, resolved_size, "%s/%s", window->project_dir, relative_path);
    if (written < 0 || (size_t)written >= resolved_size)
    {
        set_error(window, "Resolved shader path is too long");
        return false;
    }

    return true;
}

static GLuint create_program_from_source(Window *window, const char *vertex_source,
                                         const char *fragment_source, bool require_app_dialect);
/* Binds a texture to a sampler uniform of the program on the given unit
 * (0..7); the program stays current and the active unit returns to 0. */
static bool immediate_bind_sampler_texture(Window *window, int program_id,
                                           const char *uniform_name,
                                           GLuint texture_id, GLenum texture_target,
                                           int texture_unit)
{
    GLProgramSlot *slot;
    GLint uniform_loc;

    if (!BUDO_GL_WINDOW_READY(window) || texture_id == 0)
        return false;
    if (!uniform_name || !uniform_name[0])
    {
        set_error(window, "Texture uniform name cannot be empty");
        return false;
    }
    if (texture_unit < 0 || texture_unit > 7)
    {
        set_error(window, "Texture unit must be between 0 and 7");
        return false;
    }

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glUseProgram(slot->program);
    glActiveTexture(GL_TEXTURE0 + (GLenum)texture_unit);
    glBindTexture(texture_target, texture_id);
    if (texture_target == GL_TEXTURE_2D)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    uniform_loc = glGetUniformLocation(slot->program, uniform_name);
    if (uniform_loc >= 0)
        glUniform1i(uniform_loc, texture_unit);
    glActiveTexture(GL_TEXTURE0);
    return true;
}
static GLenum gl_tex_internal_format(WindowGLTexFormat f);
static GLenum gl_tex_upload_format(WindowGLTexFormat f);

/* Call immediately after a failed fopen so errno still describes it. */
static void set_file_open_error(Window *window, const char *what, const char *path)
{
    if (!window)
        return;
    snprintf(window->error_msg, sizeof(window->error_msg), "Failed to open %s: %s (%s)",
             what, path ? path : "", strerror(errno));
}

static char *read_text_file(Window *window, const char *path)
{
    FILE *file;
    long size;
    char *buffer;

    file = fopen(path, "rb");
    if (!file)
    {
        set_file_open_error(window, "shader file", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        set_error(window, "Failed to read shader file size");
        return NULL;
    }

    size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        set_error(window, "Failed to read shader file size");
        return NULL;
    }

    rewind(file);

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer)
    {
        fclose(file);
        set_error(window, "Failed to allocate shader source buffer");
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        free(buffer);
        set_error(window, "Failed to read shader file contents");
        return NULL;
    }

    fclose(file);
    buffer[size] = '\0';
    return buffer;
}

static uint8_t *read_binary_file(Window *window, const char *path, size_t *out_size)
{
    FILE *file;
    long size;
    uint8_t *buffer;

    if (out_size)
        *out_size = 0;

    file = fopen(path, "rb");
    if (!file)
    {
        set_file_open_error(window, "resource file", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        set_error(window, "Failed to read resource file size");
        return NULL;
    }

    size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        set_error(window, "Failed to read resource file size");
        return NULL;
    }

    rewind(file);

    buffer = (uint8_t *)malloc((size_t)size);
    if (!buffer)
    {
        fclose(file);
        set_error(window, "Failed to allocate resource buffer");
        return NULL;
    }

    if (size > 0 && fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        free(buffer);
        fclose(file);
        set_error(window, "Failed to read resource file");
        return NULL;
    }

    fclose(file);
    if (out_size)
        *out_size = (size_t)size;
    return buffer;
}

int window_gl_create_program_from_source(Window *window,
                                         const char *vertex_source, size_t vertex_len,
                                         const char *fragment_source, size_t fragment_len)
{
    char *vertex_copy = NULL;
    char *fragment_copy = NULL;
    GLuint program = 0;
    int i;

    if (!window || !vertex_source || !fragment_source)
        return -1;

    if (!BUDO_GL_WINDOW_READY(window))
    {
        set_error(window, "GL context not initialized");
        return -1;
    }

    vertex_copy = (char *)malloc(vertex_len + 1);
    fragment_copy = (char *)malloc(fragment_len + 1);
    if (!vertex_copy || !fragment_copy)
    {
        free(vertex_copy);
        free(fragment_copy);
        set_error(window, "Failed to allocate shader source buffer");
        return -1;
    }

    memcpy(vertex_copy, vertex_source, vertex_len);
    vertex_copy[vertex_len] = '\0';
    memcpy(fragment_copy, fragment_source, fragment_len);
    fragment_copy[fragment_len] = '\0';

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    program = create_program_from_source(window, vertex_copy, fragment_copy, true);
    free(vertex_copy);
    free(fragment_copy);

    if (program == 0)
        return -1;

    for (i = 0; i < MAX_GL_PROGRAMS; i++)
    {
        if (!window->programs[i].in_use)
        {
            window->programs[i].in_use = true;
            window->programs[i].program = program;
            window->programs[i].sampler_loc = glGetUniformLocation(program, "u_canvas");
            window->programs[i].resolution_loc = glGetUniformLocation(program, "u_resolution");
            window->programs[i].time_loc = glGetUniformLocation(program, "u_time");
            window->programs[i].position_loc = glGetAttribLocation(program, "a_position");
            window->programs[i].tex_coord_loc = glGetAttribLocation(program, "a_texCoord");
            memset(window->programs[i].uniform_cache, 0,
                   sizeof(window->programs[i].uniform_cache));
            return i + 1;
        }
    }

    glDeleteProgram(program);
    set_error(window, "Maximum number of shader programs reached");
    return -1;
}

bool window_gl_bind_canvas_texture_immediate(Window *window, int program_id,
                                             const char *uniform_name,
                                             CanvasTexture *canvas_texture,
                                             int texture_unit)
{
    uint32_t texture_id;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;
    if (!canvas_texture)
    {
        set_error(window, "Invalid canvas texture");
        return false;
    }

    window_canvas_texture_flush(canvas_texture);
    texture_id = window_canvas_texture_get_gl_texture(canvas_texture);
    if (texture_id == 0)
    {
        set_error(window, "Canvas texture has no GL texture");
        return false;
    }

    return immediate_bind_sampler_texture(window, program_id, uniform_name,
                                          (GLuint)texture_id, GL_TEXTURE_2D,
                                          texture_unit);
}

bool window_gl_bind_canvas_texture(Window *window, int program_id,
                                   const char *uniform_name,
                                   CanvasTexture *canvas_texture,
                                   int texture_unit)
{
    return window_gl_bind_canvas_texture_immediate(window, program_id, uniform_name,
                                                   canvas_texture, texture_unit);
}

int window_gl_create_texture_2d(Window *window, int width, int height,
                                WindowGLTexFormat format, const void *pixels)
{
    int slot;
    GLuint id = 0;
    GLenum internal_fmt, upload_fmt;
    if (!BUDO_GL_WINDOW_READY(window) || width <= 0 || height <= 0)
        return -1;
    slot = alloc_texture_slot(window);
    if (slot < 0)
    {
        set_error(window, "Maximum number of textures reached");
        return -1;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glGenTextures(1, &id);
    if (id == 0)
    {
        set_error(window, "glGenTextures failed");
        return -1;
    }
    internal_fmt = gl_tex_internal_format(format);
    upload_fmt = gl_tex_upload_format(format);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0,
                 upload_fmt, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    memset(&window->textures[slot], 0, sizeof(window->textures[slot]));
    window->textures[slot].in_use = true;
    window->textures[slot].id = id;
    window->textures[slot].target = GL_TEXTURE_2D;
    window->textures[slot].width = width;
    window->textures[slot].height = height;
    window->textures[slot].format = upload_fmt;
    return slot + 1;
}

int window_gl_create_texture_2d_from_file(Window *window, const char *path)
{
    char resolved[PATH_MAX];
    size_t byte_count = 0;
    uint8_t *bytes;
    int id;
    if (!window || !path)
        return -1;
    if (!resolve_project_path(window, path, resolved, sizeof(resolved)))
        return -1;
    bytes = read_binary_file(window, resolved, &byte_count);
    if (!bytes)
        return -1;
    id = window_gl_create_texture_2d_from_buffer(window, bytes, byte_count);
    free(bytes);
    return id;
}

int window_gl_create_texture_cube(Window *window, int size,
                                  WindowGLTexFormat format,
                                  const void *const faces[6])
{
    int slot, i;
    GLuint id = 0;
    GLenum internal_fmt, upload_fmt;
    if (!BUDO_GL_WINDOW_READY(window) || size <= 0 || !faces)
        return -1;
    slot = alloc_texture_slot(window);
    if (slot < 0)
    {
        set_error(window, "Maximum number of textures reached");
        return -1;
    }
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glGenTextures(1, &id);
    if (id == 0)
    {
        set_error(window, "glGenTextures failed");
        return -1;
    }
    internal_fmt = gl_tex_internal_format(format);
    upload_fmt = gl_tex_upload_format(format);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, internal_fmt,
                     size, size, 0, upload_fmt, GL_UNSIGNED_BYTE, faces[i]);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    memset(&window->textures[slot], 0, sizeof(window->textures[slot]));
    window->textures[slot].in_use = true;
    window->textures[slot].id = id;
    window->textures[slot].target = GL_TEXTURE_CUBE_MAP;
    window->textures[slot].width = size;
    window->textures[slot].height = size;
    window->textures[slot].format = upload_fmt;
    return slot + 1;
}

int window_gl_create_texture_cube_from_files(Window *window, const char *const paths[6])
{
    char resolved[6][PATH_MAX];
    uint8_t *bytes[6] = {0};
    size_t byte_counts[6] = {0};
    int i, id = -1;
    if (!window || !paths)
        return -1;
    for (i = 0; i < 6; ++i)
    {
        if (!paths[i] || !resolve_project_path(window, paths[i], resolved[i], sizeof(resolved[i])))
            goto cleanup;
        bytes[i] = read_binary_file(window, resolved[i], &byte_counts[i]);
        if (!bytes[i])
            goto cleanup;
    }
    id = window_gl_create_texture_cube_from_buffers(window, (const uint8_t *const *)bytes, byte_counts);
cleanup:
    for (i = 0; i < 6; ++i)
        free(bytes[i]);
    return id;
}

/* ── Uniforms ────────────────────────────────────────────────────────── */

static void apply_uniform_value(GLint location, GLUniformKind kind, int count, const void *data)
{
    const GLint *iv = (const GLint *)data;
    const GLfloat *fv = (const GLfloat *)data;

    switch (kind)
    {
    case GL_UNIFORM_1I:
        glUniform1i(location, iv[0]);
        break;
    case GL_UNIFORM_1F:
        glUniform1f(location, fv[0]);
        break;
    case GL_UNIFORM_2F:
        glUniform2f(location, fv[0], fv[1]);
        break;
    case GL_UNIFORM_3F:
        glUniform3f(location, fv[0], fv[1], fv[2]);
        break;
    case GL_UNIFORM_4F:
        glUniform4f(location, fv[0], fv[1], fv[2], fv[3]);
        break;
    case GL_UNIFORM_1IV:
        glUniform1iv(location, count, iv);
        break;
    case GL_UNIFORM_1FV:
        glUniform1fv(location, count, fv);
        break;
    case GL_UNIFORM_2FV:
        glUniform2fv(location, count, fv);
        break;
    case GL_UNIFORM_3FV:
        glUniform3fv(location, count, fv);
        break;
    case GL_UNIFORM_4FV:
        glUniform4fv(location, count, fv);
        break;
    case GL_UNIFORM_MAT3FV:
        glUniformMatrix3fv(location, count, GL_FALSE, fv);
        break;
    case GL_UNIFORM_MAT4FV:
        glUniformMatrix4fv(location, count, GL_FALSE, fv);
        break;
    }
}

/* Sets a uniform on the given program now, restoring the current program. */
static bool set_program_uniform(Window *window, int program_id, const char *name,
                                GLUniformKind kind, int count, const void *data)
{
    GLProgramSlot *slot;
    GLint location;
    GLint previous_program = 0;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;
    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!name || !name[0])
    {
        set_error(window, "Uniform name cannot be empty");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    location = get_cached_uniform_location(slot, name);
    if (location < 0)
    {
        snprintf(window->error_msg, sizeof(window->error_msg), "Uniform not found: %s", name);
        return false;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    if ((GLuint)previous_program != slot->program)
        glUseProgram(slot->program);
    apply_uniform_value(location, kind, count, data);
    if ((GLuint)previous_program != slot->program)
        glUseProgram((GLuint)previous_program);

    return true;
}

bool window_gl_set_uniform_1i(Window *window, int program_id, const char *name, int value)
{
    GLint v = (GLint)value;
    return set_program_uniform(window, program_id, name, GL_UNIFORM_1I, 1, &v);
}

bool window_gl_set_uniform_1f(Window *window, int program_id, const char *name, float value)
{
    GLfloat v = value;
    return set_program_uniform(window, program_id, name, GL_UNIFORM_1F, 1, &v);
}

bool window_gl_set_uniform_2f(Window *window, int program_id, const char *name, float v0, float v1)
{
    GLfloat v[2] = {v0, v1};
    return set_program_uniform(window, program_id, name, GL_UNIFORM_2F, 1, v);
}

bool window_gl_set_uniform_3f(Window *window, int program_id, const char *name,
                              float v0, float v1, float v2)
{
    GLfloat v[3] = {v0, v1, v2};
    return set_program_uniform(window, program_id, name, GL_UNIFORM_3F, 1, v);
}

bool window_gl_set_uniform_4f(Window *window, int program_id, const char *name,
                              float v0, float v1, float v2, float v3)
{
    GLfloat v[4] = {v0, v1, v2, v3};
    return set_program_uniform(window, program_id, name, GL_UNIFORM_4F, 1, v);
}

#define UNIFORM_ARRAY_SETTER(suffix, ctype, kind_enum)                          \
    bool window_gl_set_uniform_##suffix(Window *window, int program_id,        \
                                        const char *name,                      \
                                        const ctype *values, int count)        \
    {                                                                          \
        return set_program_uniform(window, program_id, name, kind_enum,        \
                                   count < 1 ? 1 : count, values);             \
    }

UNIFORM_ARRAY_SETTER(matrix3fv, float, GL_UNIFORM_MAT3FV)
UNIFORM_ARRAY_SETTER(matrix4fv, float, GL_UNIFORM_MAT4FV)
UNIFORM_ARRAY_SETTER(1iv, int, GL_UNIFORM_1IV)
UNIFORM_ARRAY_SETTER(1fv, float, GL_UNIFORM_1FV)
UNIFORM_ARRAY_SETTER(2fv, float, GL_UNIFORM_2FV)
UNIFORM_ARRAY_SETTER(3fv, float, GL_UNIFORM_3FV)
UNIFORM_ARRAY_SETTER(4fv, float, GL_UNIFORM_4FV)

#undef UNIFORM_ARRAY_SETTER

/* ── Programs ────────────────────────────────────────────────────────── */

int window_gl_create_program(Window *window, const char *vertex_path, const char *fragment_path)
{
    char vertex_full_path[PATH_MAX];
    char fragment_full_path[PATH_MAX];
    char *vertex_source = NULL;
    char *fragment_source = NULL;
    int program_id;

    if (!window)
        return -1;
    if (!BUDO_GL_WINDOW_READY(window))
    {
        set_error(window, "GL context not initialized");
        return -1;
    }

    if (!resolve_project_path(window, vertex_path, vertex_full_path, sizeof(vertex_full_path)) ||
        !resolve_project_path(window, fragment_path, fragment_full_path, sizeof(fragment_full_path)))
    {
        return -1;
    }

    vertex_source = read_text_file(window, vertex_full_path);
    if (!vertex_source)
        return -1;

    fragment_source = read_text_file(window, fragment_full_path);
    if (!fragment_source)
    {
        free(vertex_source);
        return -1;
    }

    program_id = window_gl_create_program_from_source(window, vertex_source, strlen(vertex_source),
                                                      fragment_source, strlen(fragment_source));
    free(vertex_source);
    free(fragment_source);
    return program_id;
}

bool window_gl_destroy_program(Window *window, int program_id)
{
    GLProgramSlot *slot = get_program_slot(window, program_id);

    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glDeleteProgram(slot->program);
    slot->program = 0;
    slot->in_use = false;
    return true;
}

bool window_gl_use_program(Window *window, int program_id)
{
    GLProgramSlot *slot;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;

    slot = get_program_slot(window, program_id);
    if (!slot)
    {
        set_error(window, "Invalid shader program id");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    glUseProgram(slot->program);
    return true;
}

int window_gl_get_attrib_location(Window *window, int program_id, const char *name)
{
    GLProgramSlot *slot;

    if (!BUDO_GL_WINDOW_READY(window))
        return -1;
    slot = get_program_slot(window, program_id);
    if (!slot || !name || !name[0])
        return -1;
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    return (int)glGetAttribLocation(slot->program, name);
}

/* ── Sampler binding: render targets and 2D / cube textures ──────────── */

bool window_gl_bind_texture_immediate(Window *window, int program_id,
                                      const char *uniform_name,
                                      int render_target_id, int texture_unit)
{
    GLRenderTarget *target;

    if (!window)
        return false;
    target = get_render_target_slot(window, render_target_id);
    if (!target)
    {
        set_error(window, "Invalid render target id");
        return false;
    }
    return immediate_bind_sampler_texture(window, program_id, uniform_name,
                                          target->texture, GL_TEXTURE_2D, texture_unit);
}

bool window_gl_bind_texture(Window *window, int program_id,
                            const char *uniform_name,
                            int render_target_id, int texture_unit)
{
    return window_gl_bind_texture_immediate(window, program_id, uniform_name,
                                            render_target_id, texture_unit);
}

static bool bind_texture_common(Window *window, int program_id,
                                const char *uniform_name, int texture_id,
                                int texture_unit, GLenum required_target)
{
    GLTextureSlot *t;

    if (!window)
        return false;
    t = get_texture_slot(window, texture_id);
    if (!t)
    {
        set_error(window, "Invalid texture id");
        return false;
    }
    if (t->target != required_target)
    {
        set_error(window, "Texture target does not match binding kind");
        return false;
    }
    return immediate_bind_sampler_texture(window, program_id, uniform_name,
                                          t->id, required_target, texture_unit);
}

bool window_gl_bind_texture_2d(Window *window, int program_id,
                               const char *uniform_name,
                               int texture_id, int texture_unit)
{
    return bind_texture_common(window, program_id, uniform_name, texture_id,
                               texture_unit, GL_TEXTURE_2D);
}

bool window_gl_bind_texture_cube(Window *window, int program_id,
                                 const char *uniform_name,
                                 int texture_id, int texture_unit)
{
    return bind_texture_common(window, program_id, uniform_name, texture_id,
                               texture_unit, GL_TEXTURE_CUBE_MAP);
}

bool window_gl_bind_texture_2d_immediate(Window *window, int program_id,
                                         const char *uniform_name,
                                         int texture_id, int texture_unit)
{
    return window_gl_bind_texture_2d(window, program_id, uniform_name,
                                     texture_id, texture_unit);
}

bool window_gl_bind_texture_cube_immediate(Window *window, int program_id,
                                           const char *uniform_name,
                                           int texture_id, int texture_unit)
{
    return window_gl_bind_texture_cube(window, program_id, uniform_name,
                                       texture_id, texture_unit);
}

/* ── Render targets ──────────────────────────────────────────────────── */

/* Built at call time: on Windows the GL entry points are runtime-loaded
 * pointers (core/gl_desktop.h), not constants a static table could hold. */
static GlRenderTargetApi render_target_api(void)
{
    const GlRenderTargetApi api = {
        (void (*)(int, uint32_t *))glGenTextures,
        (void (*)(uint32_t, uint32_t))glBindTexture,
        (void (*)(uint32_t, uint32_t, int))glTexParameteri,
        (void (*)(uint32_t, int, int, int, int, int, uint32_t, uint32_t,
                  const void *))glTexImage2D,
        (void (*)(int, const uint32_t *))glDeleteTextures,
        (void (*)(int, uint32_t *))glGenFramebuffers,
        (void (*)(uint32_t, uint32_t))glBindFramebuffer,
        (void (*)(uint32_t, uint32_t, uint32_t, uint32_t, int))glFramebufferTexture2D,
        (uint32_t (*)(uint32_t))glCheckFramebufferStatus,
        (void (*)(int, const uint32_t *))glDeleteFramebuffers,
        (void (*)(int, uint32_t *))glGenRenderbuffers,
        (void (*)(uint32_t, uint32_t))glBindRenderbuffer,
        (void (*)(uint32_t, uint32_t, int, int))glRenderbufferStorage,
        (void (*)(uint32_t, uint32_t, uint32_t, uint32_t))glFramebufferRenderbuffer,
        (void (*)(int, const uint32_t *))glDeleteRenderbuffers};
    return api;
}

static const GlRenderTargetConstants render_target_constants = {
    GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER,
    GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_LINEAR, GL_CLAMP_TO_EDGE,
    GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, GL_FRAMEBUFFER,
    GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
    GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_COMPLETE};

int window_gl_create_render_target(Window *window, int width, int height, bool depth)
{
    int i;

    if (!BUDO_GL_WINDOW_READY(window) || width <= 0 || height <= 0)
        return -1;

    for (i = 0; i < MAX_GL_RENDER_TARGETS; i++)
    {
        if (!window->render_targets[i].in_use)
            break;
    }
    if (i == MAX_GL_RENDER_TARGETS)
    {
        set_error(window, "Maximum number of render targets reached");
        return -1;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    const GlRenderTargetApi api = render_target_api();
    if (!gl_render_target_create(&api, &render_target_constants,
                                 &window->render_targets[i], width, height, depth))
    {
        set_error(window, "Render target framebuffer incomplete");
        return -1;
    }
    return i + 1;
}

bool window_gl_resize_render_target(Window *window, int target_id, int width, int height)
{
    GLRenderTarget *rt;

    if (!BUDO_GL_WINDOW_READY(window) || width <= 0 || height <= 0)
        return false;

    rt = get_render_target_slot(window, target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    const GlRenderTargetApi api = render_target_api();
    if (!gl_render_target_resize(&api, &render_target_constants,
                                 rt, width, height))
    {
        set_error(window, "Render target framebuffer incomplete after resize");
        return false;
    }
    return true;
}

bool window_gl_destroy_render_target(Window *window, int target_id)
{
    GLRenderTarget *rt;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;

    rt = get_render_target_slot(window, target_id);
    if (!rt)
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    const GlRenderTargetApi api = render_target_api();
    gl_render_target_destroy(&api, rt);
    return true;
}

/* ── Bound render target, built-in uniforms, and mesh draws ──────────────
 *
 * Draws without an explicit target (target id 0) go to the app render target
 * selected with bindRenderTarget(), and otherwise to the screen. A negative
 * target id selects the screen explicitly.
 *
 * The backend defines (later in its file):
 *   prepare_screen_for_mesh(window)  blit the default canvas to the screen and
 *                                    clear screen depth before the frame's
 *                                    first screen mesh draw */

#if defined(GL_ES_VERSION_3_0) || defined(GL_VERSION_3_3) || defined(GL_ARB_instanced_arrays)
#define BUDO_GL_HAS_INSTANCED_ARRAYS 1
#else
#define BUDO_GL_HAS_INSTANCED_ARRAYS 0
#endif
#if defined(GL_ES_VERSION_3_0) || defined(GL_VERSION_3_1) || defined(GL_ARB_draw_instanced)
#define BUDO_GL_HAS_DRAW_INSTANCED 1
#else
#define BUDO_GL_HAS_DRAW_INSTANCED 0
#endif

static void prepare_screen_for_mesh(Window *window);

static void immediate_gl_apply_builtin_uniforms(Window *window, GLuint program, GLProgramSlot *slot,
                                                GLfloat resolution_width, GLfloat resolution_height,
                                                int canvas_texture_unit, bool apply_canvas_sampler)
{
    GLint sampler_loc;
    GLint resolution_loc;
    GLint time_loc;

    if (apply_canvas_sampler)
    {
        sampler_loc = slot ? slot->sampler_loc : glGetUniformLocation(program, "u_canvas");
        if (sampler_loc >= 0)
            glUniform1i(sampler_loc, canvas_texture_unit);
    }

    resolution_loc = slot ? slot->resolution_loc : glGetUniformLocation(program, "u_resolution");
    if (resolution_loc >= 0)
        glUniform2f(resolution_loc, resolution_width, resolution_height);

    time_loc = slot ? slot->time_loc : glGetUniformLocation(program, "u_time");
    if (time_loc >= 0)
        glUniform1f(time_loc, (GLfloat)window->frame_time);
}

static void immediate_gl_cleanup_texture_units(int first_unit, int unit_count)
{
    int i;

    for (i = 0; i < unit_count; i++)
    {
        glActiveTexture(GL_TEXTURE0 + (GLenum)(first_unit + i));
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

static bool immediate_gl_bind_screen_target(Window *window, GLBoundRenderTarget *bound)
{
    if (!window)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (bound)
    {
        bound->is_screen = true;
        bound->width = window->width;
        bound->height = window->height;
        bound->has_depth = true;
    }
    return true;
}

static bool immediate_gl_bind_app_render_target(Window *window, int target_id, GLBoundRenderTarget *bound)
{
    GLRenderTarget *rt = get_render_target_slot(window, target_id);

    if (!rt)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);

    if (bound)
    {
        bound->is_screen = false;
        bound->width = rt->width;
        bound->height = rt->height;
        bound->has_depth = rt->has_depth;
    }
    return true;
}

static bool immediate_gl_bind_render_target(Window *window, int target_id, GLBoundRenderTarget *bound)
{
    if (target_id > 0)
        return immediate_gl_bind_app_render_target(window, target_id, bound);
    return immediate_gl_bind_screen_target(window, bound);
}

static bool immediate_gl_get_bound_render_target(Window *window, GLBoundRenderTarget *bound)
{
    GLint framebuffer = 0;
    int i;

    if (!window || !bound)
        return false;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

    if (framebuffer == 0)
    {
        bound->is_screen = true;
        bound->width = window->width;
        bound->height = window->height;
        bound->has_depth = true;
        return true;
    }

    for (i = 0; i < MAX_GL_RENDER_TARGETS; i++)
    {
        GLRenderTarget *rt = &window->render_targets[i];
        if (rt->in_use && rt->fbo == (GLuint)framebuffer)
        {
            bound->is_screen = false;
            bound->width = rt->width;
            bound->height = rt->height;
            bound->has_depth = rt->has_depth;
            return true;
        }
    }

    /* Not an app render target (e.g. a Skia canvas framebuffer left bound):
     * the draw goes to the screen. */
    return immediate_gl_bind_screen_target(window, bound);
}

static void immediate_gl_apply_render_target_viewport(const GLBoundRenderTarget *bound,
                                                      int x, int y, int width, int height)
{
    (void)bound;
    glViewport(x, y, width, height);
}

static void immediate_gl_apply_depth_state(const GLBoundRenderTarget *bound,
                                           bool depth_test, bool depth_write,
                                           bool clear_depth)
{
    if (depth_test)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        if (clear_depth && bound && bound->has_depth)
            glClear(GL_DEPTH_BUFFER_BIT);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(depth_write ? GL_TRUE : GL_FALSE);
}

static void immediate_gl_clear_render_target_depth(const GLBoundRenderTarget *bound)
{
    if (bound && bound->has_depth)
        glClear(GL_DEPTH_BUFFER_BIT);
}

static bool immediate_gl_draw_mesh_now(Window *window, int program_id, int layout_id,
                                       GLenum mesh_mode, int first, int count,
                                       int target_id, bool bind_explicit_target,
                                       bool depth_test, bool depth_write,
                                       GLenum cull, int blend,
                                       int instance_count)
{
    GLProgramSlot *slot;
    GLVertexLayout *layout;
    GLBoundRenderTarget target;
    int i;
    int viewport_w, viewport_h;

    if (!window)
        return false;
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    slot = get_program_slot(window, program_id);
    layout = get_layout_slot(window, layout_id);
    if (!slot || !layout)
        return false;

    if (bind_explicit_target)
    {
        if (!immediate_gl_bind_render_target(window, target_id, &target))
            return false;
    }
    else if (!immediate_gl_get_bound_render_target(window, &target))
    {
        return false;
    }

    viewport_w = target.width;
    viewport_h = target.height;

    /* The first screen mesh of a frame draws over the default canvas. */
    if (target.is_screen && !window->screen_rendered_this_frame)
        prepare_screen_for_mesh(window);

    immediate_gl_apply_render_target_viewport(&target, 0, 0, viewport_w, viewport_h);

    glUseProgram(slot->program);

    /* Built-in uniforms are best-effort; user shaders may not declare them. */
    immediate_gl_apply_builtin_uniforms(window, slot->program, slot,
                                        (GLfloat)viewport_w, (GLfloat)viewport_h,
                                        0, false);

    glBindVertexArray(layout->vao);

    /* GL state for the mesh pass. */
    immediate_gl_apply_depth_state(&target, depth_test, depth_write,
                                   depth_test && !target.is_screen);

    if (cull != GL_NONE)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(cull);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    apply_blend_mode(blend);

    if (layout->needs_rebind)
    {
        glBindVertexArray(layout->vao);
        for (i = 0; i < MAX_GL_LAYOUT_ATTRS; i++)
        {
            glDisableVertexAttribArray((GLuint)i);
#if BUDO_GL_HAS_INSTANCED_ARRAYS
            glVertexAttribDivisor((GLuint)i, 0);
#endif
        }
        for (i = 0; i < layout->attr_count; i++)
        {
            GLVertexAttribDesc *a = &layout->attrs[i];
            GLBufferSlot *b;
            if (!a->active)
                continue;
            b = get_buffer_slot(window, a->buffer_id);
            if (!b)
                continue;
            glBindBuffer(GL_ARRAY_BUFFER, b->id);
            glEnableVertexAttribArray((GLuint)a->location);
            glVertexAttribPointer((GLuint)a->location, a->size, a->gl_type,
                                  a->normalized ? GL_TRUE : GL_FALSE,
                                  (GLsizei)a->stride,
                                  (const void *)(intptr_t)a->offset);
#if BUDO_GL_HAS_INSTANCED_ARRAYS
            glVertexAttribDivisor((GLuint)a->location, (GLuint)(a->divisor > 0 ? a->divisor : 0));
#endif
        }
        if (layout->index_buffer_id > 0)
        {
            GLBufferSlot *ib = get_buffer_slot(window, layout->index_buffer_id);
            if (ib)
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        layout->needs_rebind = false;
    }

    /* Draw. */
    if (layout->index_buffer_id > 0)
    {
        GLBufferSlot *ib = get_buffer_slot(window, layout->index_buffer_id);
        if (ib)
        {
            size_t element_size = (layout->index_gl_type == GL_UNSIGNED_INT) ? 4 : 2;
            const void *offset_ptr = (const void *)(intptr_t)((size_t)first * element_size);
            if (instance_count > 1)
            {
#if BUDO_GL_HAS_DRAW_INSTANCED
                glDrawElementsInstanced(mesh_mode, count,
                                        layout->index_gl_type, offset_ptr,
                                        instance_count);
#else
                glDrawElements(mesh_mode, count,
                               layout->index_gl_type, offset_ptr);
#endif
            }
            else
            {
                glDrawElements(mesh_mode, count,
                               layout->index_gl_type, offset_ptr);
            }
        }
    }
    else
    {
        if (instance_count > 1)
        {
#if BUDO_GL_HAS_DRAW_INSTANCED
            glDrawArraysInstanced(mesh_mode, first,
                                  count, instance_count);
#else
            glDrawArrays(mesh_mode, first, count);
#endif
        }
        else
        {
            glDrawArrays(mesh_mode, first, count);
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    immediate_gl_cleanup_texture_units(1, 7);

    /* Restore default GL state expected by Skia and later immediate draws. */
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(0);
    if (target.is_screen)
        window->screen_rendered_this_frame = true;
    else if (bind_explicit_target)
        immediate_gl_bind_screen_target(window, NULL);

    return true;
}

bool window_gl_draw_mesh_immediate(Window *window, int program_id, int layout_id,
                                   WindowGLPrimitive mode, int first, int count,
                                   int target_id,
                                   const WindowGLDrawState *state,
                                   int instance_count)
{
    GLenum cull_gl = GL_NONE;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;
    if (!get_program_slot(window, program_id))
    {
        set_error(window, "Invalid shader program id");
        return false;
    }
    if (!get_layout_slot(window, layout_id))
    {
        set_error(window, "Invalid vertex layout id");
        return false;
    }
    if (count <= 0)
    {
        set_error(window, "Mesh draw count must be > 0");
        return false;
    }
    if (target_id > 0 && !get_render_target_slot(window, target_id))
    {
        set_error(window, "Invalid render target id");
        return false;
    }

    if (state)
    {
        switch (state->cull)
        {
        case WINDOW_GL_CULL_BACK:
            cull_gl = GL_BACK;
            break;
        case WINDOW_GL_CULL_FRONT:
            cull_gl = GL_FRONT;
            break;
        default:
            cull_gl = GL_NONE;
            break;
        }
    }

    if (!immediate_gl_draw_mesh_now(window, program_id, layout_id,
                                    gl_prim_to_gl(mode), first, count,
                                    target_id, target_id != 0,
                                    state ? state->depth_test : true,
                                    state ? state->depth_write : true,
                                    cull_gl,
                                    state ? (int)state->blend : (int)WINDOW_GL_BLEND_ALPHA,
                                    instance_count))
    {
        set_error(window, "Immediate mesh draw failed");
        return false;
    }
    return true;
}

/* ── Explicit target binding and errors ──────────────────────────────── */

bool window_gl_bind_screen(Window *window)
{
    GLBoundRenderTarget target;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;
    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    if (!immediate_gl_bind_screen_target(window, &target))
        return false;
    immediate_gl_apply_render_target_viewport(&target, 0, 0, target.width, target.height);
    return true;
}

bool window_gl_bind_render_target_immediate(Window *window, int target_id)
{
    GLBoundRenderTarget target;

    if (!BUDO_GL_WINDOW_READY(window))
        return false;
    if (target_id <= 0)
        return window_gl_bind_screen(window);

    BUDO_GL_WINDOW_MAKE_CURRENT(window);
    if (!immediate_gl_bind_app_render_target(window, target_id, &target))
    {
        set_error(window, "Invalid render target id");
        return false;
    }
    immediate_gl_apply_render_target_viewport(&target, 0, 0, target.width, target.height);
    return true;
}

/* The last GL error message, or NULL when none was recorded. */
const char *window_gl_get_error(Window *window)
{
    if (!window || window->error_msg[0] == '\0')
        return NULL;
    return window->error_msg;
}

