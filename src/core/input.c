#include "input.h"
#include <string.h>

static InputTouchPoint *find_touch_point(InputState *input, int pointer_id)
{
    int i;

    if (!input)
        return NULL;

    for (i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        if (input->touch_points[i].active && input->touch_points[i].id == pointer_id)
            return &input->touch_points[i];
    }

    return NULL;
}

static InputTouchPoint *find_or_allocate_touch_point(InputState *input, int pointer_id)
{
    int i;
    InputTouchPoint *slot;

    slot = find_touch_point(input, pointer_id);
    if (slot)
        return slot;

    if (!input)
        return NULL;

    for (i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        if (!input->touch_points[i].active)
        {
            memset(&input->touch_points[i], 0, sizeof(input->touch_points[i]));
            input->touch_points[i].id = pointer_id;
            return &input->touch_points[i];
        }
    }

    return NULL;
}

static InputTouchPoint *find_primary_touch(InputState *input)
{
    int i;

    if (!input)
        return NULL;

    if (input->primary_touch_id >= 0)
    {
        InputTouchPoint *primary = find_touch_point(input, input->primary_touch_id);
        if (primary)
            return primary;
    }

    for (i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        if (input->touch_points[i].active)
        {
            input->primary_touch_id = input->touch_points[i].id;
            return &input->touch_points[i];
        }
    }

    input->primary_touch_id = -1;
    return NULL;
}

static void sync_primary_touch_to_mouse(InputState *input)
{
    InputTouchPoint *primary;
    bool has_touch;
    bool had_left_down;

    if (!input)
        return;

    primary = find_primary_touch(input);
    has_touch = primary != NULL;
    had_left_down = input->mouse_buttons[INPUT_MOUSE_LEFT];

    if (primary)
    {
        input->mouse_x = primary->x;
        input->mouse_y = primary->y;
        input->mouse_dx = primary->dx;
        input->mouse_dy = primary->dy;
    }

    input->mouse_buttons[INPUT_MOUSE_LEFT] = has_touch;

    if (has_touch && !had_left_down)
        input->mouse_buttons_pressed[INPUT_MOUSE_LEFT] = true;
    else if (!has_touch && had_left_down)
        input->mouse_buttons_released[INPUT_MOUSE_LEFT] = true;
}

void input_init(InputState *input)
{
    if (!input)
        return;
    memset(input, 0, sizeof(InputState));
    input->primary_touch_id = -1;
    input->window_focused = true;
}

void input_begin_frame(InputState *input)
{
    if (input)
        input->event_count = 0;
    if (!input)
        return;

    memset(input->mouse_buttons_pressed, 0, sizeof(input->mouse_buttons_pressed));
    memset(input->mouse_buttons_released, 0, sizeof(input->mouse_buttons_released));
    memset(input->keys_pressed, 0, sizeof(input->keys_pressed));
    memset(input->keys_released, 0, sizeof(input->keys_released));
    input->text[0] = '\0';
    input->text_length = 0;
    input->text_edit_changed = false;
    input->composition_changed = false;

    for (int i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        input->touch_points[i].pressed = false;
        input->touch_points[i].released = false;
        input->touch_points[i].dx = 0;
        input->touch_points[i].dy = 0;
    }

    input->mouse_dx = 0;
    input->mouse_dy = 0;
    input->mouse_wheel_x = 0;
    input->mouse_wheel_y = 0;
}

void input_set_mouse_position(InputState *input, int x, int y)
{
    if (input)
        input->event_count++;
    if (!input)
        return;

    input->mouse_dx = x - input->mouse_x;
    input->mouse_dy = y - input->mouse_y;
    input->mouse_x = x;
    input->mouse_y = y;
}

void input_set_mouse_button(InputState *input, InputMouseButton button, bool pressed)
{
    if (input)
        input->event_count++;
    if (!input || button >= INPUT_MOUSE_MAX)
        return;

    bool was_pressed = input->mouse_buttons[button];
    input->mouse_buttons[button] = pressed;

    if (pressed && !was_pressed)
    {
        input->mouse_buttons_pressed[button] = true;
    }
    else if (!pressed && was_pressed)
    {
        input->mouse_buttons_released[button] = true;
    }
}

