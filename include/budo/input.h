#ifndef BUDO_PUBLIC_INPUT_H
#define BUDO_PUBLIC_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <budo/core.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef int32_t BudoPointerButton;
    enum
    {
        BUDO_POINTER_LEFT = 0,
        BUDO_POINTER_MIDDLE = 1,
        BUDO_POINTER_RIGHT = 2
    };

    typedef int32_t BudoKey;

    enum
    {
        BUDO_KEY_UNKNOWN = 0,
        BUDO_KEY_A = 4,
        BUDO_KEY_B = 5,
        BUDO_KEY_C = 6,
        BUDO_KEY_D = 7,
        BUDO_KEY_E = 8,
        BUDO_KEY_F = 9,
        BUDO_KEY_G = 10,
        BUDO_KEY_H = 11,
        BUDO_KEY_I = 12,
        BUDO_KEY_J = 13,
        BUDO_KEY_K = 14,
        BUDO_KEY_L = 15,
        BUDO_KEY_M = 16,
        BUDO_KEY_N = 17,
        BUDO_KEY_O = 18,
        BUDO_KEY_P = 19,
        BUDO_KEY_Q = 20,
        BUDO_KEY_R = 21,
        BUDO_KEY_S = 22,
        BUDO_KEY_T = 23,
        BUDO_KEY_U = 24,
        BUDO_KEY_V = 25,
        BUDO_KEY_W = 26,
        BUDO_KEY_X = 27,
        BUDO_KEY_Y = 28,
        BUDO_KEY_Z = 29,
        BUDO_KEY_1 = 30,
        BUDO_KEY_2 = 31,
        BUDO_KEY_3 = 32,
        BUDO_KEY_4 = 33,
        BUDO_KEY_5 = 34,
        BUDO_KEY_6 = 35,
        BUDO_KEY_7 = 36,
        BUDO_KEY_8 = 37,
        BUDO_KEY_9 = 38,
        BUDO_KEY_0 = 39,
        BUDO_KEY_ENTER = 40,
        BUDO_KEY_ESCAPE = 41,
        BUDO_KEY_BACKSPACE = 42,
        BUDO_KEY_TAB = 43,
        BUDO_KEY_SPACE = 44,
        BUDO_KEY_RIGHT = 79,
        BUDO_KEY_LEFT = 80,
        BUDO_KEY_DOWN = 81,
        BUDO_KEY_UP = 82
    };

    typedef struct BudoPointer
    {
        int32_t x;
        int32_t y;
        int32_t dx;
        int32_t dy;
        int32_t wheel_x;
        int32_t wheel_y;
        bool down[3];
        bool pressed[3];
        bool released[3];
    } BudoPointer;

    typedef struct BudoTouchPoint
    {
        int32_t id;
        int32_t x;
        int32_t y;
        int32_t dx;
        int32_t dy;
        bool pressed;
        bool released;
    } BudoTouchPoint;

    typedef struct BudoModifiers
    {
        bool shift;
        bool control;
        bool alt;
        bool meta;
    } BudoModifiers;

    bool budo_input_pointer(const BudoInput *input, BudoPointer *out_pointer);
    uint32_t budo_input_touch_count(const BudoInput *input);
    bool budo_input_touch(const BudoInput *input, uint32_t index,
                          BudoTouchPoint *out_touch);
    bool budo_input_key_down(const BudoInput *input, BudoKey key);
    bool budo_input_key_pressed(const BudoInput *input, BudoKey key);
    bool budo_input_key_released(const BudoInput *input, BudoKey key);
    BudoModifiers budo_input_modifiers(const BudoInput *input);
    bool budo_input_focused(const BudoInput *input);

#ifdef __cplusplus
}
#endif

#endif