#include "native_host_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static uintptr_t current_thread_token(void)
{
#ifdef _WIN32
    return (uintptr_t)GetCurrentThreadId();
#else
    return (uintptr_t)pthread_self();
#endif
}

static bool valid_key(BudoKey key)
{
    return key >= 0 && key < INPUT_MAX_KEYS;
}

void budo_native_host_init(BudoHost *host, const InputState *input,
                           BudoNativeLogCallback log_callback,
                           void *log_context)
{
    if (!host)
        return;

    memset(host, 0, sizeof(*host));
    host->identity.struct_size = sizeof(host->identity);
    host->identity.api_version = BUDO_NATIVE_API_VERSION;
    host->identity.sdk_version = BUDO_VERSION_STRING;
    host->identity.sdk_build_id = BUDO_NATIVE_SDK_BUILD_ID;
    host->input.state = input;
    host->last_error.struct_size = sizeof(host->last_error);
    host->last_error.status = BUDO_STATUS_OK;
    host->last_error.message = host->error_message;
    host->log_callback = log_callback;
    host->log_context = log_context;
    host->canvas.host = host;
    host->graphics_generation = 1;
}

void budo_native_host_set_input(BudoHost *host, const InputState *input)
{
    if (host)
        host->input.state = input;
}

void budo_native_host_set_canvas(BudoHost *host, SkiaCanvas *canvas)
{
    if (host)
        host->canvas.implementation = canvas;
}

void budo_native_host_set_graphics_window(BudoHost *host, Window *window)
{
    if (host)
        host->graphics_window = window;
}

void budo_native_host_surface_created(BudoHost *host)
{
    if (!host)
        return;
    host->surface_active = host->canvas.implementation != NULL;
}

void budo_native_host_context_lost(BudoHost *host)
{
    if (!host)
        return;
    host->surface_active = false;
    host->canvas.implementation = NULL;
    host->graphics_generation++;
}

void budo_native_host_enter_callback(BudoHost *host, bool allows_graphics)
{
    if (!host)
        return;
    if (host->callback_depth == 0)
        host->callback_thread = current_thread_token();
    host->callback_depth++;
    host->callback_allows_graphics = allows_graphics;
}

void budo_native_host_leave_callback(BudoHost *host)
{
    if (!host || host->callback_depth == 0)
        return;
    host->callback_depth--;
    if (host->callback_depth == 0)
    {
        host->callback_allows_graphics = false;
        host->callback_thread = 0;
    }
}

bool budo_native_host_graphics_available(BudoHost *host)
{
    return host && host->surface_active && host->canvas.implementation &&
           host->callback_depth > 0 && host->callback_allows_graphics &&
           host->callback_thread == current_thread_token();
}

void budo_native_host_reset(BudoHost *host)
{
    if (!host)
        return;
    host->input.state = NULL;
    budo_native_host_context_lost(host);
    host->graphics_window = NULL;
    host->log_callback = NULL;
    host->log_context = NULL;
    host->error_message[0] = '\0';
    host->last_error.status = BUDO_STATUS_OK;
}

void budo_host_log(BudoHost *host, BudoLogLevel level, const char *message)
{
    if (!host || !message)
        return;
    if (host->log_callback)
    {
        host->log_callback(host->log_context, level, message);
        return;
    }
    fprintf(level >= BUDO_LOG_WARNING ? stderr : stdout, "%s\n", message);
}

void budo_host_set_error(BudoHost *host, BudoStatus status, const char *message)
{
    if (!host)
        return;

    host->last_error.status = status;
    if (!message)
        message = "";
    snprintf(host->error_message, sizeof(host->error_message), "%s", message);
}

const BudoError *budo_host_last_error(const BudoHost *host)
{
    return host ? &host->last_error : NULL;
}

const BudoBuildIdentity *budo_host_build_identity(const BudoHost *host)
{
    return host ? &host->identity : NULL;
}

const BudoInput *budo_host_input(const BudoHost *host)
{
    return host ? &host->input : NULL;
}

bool budo_input_pointer(const BudoInput *input, BudoPointer *out_pointer)
{
    const InputState *state;
    int i;

    if (!input || !input->state || !out_pointer)
        return false;
    state = input->state;
    memset(out_pointer, 0, sizeof(*out_pointer));
    out_pointer->x = state->mouse_x;
    out_pointer->y = state->mouse_y;
    out_pointer->dx = state->mouse_dx;
    out_pointer->dy = state->mouse_dy;
    out_pointer->wheel_x = state->mouse_wheel_x;
    out_pointer->wheel_y = state->mouse_wheel_y;
    for (i = 0; i < 3; ++i)
    {
        out_pointer->down[i] = state->mouse_buttons[i];
        out_pointer->pressed[i] = state->mouse_buttons_pressed[i];
        out_pointer->released[i] = state->mouse_buttons_released[i];
    }
    return true;
}

uint32_t budo_input_touch_count(const BudoInput *input)
{
    int count;

    if (!input || !input->state)
        return 0;
    count = input_touch_count(input->state);
    return count > 0 ? (uint32_t)count : 0;
}

bool budo_input_touch(const BudoInput *input, uint32_t index,
                      BudoTouchPoint *out_touch)
{
    const InputTouchPoint *touch;

    if (!input || !input->state || !out_touch || index > INT32_MAX)
        return false;
    touch = input_touch_get(input->state, (int)index);
    if (!touch)
        return false;
    out_touch->id = touch->id;
    out_touch->x = touch->x;
    out_touch->y = touch->y;
    out_touch->dx = touch->dx;
    out_touch->dy = touch->dy;
    out_touch->pressed = touch->pressed;
    out_touch->released = touch->released;
    return true;
}

bool budo_input_key_down(const BudoInput *input, BudoKey key)
{
    return input && input->state && valid_key(key) &&
           input_key_down(input->state, key);
}

bool budo_input_key_pressed(const BudoInput *input, BudoKey key)
{
    return input && input->state && valid_key(key) &&
           input_key_pressed(input->state, key);
}

bool budo_input_key_released(const BudoInput *input, BudoKey key)
{
    return input && input->state && valid_key(key) &&
           input_key_released(input->state, key);
}

BudoModifiers budo_input_modifiers(const BudoInput *input)
{
    BudoModifiers modifiers = {0};

    if (!input || !input->state)
        return modifiers;
    modifiers.shift = input->state->shift;
    modifiers.control = input->state->ctrl;
    modifiers.alt = input->state->alt;
    modifiers.meta = input->state->meta;
    return modifiers;
}

bool budo_input_focused(const BudoInput *input)
{
    return input && input->state && input->state->window_focused;
}