void input_set_mouse_wheel(InputState *input, int x, int y)
{
    if (input)
        input->event_count++;
    if (!input)
        return;
    input->mouse_wheel_x += x;
    input->mouse_wheel_y += y;
}

void input_touch_start(InputState *input, int pointer_id, int x, int y)
{
    if (input)
        input->event_count++;
    InputTouchPoint *touch;

    if (!input)
        return;

    touch = find_or_allocate_touch_point(input, pointer_id);
    if (!touch)
        return;

    touch->active = true;
    touch->pressed = true;
    touch->released = false;
    touch->dx = 0;
    touch->dy = 0;
    touch->x = x;
    touch->y = y;

    if (input->primary_touch_id < 0)
        input->primary_touch_id = pointer_id;

    sync_primary_touch_to_mouse(input);
}

void input_touch_move(InputState *input, int pointer_id, int x, int y)
{
    if (input)
        input->event_count++;
    InputTouchPoint *touch;

    if (!input)
        return;

    touch = find_or_allocate_touch_point(input, pointer_id);
    if (!touch)
        return;

    touch->dx = x - touch->x;
    touch->dy = y - touch->y;
    touch->x = x;
    touch->y = y;
    touch->active = true;

    if (input->primary_touch_id < 0)
        input->primary_touch_id = pointer_id;

    sync_primary_touch_to_mouse(input);
}

void input_touch_end(InputState *input, int pointer_id, int x, int y)
{
    if (input)
        input->event_count++;
    InputTouchPoint *touch;

    if (!input)
        return;

    touch = find_touch_point(input, pointer_id);
    if (!touch)
        return;

    touch->dx = x - touch->x;
    touch->dy = y - touch->y;
    touch->x = x;
    touch->y = y;
    touch->active = false;
    touch->released = true;
    touch->pressed = false;

    if (input->primary_touch_id == pointer_id)
        input->primary_touch_id = -1;

    sync_primary_touch_to_mouse(input);
}

void input_touch_cancel(InputState *input, int pointer_id)
{
    if (input)
        input->event_count++;
    InputTouchPoint *touch;

    if (!input)
        return;

    touch = find_touch_point(input, pointer_id);
    if (!touch)
        return;

    touch->active = false;
    touch->released = true;
    touch->pressed = false;
    touch->dx = 0;
    touch->dy = 0;

    if (input->primary_touch_id == pointer_id)
        input->primary_touch_id = -1;

    sync_primary_touch_to_mouse(input);
}

void input_touch_cancel_all(InputState *input)
{
    if (input)
        input->event_count++;
    int i;

    if (!input)
        return;

    for (i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        if (input->touch_points[i].active)
        {
            input->touch_points[i].active = false;
            input->touch_points[i].pressed = false;
            input->touch_points[i].released = true;
            input->touch_points[i].dx = 0;
            input->touch_points[i].dy = 0;
        }
    }

    input->primary_touch_id = -1;
    sync_primary_touch_to_mouse(input);
}

void input_set_key(InputState *input, int scancode, bool pressed)
{
    if (input)
        input->event_count++;
    if (!input || scancode < 0 || scancode >= INPUT_MAX_KEYS)
        return;

    bool was_pressed = input->keys[scancode];
    input->keys[scancode] = pressed;

    if (pressed && !was_pressed)
    {
        input->keys_pressed[scancode] = true;
    }
    else if (!pressed && was_pressed)
    {
        input->keys_released[scancode] = true;
    }
}

void input_append_text(InputState *input, const char *text)
{
    if (input)
        input->event_count++;
    size_t available;
    size_t length;

    if (!input || !text || text[0] == '\0')
        return;

    available = INPUT_TEXT_CAPACITY - 1 - input->text_length;
    length = strlen(text);
    if (length > available)
    {
        length = available;
        while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
            length--;
    }
    if (length == 0)
        return;

    memcpy(input->text + input->text_length, text, length);
    input->text_length += length;
    input->text[input->text_length] = '\0';
}

