#include <string.h>
#include <stdio.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include "web/web_input.h"

static InputState *g_web_input = NULL;

EMSCRIPTEN_KEEPALIVE void web_input_text_edit(const char *text, int start, int end)
{
    if (g_web_input)
        input_text_set_edit(g_web_input, text, start, end);
}

EMSCRIPTEN_KEEPALIVE void web_input_composition(const char *text, int start,
                                                int end, int active)
{
    if (g_web_input)
        input_text_set_composition(g_web_input, text, start, end, active != 0);
}

EM_JS(void, js_text_input_start,
      (const char *text, int start, int end, int multiline), {
          const initial = UTF8ToString(text);
          let editor = Module.__budoTextEditor;
          const tag = multiline ? 'TEXTAREA' : 'INPUT';
          if (!editor || editor.tagName != tag)
          {
              if (editor)
                  editor.remove();
              editor = document.createElement(multiline ? 'textarea' : 'input');
              if (!multiline)
                  editor.type = 'text';
              editor.autocapitalize = 'off';
              editor.autocomplete = 'off';
              editor.spellcheck = false;
              Object.assign(editor.style, {
                  position : 'fixed',
                  left : '0',
                  top : '0',
                  width : '1px',
                  height : '1px',
                  opacity : '0.01',
                  color : 'transparent',
                  background : 'transparent',
                  border : '0',
                  padding : '0',
                  outline : 'none',
                  resize : 'none',
                  caretColor : 'transparent',
                  zIndex : '-1'
              });
              const sendEdit = function()
              {
                  if (editor.__budoSyncing || editor.__budoComposing)
                      return;
                  const pointer = stringToNewUTF8(editor.value);
                  _web_input_text_edit(pointer, editor.selectionStart || 0,
                                       editor.selectionEnd || 0);
                  _free(pointer);
              };
              const sendComposition = function(event, active)
              {
                  const value = event.data || String();
                  const pointer = stringToNewUTF8(value);
                  _web_input_composition(pointer, value.length, value.length,
                                         active ? 1 : 0);
                  _free(pointer);
              };
              editor.addEventListener('input', sendEdit);
              editor.addEventListener('select', sendEdit);
              editor.addEventListener('compositionstart', function(event) {
                                    editor.__budoComposing = true;
                                    sendComposition(event, true); });
              editor.addEventListener('compositionupdate', function(event) { sendComposition(event, true); });
              editor.addEventListener('compositionend', function(event) {
                                    editor.__budoComposing = false;
                                    sendComposition(event, false); });
              document.body.appendChild(editor);
              Module.__budoTextEditor = editor;
          }
          editor.__budoSyncing = true;
          editor.value = initial;
          editor.setSelectionRange(start, end);
          editor.__budoSyncing = false;
          editor.focus({preventScroll : true});
      });

EM_JS(void, js_text_input_update,
      (const char *text, int start, int end, int x, int y, int width, int height), {
          const editor = Module.__budoTextEditor;
          if (!editor)
              return;
          const value = UTF8ToString(text);
          editor.__budoSyncing = true;
          if (!editor.__budoComposing)
          {
              if (editor.value != value)
                  editor.value = value;
              if (editor.selectionStart != start || editor.selectionEnd != end)
                  editor.setSelectionRange(start, end);
          }
          editor.__budoSyncing = false;
          const canvas = Module.canvas;
          if (canvas)
          {
              const bounds = canvas.getBoundingClientRect();
              const scaleX = bounds.width / Math.max(1, canvas.width);
              const scaleY = bounds.height / Math.max(1, canvas.height);
              editor.style.left = (bounds.left + x * scaleX) + 'px';
              editor.style.top = (bounds.top + y * scaleY) + 'px';
              editor.style.width = Math.max(1, width * scaleX) + 'px';
              editor.style.height = Math.max(1, height * scaleY) + 'px';
          }
      });

EM_JS(void, js_text_input_stop, (void), {
    const editor = Module.__budoTextEditor;
    if (editor)
        editor.blur();
});

