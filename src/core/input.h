#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define INPUT_MAX_KEYS 512

#define INPUT_MAX_TOUCH_POINTS 10

#define INPUT_TEXT_CAPACITY 1024
#define INPUT_TEXT_VALUE_CAPACITY 4096

    typedef struct
    {
        void (*start)(void *context, const char *text, int selection_start,
                      int selection_end, bool multiline);
        void (*update)(void *context, const char *text, int selection_start,
                       int selection_end, int x, int y, int width, int height);
        void (*stop)(void *context);

        bool native_editing;
    } InputTextPlatformCallbacks;

    typedef enum
    {
        INPUT_MOUSE_LEFT = 0,
        INPUT_MOUSE_MIDDLE = 1,
        INPUT_MOUSE_RIGHT = 2,
        INPUT_MOUSE_MAX = 5
    } InputMouseButton;

    typedef struct
    {
        int id;
        int x;
        int y;
        int dx;
        int dy;
        bool active;
        bool pressed;
        bool released;
    } InputTouchPoint;

    typedef struct
    {
        
        int mouse_x;
        int mouse_y;
        int mouse_dx;      
        int mouse_dy;      
        int mouse_wheel_x; 
        int mouse_wheel_y; 
        bool mouse_buttons[INPUT_MOUSE_MAX];
        bool mouse_buttons_pressed[INPUT_MOUSE_MAX];  
        bool mouse_buttons_released[INPUT_MOUSE_MAX]; 

        InputTouchPoint touch_points[INPUT_MAX_TOUCH_POINTS];
        int primary_touch_id;

        bool keys[INPUT_MAX_KEYS];
        bool keys_pressed[INPUT_MAX_KEYS];  
        bool keys_released[INPUT_MAX_KEYS]; 

        char text[INPUT_TEXT_CAPACITY];
        size_t text_length;

        bool text_session_active;
        bool text_session_multiline;
        char text_value[INPUT_TEXT_VALUE_CAPACITY];
        size_t text_value_length;
        int text_selection_start;
        int text_selection_end;
        int text_caret_x;
        int text_caret_y;
        int text_caret_width;
        int text_caret_height;

        bool text_edit_changed;

        bool composition_active;
        bool composition_changed;
        char composition_text[INPUT_TEXT_CAPACITY];
        size_t composition_text_length;
        int composition_selection_start;
        int composition_selection_end;

        InputTextPlatformCallbacks text_platform;
        void *text_platform_context;

        bool shift;
        bool ctrl;
        bool alt;
        bool meta; 

        bool window_focused;

        double delta_time;    
        double total_time;    
        uint64_t frame_count; 

        uint32_t event_count;
    } InputState;

    void input_init(InputState *input);

    void input_begin_frame(InputState *input);

    void input_set_mouse_position(InputState *input, int x, int y);

    void input_set_mouse_button(InputState *input, InputMouseButton button, bool pressed);

    void input_set_mouse_wheel(InputState *input, int x, int y);

    void input_touch_start(InputState *input, int pointer_id, int x, int y);

    void input_touch_move(InputState *input, int pointer_id, int x, int y);

    void input_touch_end(InputState *input, int pointer_id, int x, int y);

    void input_touch_cancel(InputState *input, int pointer_id);

    void input_touch_cancel_all(InputState *input);

    void input_set_key(InputState *input, int scancode, bool pressed);

    void input_append_text(InputState *input, const char *text);

    void input_append_codepoint(InputState *input, uint32_t codepoint);

    void input_text_set_platform(InputState *input,
                                 const InputTextPlatformCallbacks *callbacks,
                                 void *context);
    bool input_text_start(InputState *input, const char *text,
                          int selection_start, int selection_end,
                          bool multiline);
    bool input_text_update(InputState *input, const char *text,
                           int selection_start, int selection_end,
                           int x, int y, int width, int height);
    void input_text_stop(InputState *input);
    void input_text_set_edit(InputState *input, const char *text,
                             int selection_start, int selection_end);
    void input_text_set_composition(InputState *input, const char *text,
                                    int selection_start, int selection_end,
                                    bool active);

    void input_set_modifiers(InputState *input, bool shift, bool ctrl, bool alt, bool meta);

    void input_set_focus(InputState *input, bool focused);

    void input_update_timing(InputState *input, double delta_time);

    bool input_mouse_down(const InputState *input, InputMouseButton button);

    bool input_mouse_pressed(const InputState *input, InputMouseButton button);

    bool input_mouse_released(const InputState *input, InputMouseButton button);

    int input_touch_count(const InputState *input);

    const InputTouchPoint *input_touch_get(const InputState *input, int index);

    bool input_key_down(const InputState *input, int scancode);

    bool input_key_pressed(const InputState *input, int scancode);

    bool input_key_released(const InputState *input, int scancode);

#ifdef __cplusplus
}
#endif

#endif