void input_append_codepoint(InputState *input, uint32_t codepoint)
{
    char encoded[5];

    if (codepoint == 0 || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return;

    if (codepoint <= 0x7F)
    {
        encoded[0] = (char)codepoint;
        encoded[1] = '\0';
    }
    else if (codepoint <= 0x7FF)
    {
        encoded[0] = (char)(0xC0 | (codepoint >> 6));
        encoded[1] = (char)(0x80 | (codepoint & 0x3F));
        encoded[2] = '\0';
    }
    else if (codepoint <= 0xFFFF)
    {
        encoded[0] = (char)(0xE0 | (codepoint >> 12));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = (char)(0x80 | (codepoint & 0x3F));
        encoded[3] = '\0';
    }
    else
    {
        encoded[0] = (char)(0xF0 | (codepoint >> 18));
        encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = (char)(0x80 | (codepoint & 0x3F));
        encoded[4] = '\0';
    }

    input_append_text(input, encoded);
}

static void input_copy_text(char *destination, size_t capacity,
                            size_t *out_length, const char *text)
{
    size_t length = text ? strlen(text) : 0;
    if (length >= capacity)
    {
        length = capacity - 1;
        while (length > 0 && text &&
               ((unsigned char)text[length] & 0xC0) == 0x80)
            length--;
    }
    if (length > 0 && text)
        memcpy(destination, text, length);
    destination[length] = '\0';
    if (out_length)
        *out_length = length;
}

static int input_utf16_length(const char *text)
{
    int length = 0;
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor)
    {
        uint32_t codepoint;
        int bytes;
        if (*cursor < 0x80)
        {
            codepoint = *cursor;
            bytes = 1;
        }
        else if ((*cursor & 0xE0) == 0xC0 && cursor[1])
        {
            codepoint = ((uint32_t)(cursor[0] & 0x1F) << 6) |
                        (uint32_t)(cursor[1] & 0x3F);
            bytes = 2;
        }
        else if ((*cursor & 0xF0) == 0xE0 && cursor[1] && cursor[2])
        {
            codepoint = ((uint32_t)(cursor[0] & 0x0F) << 12) |
                        ((uint32_t)(cursor[1] & 0x3F) << 6) |
                        (uint32_t)(cursor[2] & 0x3F);
            bytes = 3;
        }
        else if ((*cursor & 0xF8) == 0xF0 && cursor[1] && cursor[2] && cursor[3])
        {
            codepoint = ((uint32_t)(cursor[0] & 0x07) << 18) |
                        ((uint32_t)(cursor[1] & 0x3F) << 12) |
                        ((uint32_t)(cursor[2] & 0x3F) << 6) |
                        (uint32_t)(cursor[3] & 0x3F);
            bytes = 4;
        }
        else
        {
            codepoint = 0xFFFD;
            bytes = 1;
        }
        length += codepoint > 0xFFFF ? 2 : 1;
        cursor += bytes;
    }
    return length;
}

static int input_clamp_selection(int value, const char *text)
{
    int text_length = input_utf16_length(text);
    if (value < 0)
        return 0;
    if (value > text_length)
        return text_length;
    return value;
}

void input_text_set_platform(InputState *input,
                             const InputTextPlatformCallbacks *callbacks,
                             void *context)
{
    if (!input)
        return;
    memset(&input->text_platform, 0, sizeof(input->text_platform));
    if (callbacks)
        input->text_platform = *callbacks;
    input->text_platform_context = context;
}

bool input_text_start(InputState *input, const char *text,
                      int selection_start, int selection_end,
                      bool multiline)
{
    if (!input)
        return false;
    input_copy_text(input->text_value, sizeof(input->text_value),
                    &input->text_value_length, text);
    input->text_selection_start =
        input_clamp_selection(selection_start, input->text_value);
    input->text_selection_end =
        input_clamp_selection(selection_end, input->text_value);
    input->text_session_active = true;
    input->text_session_multiline = multiline;
    input_text_set_composition(input, "", 0, 0, false);
    if (input->text_platform.start)
        input->text_platform.start(input->text_platform_context,
                                   input->text_value,
                                   input->text_selection_start,
                                   input->text_selection_end, multiline);
    return true;
}

bool input_text_update(InputState *input, const char *text,
                       int selection_start, int selection_end,
                       int x, int y, int width, int height)
{
    if (!input || !input->text_session_active)
        return false;
    input_copy_text(input->text_value, sizeof(input->text_value),
                    &input->text_value_length, text);
    input->text_selection_start =
        input_clamp_selection(selection_start, input->text_value);
    input->text_selection_end =
        input_clamp_selection(selection_end, input->text_value);
    input->text_caret_x = x;
    input->text_caret_y = y;
    input->text_caret_width = width;
    input->text_caret_height = height;
    if (input->text_platform.update)
        input->text_platform.update(input->text_platform_context,
                                    input->text_value,
                                    input->text_selection_start,
                                    input->text_selection_end,
                                    x, y, width, height);
    return true;
}