static void web_text_start(void *context, const char *text, int start, int end,
                           bool multiline)
{
    (void)context;
    js_text_input_start(text, start, end, multiline ? 1 : 0);
}

static void web_text_update(void *context, const char *text, int start, int end,
                            int x, int y, int width, int height)
{
    (void)context;
    js_text_input_update(text, start, end, x, y, width, height);
}

static void web_text_stop(void *context)
{
    (void)context;
    js_text_input_stop();
}

static const InputTextPlatformCallbacks web_text_callbacks = {
    .start = web_text_start,
    .update = web_text_update,
    .stop = web_text_stop,
    .native_editing = true,
};

#define SC_A 4
#define SC_B 5
#define SC_C 6
#define SC_D 7
#define SC_E 8
#define SC_F 9
#define SC_G 10
#define SC_H 11
#define SC_I 12
#define SC_J 13
#define SC_K 14
#define SC_L 15
#define SC_M 16
#define SC_N 17
#define SC_O 18
#define SC_P 19
#define SC_Q 20
#define SC_R 21
#define SC_S 22
#define SC_T 23
#define SC_U 24
#define SC_V 25
#define SC_W 26
#define SC_X 27
#define SC_Y 28
#define SC_Z 29
#define SC_1 30
#define SC_2 31
#define SC_3 32
#define SC_4 33
#define SC_5 34
#define SC_6 35
#define SC_7 36
#define SC_8 37
#define SC_9 38
#define SC_0 39
#define SC_RETURN 40
#define SC_ESCAPE 41
#define SC_BACKSPACE 42
#define SC_TAB 43
#define SC_SPACE 44
#define SC_MINUS 45
#define SC_EQUALS 46
#define SC_LEFTBRACKET 47
#define SC_RIGHTBRACKET 48
#define SC_BACKSLASH 49
#define SC_SEMICOLON 51
#define SC_APOSTROPHE 52
#define SC_GRAVE 53
#define SC_COMMA 54
#define SC_PERIOD 55
#define SC_SLASH 56
#define SC_CAPSLOCK 57
#define SC_F1 58
#define SC_F2 59
#define SC_F3 60
#define SC_F4 61
#define SC_F5 62
#define SC_F6 63
#define SC_F7 64
#define SC_F8 65
#define SC_F9 66
#define SC_F10 67
#define SC_F11 68
#define SC_F12 69
#define SC_PRINTSCREEN 70
#define SC_SCROLLLOCK 71
#define SC_PAUSE 72
#define SC_INSERT 73
#define SC_HOME 74
#define SC_PAGEUP 75
#define SC_DELETE 76
#define SC_END 77
#define SC_PAGEDOWN 78
#define SC_RIGHT 79
#define SC_LEFT 80
#define SC_DOWN 81
#define SC_UP 82
#define SC_NUMLOCK 83
#define SC_KP_DIVIDE 84
#define SC_KP_MULTIPLY 85
#define SC_KP_MINUS 86
#define SC_KP_PLUS 87
#define SC_KP_ENTER 88
#define SC_KP_1 89
#define SC_KP_2 90
#define SC_KP_3 91
#define SC_KP_4 92
#define SC_KP_5 93
#define SC_KP_6 94
#define SC_KP_7 95
#define SC_KP_8 96
#define SC_KP_9 97
#define SC_KP_0 98
#define SC_KP_PERIOD 99
#define SC_LCTRL 224
#define SC_LSHIFT 225
#define SC_LALT 226
#define SC_LGUI 227
#define SC_RCTRL 228
#define SC_RSHIFT 229
#define SC_RALT 230
#define SC_RGUI 231

