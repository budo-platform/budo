#include "core/input.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    int starts;
    int updates;
    int stops;
} PlatformCalls;

static void platform_start(void *context, const char *text, int start, int end,
                           bool multiline)
{
    PlatformCalls *calls = (PlatformCalls *)context;
    calls->starts++;
    (void)text;
    (void)start;
    (void)end;
    (void)multiline;
}

static void platform_update(void *context, const char *text, int start, int end,
                            int x, int y, int width, int height)
{
    PlatformCalls *calls = (PlatformCalls *)context;
    calls->updates++;
    (void)text;
    (void)start;
    (void)end;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void platform_stop(void *context)
{
    PlatformCalls *calls = (PlatformCalls *)context;
    calls->stops++;
}

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "input_text_test: %s\n", message);
    return 1;
}

int main(void)
{
    InputState input;
    PlatformCalls calls = {0};
    const InputTextPlatformCallbacks callbacks = {
        platform_start, platform_update, platform_stop};
    const char *expected = "azerty \xC3\xA9";

    input_init(&input);
    input_append_text(&input, "azerty ");
    input_append_codepoint(&input, 0x00E9);

    if (check(strcmp(input.text, expected) == 0,
              "committed UTF-8 text differs"))
        return 1;
    if (check(input.text_length == strlen(expected),
              "committed UTF-8 length differs"))
        return 1;

    input_begin_frame(&input);
    if (check(input.text_length == 0 && input.text[0] == '\0',
              "committed text was not reset for the next frame"))
        return 1;

    input_text_set_platform(&input, &callbacks, &calls);
    if (check(input_text_start(&input, "A\xF0\x9F\x99\x82Z", 3, 99, false),
              "text session did not start"))
        return 1;
    if (check(calls.starts == 1 && input.text_selection_start == 3 &&
                  input.text_selection_end == 4,
              "UTF-16 selection or start callback differs"))
        return 1;
    input_text_update(&input, "hello", 2, 2, 10, 20, 1, 18);
    if (check(calls.updates == 1 && input.text_caret_x == 10,
              "text session update differs"))
        return 1;
    input_text_set_edit(&input, "hallo", 2, 2);
    input_text_set_composition(&input, "\xE3\x81\x82", 1, 1, true);
    if (check(input.text_edit_changed && input.composition_active &&
                  input.composition_changed,
              "edit or composition update was not published"))
        return 1;
    input_begin_frame(&input);
    if (check(!input.text_edit_changed && !input.composition_changed &&
                  input.composition_active &&
                  strcmp(input.composition_text, "\xE3\x81\x82") == 0,
              "composition did not persist across frames"))
        return 1;
    input_text_stop(&input);
    if (check(calls.stops == 1 && !input.text_session_active &&
                  !input.composition_active,
              "text session did not stop"))
        return 1;

    puts("input_text_test: ok");
    return 0;
}