void input_text_stop(InputState *input)
{
    if (!input || !input->text_session_active)
        return;
    if (input->text_platform.stop)
        input->text_platform.stop(input->text_platform_context);
    input->text_session_active = false;
    input_text_set_composition(input, "", 0, 0, false);
}

void input_text_set_edit(InputState *input, const char *text,
                         int selection_start, int selection_end)
{
    if (input)
        input->event_count++;
    if (!input || !input->text_session_active)
        return;
    input_copy_text(input->text_value, sizeof(input->text_value),
                    &input->text_value_length, text);
    input->text_selection_start =
        input_clamp_selection(selection_start, input->text_value);
    input->text_selection_end =
        input_clamp_selection(selection_end, input->text_value);
    input->text_edit_changed = true;
}

void input_text_set_composition(InputState *input, const char *text,
                                int selection_start, int selection_end,
                                bool active)
{
    if (input)
        input->event_count++;
    if (!input)
        return;
    input_copy_text(input->composition_text, sizeof(input->composition_text),
                    &input->composition_text_length, text);
    input->composition_selection_start =
        input_clamp_selection(selection_start, input->composition_text);
    input->composition_selection_end =
        input_clamp_selection(selection_end, input->composition_text);
    input->composition_active = active;
    input->composition_changed = true;
}

void input_set_modifiers(InputState *input, bool shift, bool ctrl, bool alt, bool meta)
{
    if (!input)
        return;
    input->shift = shift;
    input->ctrl = ctrl;
    input->alt = alt;
    input->meta = meta;
}

void input_set_focus(InputState *input, bool focused)
{
    if (input)
        input->event_count++;
    if (!input)
        return;
    input->window_focused = focused;

    if (!focused)
    {
        memset(input->keys, 0, sizeof(input->keys));
        memset(input->mouse_buttons, 0, sizeof(input->mouse_buttons));
        input_touch_cancel_all(input);
    }
}

void input_update_timing(InputState *input, double delta_time)
{
    if (!input)
        return;
    input->delta_time = delta_time;
    input->total_time += delta_time;
    input->frame_count++;
}

bool input_mouse_down(const InputState *input, InputMouseButton button)
{
    if (!input || button >= INPUT_MOUSE_MAX)
        return false;
    return input->mouse_buttons[button];
}

bool input_mouse_pressed(const InputState *input, InputMouseButton button)
{
    if (!input || button >= INPUT_MOUSE_MAX)
        return false;
    return input->mouse_buttons_pressed[button];
}

bool input_mouse_released(const InputState *input, InputMouseButton button)
{
    if (!input || button >= INPUT_MOUSE_MAX)
        return false;
    return input->mouse_buttons_released[button];
}

int input_touch_count(const InputState *input)
{
    int i;
    int count = 0;

    if (!input)
        return 0;

    for (i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        if (input->touch_points[i].active || input->touch_points[i].released)
            count++;
    }

    return count;
}

const InputTouchPoint *input_touch_get(const InputState *input, int index)
{
    int i;
    int active_index = 0;

    if (!input || index < 0)
        return NULL;

    for (i = 0; i < INPUT_MAX_TOUCH_POINTS; i++)
    {
        if (!input->touch_points[i].active && !input->touch_points[i].released)
            continue;

        if (active_index == index)
            return &input->touch_points[i];

        active_index++;
    }

    return NULL;
}

bool input_key_down(const InputState *input, int scancode)
{
    if (!input || scancode < 0 || scancode >= INPUT_MAX_KEYS)
        return false;
    return input->keys[scancode];
}

bool input_key_pressed(const InputState *input, int scancode)
{
    if (!input || scancode < 0 || scancode >= INPUT_MAX_KEYS)
        return false;
    return input->keys_pressed[scancode];
}

bool input_key_released(const InputState *input, int scancode)
{
    if (!input || scancode < 0 || scancode >= INPUT_MAX_KEYS)
        return false;
    return input->keys_released[scancode];
}