static int dom_code_to_scancode(const char *code)
{
    if (!code || !code[0])
        return 0;

    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y' &&
        code[3] >= 'A' && code[3] <= 'Z' && code[4] == '\0')
    {
        return SC_A + (code[3] - 'A');
    }

    if (code[0] == 'D' && code[1] == 'i' && code[2] == 'g' &&
        code[3] == 'i' && code[4] == 't')
    {
        char d = code[5];
        if (d >= '1' && d <= '9' && code[6] == '\0')
            return SC_1 + (d - '1');
        if (d == '0' && code[6] == '\0')
            return SC_0;
    }

    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9')
    {
        if (code[2] == '\0')
            return SC_F1 + (code[1] - '1'); 
        if (code[1] == '1' && code[2] >= '0' && code[2] <= '2' && code[3] == '\0')
            return SC_F10 + (code[2] - '0'); 
    }

    if (code[0] == 'N' && code[1] == 'u' && code[2] == 'm' && code[3] == 'p' &&
        code[4] == 'a' && code[5] == 'd')
    {
        char c = code[6];
        if (c >= '0' && c <= '9' && code[7] == '\0')
            return (c == '0') ? SC_KP_0 : SC_KP_1 + (c - '1');
        if (strcmp(code + 6, "Enter") == 0)
            return SC_KP_ENTER;
        if (strcmp(code + 6, "Add") == 0)
            return SC_KP_PLUS;
        if (strcmp(code + 6, "Subtract") == 0)
            return SC_KP_MINUS;
        if (strcmp(code + 6, "Multiply") == 0)
            return SC_KP_MULTIPLY;
        if (strcmp(code + 6, "Divide") == 0)
            return SC_KP_DIVIDE;
        if (strcmp(code + 6, "Decimal") == 0)
            return SC_KP_PERIOD;
    }

    if (strcmp(code, "ArrowRight") == 0)
        return SC_RIGHT;
    if (strcmp(code, "ArrowLeft") == 0)
        return SC_LEFT;
    if (strcmp(code, "ArrowDown") == 0)
        return SC_DOWN;
    if (strcmp(code, "ArrowUp") == 0)
        return SC_UP;

    if (strcmp(code, "Enter") == 0)
        return SC_RETURN;
    if (strcmp(code, "Escape") == 0)
        return SC_ESCAPE;
    if (strcmp(code, "Backspace") == 0)
        return SC_BACKSPACE;
    if (strcmp(code, "Tab") == 0)
        return SC_TAB;
    if (strcmp(code, "Space") == 0)
        return SC_SPACE;
    if (strcmp(code, "Minus") == 0)
        return SC_MINUS;
    if (strcmp(code, "Equal") == 0)
        return SC_EQUALS;
    if (strcmp(code, "BracketLeft") == 0)
        return SC_LEFTBRACKET;
    if (strcmp(code, "BracketRight") == 0)
        return SC_RIGHTBRACKET;
    if (strcmp(code, "Backslash") == 0)
        return SC_BACKSLASH;
    if (strcmp(code, "Semicolon") == 0)
        return SC_SEMICOLON;
    if (strcmp(code, "Quote") == 0)
        return SC_APOSTROPHE;
    if (strcmp(code, "Backquote") == 0)
        return SC_GRAVE;
    if (strcmp(code, "Comma") == 0)
        return SC_COMMA;
    if (strcmp(code, "Period") == 0)
        return SC_PERIOD;
    if (strcmp(code, "Slash") == 0)
        return SC_SLASH;

    if (strcmp(code, "Insert") == 0)
        return SC_INSERT;
    if (strcmp(code, "Home") == 0)
        return SC_HOME;
    if (strcmp(code, "End") == 0)
        return SC_END;
    if (strcmp(code, "PageUp") == 0)
        return SC_PAGEUP;
    if (strcmp(code, "PageDown") == 0)
        return SC_PAGEDOWN;
    if (strcmp(code, "Delete") == 0)
        return SC_DELETE;

    if (strcmp(code, "CapsLock") == 0)
        return SC_CAPSLOCK;
    if (strcmp(code, "NumLock") == 0)
        return SC_NUMLOCK;
    if (strcmp(code, "ScrollLock") == 0)
        return SC_SCROLLLOCK;
    if (strcmp(code, "PrintScreen") == 0)
        return SC_PRINTSCREEN;
    if (strcmp(code, "Pause") == 0)
        return SC_PAUSE;

    if (strcmp(code, "ControlLeft") == 0)
        return SC_LCTRL;
    if (strcmp(code, "ControlRight") == 0)
        return SC_RCTRL;
    if (strcmp(code, "ShiftLeft") == 0)
        return SC_LSHIFT;
    if (strcmp(code, "ShiftRight") == 0)
        return SC_RSHIFT;
    if (strcmp(code, "AltLeft") == 0)
        return SC_LALT;
    if (strcmp(code, "AltRight") == 0)
        return SC_RALT;
    if (strcmp(code, "MetaLeft") == 0)
        return SC_LGUI;
    if (strcmp(code, "MetaRight") == 0)
        return SC_RGUI;

    return 0;
}

EM_JS(float, js_hw_get_dpr, (void), {
    return window.devicePixelRatio || 1.0;
});

static EM_BOOL on_mouse_move(int type, const EmscriptenMouseEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    float dpr = js_hw_get_dpr();
    int x = (int)(e->targetX * dpr);
    int y = (int)(e->targetY * dpr);
    input_set_mouse_position(input, x, y);
    return EM_TRUE;
}

static InputMouseButton dom_button_to_input(unsigned short button)
{
    switch (button)
    {
    case 0:
        return INPUT_MOUSE_LEFT;
    case 1:
        return INPUT_MOUSE_MIDDLE;
    case 2:
        return INPUT_MOUSE_RIGHT;
    default:
        return INPUT_MOUSE_LEFT;
    }
}

static EM_BOOL on_mouse_down(int type, const EmscriptenMouseEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    float dpr = js_hw_get_dpr();
    int x = (int)(e->targetX * dpr);
    int y = (int)(e->targetY * dpr);
    input_set_mouse_position(input, x, y);
    input_set_mouse_button(input, dom_button_to_input(e->button), true);
    return EM_TRUE;
}

static EM_BOOL on_mouse_up(int type, const EmscriptenMouseEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    float dpr = js_hw_get_dpr();
    int x = (int)(e->targetX * dpr);
    int y = (int)(e->targetY * dpr);
    input_set_mouse_position(input, x, y);
    input_set_mouse_button(input, dom_button_to_input(e->button), false);
    return EM_TRUE;
}

static EM_BOOL on_wheel(int type, const EmscriptenWheelEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    
    int dx, dy;
    if (e->deltaMode == 0)
    {
        
        dx = (int)(e->deltaX / 100.0);
        dy = (int)(e->deltaY / 100.0);
        if (e->deltaX != 0.0 && dx == 0)
            dx = (e->deltaX > 0) ? 1 : -1;
        if (e->deltaY != 0.0 && dy == 0)
            dy = (e->deltaY > 0) ? 1 : -1;
    }
    else
    {
        dx = (int)e->deltaX;
        dy = (int)e->deltaY;
    }
    
    input_set_mouse_wheel(input, dx, -dy);
    return EM_TRUE;
}

static EM_BOOL on_key_down(int type, const EmscriptenKeyboardEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;

    if (!input->text_session_active && !e->metaKey &&
        (!e->ctrlKey || e->altKey) && e->key[0] != '\0')
    {
        const unsigned char *key = (const unsigned char *)e->key;
        int length = key[0] < 0x80 ? 1 : (key[0] & 0xE0) == 0xC0 ? 2
                                     : (key[0] & 0xF0) == 0xE0   ? 3
                                     : (key[0] & 0xF8) == 0xF0   ? 4
                                                                 : 0;
        if (length > 0 && e->key[length] == '\0')
            input_append_text(input, e->key);
    }

    if (e->repeat)
        return input->text_session_active ? EM_FALSE : EM_TRUE;

    int sc = dom_code_to_scancode(e->code);
    if (sc > 0)
        input_set_key(input, sc, true);

    input_set_modifiers(input, e->shiftKey, e->ctrlKey, e->altKey, e->metaKey);

    if (input->text_session_active)
        return sc == 43 && !e->ctrlKey && !e->metaKey && !e->altKey ? EM_TRUE : EM_FALSE;

    if (!e->ctrlKey && !e->metaKey)
        return EM_TRUE; 
    return EM_FALSE;    
}

static EM_BOOL on_key_up(int type, const EmscriptenKeyboardEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;

    int sc = dom_code_to_scancode(e->code);
    if (sc > 0)
        input_set_key(input, sc, false);

    input_set_modifiers(input, e->shiftKey, e->ctrlKey, e->altKey, e->metaKey);

    if (input->text_session_active)
        return EM_FALSE;

    return EM_TRUE;
}

static EM_BOOL on_touch_start(int type, const EmscriptenTouchEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    float dpr = js_hw_get_dpr();

    for (int i = 0; i < e->numTouches; i++)
    {
        const EmscriptenTouchPoint *t = &e->touches[i];
        if (!t->isChanged)
            continue;
        int x = (int)(t->targetX * dpr);
        int y = (int)(t->targetY * dpr);
        input_touch_start(input, t->identifier, x, y);
    }

    return EM_TRUE;
}

static EM_BOOL on_touch_move(int type, const EmscriptenTouchEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    float dpr = js_hw_get_dpr();

    for (int i = 0; i < e->numTouches; i++)
    {
        const EmscriptenTouchPoint *t = &e->touches[i];
        if (!t->isChanged)
            continue;
        int x = (int)(t->targetX * dpr);
        int y = (int)(t->targetY * dpr);
        input_touch_move(input, t->identifier, x, y);
    }

    return EM_TRUE;
}

static EM_BOOL on_touch_end(int type, const EmscriptenTouchEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    float dpr = js_hw_get_dpr();

    for (int i = 0; i < e->numTouches; i++)
    {
        const EmscriptenTouchPoint *t = &e->touches[i];
        if (!t->isChanged)
            continue;
        int x = (int)(t->targetX * dpr);
        int y = (int)(t->targetY * dpr);
        input_touch_end(input, t->identifier, x, y);
    }

    return EM_TRUE;
}

static EM_BOOL on_touch_cancel(int type, const EmscriptenTouchEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;

    for (int i = 0; i < e->numTouches; i++)
    {
        const EmscriptenTouchPoint *t = &e->touches[i];
        if (!t->isChanged)
            continue;
        input_touch_cancel(input, t->identifier);
    }

    return EM_TRUE;
}

static EM_BOOL on_focus(int type, const EmscriptenFocusEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    input_set_focus(input, true);
    return EM_TRUE;
}

static EM_BOOL on_blur(int type, const EmscriptenFocusEvent *e, void *ud)
{
    InputState *input = (InputState *)ud;
    input_set_focus(input, false);
    return EM_TRUE;
}

EM_JS(void, js_hw_suppress_context_menu, (void), {
    var el = document.getElementById('canvas');
    if (el)
    {
        el.addEventListener('contextmenu', function(e) { e.preventDefault(); });
    }
});

void web_input_init(InputState *input)
{
    g_web_input = input;
    input_text_set_platform(input, &web_text_callbacks, NULL);
    if (!input)
        return;

    const char *canvas = "#canvas";

    emscripten_set_mousemove_callback(canvas, input, true, on_mouse_move);
    emscripten_set_mousedown_callback(canvas, input, true, on_mouse_down);
    emscripten_set_mouseup_callback(canvas, input, true, on_mouse_up);
    emscripten_set_wheel_callback(canvas, input, true, on_wheel);

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, input, true, on_key_down);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, input, true, on_key_up);

    emscripten_set_touchstart_callback(canvas, input, true, on_touch_start);
    emscripten_set_touchmove_callback(canvas, input, true, on_touch_move);
    emscripten_set_touchend_callback(canvas, input, true, on_touch_end);
    emscripten_set_touchcancel_callback(canvas, input, true, on_touch_cancel);

    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, input, true, on_focus);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, input, true, on_blur);

    js_hw_suppress_context_menu();
}