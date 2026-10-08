#include "lua_device_bindings.h"
#include "device_service.h"

#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"

static int l_device_keep_screen_on(lua_State *L)
{
    ApiError error;
    DeviceContext *state =
        (DeviceContext *)lua_touserdata(L, lua_upvalueindex(1));
    bool enabled = false;
    if (lua_gettop(L) >= 1)
        enabled = lua_toboolean(L, 1) ? true : false;
    bool ok = device_binding_state_keep_screen_on(state, enabled, &error);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int l_device_set_clipboard_text(lua_State *L)
{
    lua_pushboolean(L, device_set_clipboard_text(luaL_checkstring(L, 1)) ? 1 : 0);
    return 1;
}

static int l_device_get_clipboard_text(lua_State *L)
{
    char *text = device_get_clipboard_text();
    if (text)
        lua_pushstring(L, text);
    else
        lua_pushnil(L);
    free(text);
    return 1;
}

static int l_device_haptic(lua_State *L)
{
    DeviceHaptic kind = DEVICE_HAPTIC_LIGHT;
    const char *name = luaL_optstring(L, 1, "light");
    if (!device_haptic_from_name(name, &kind))
        return luaL_error(L, "sys.device.haptic: unknown kind '%s'", name);
    lua_pushboolean(L, device_haptic(kind) ? 1 : 0);
    return 1;
}

static void l_set_number(lua_State *L, const char *name, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, name);
}

static int l_device_get_preferences(lua_State *L)
{
    DevicePreferences preferences;
    device_get_preferences(&preferences);
    lua_newtable(L);
    lua_pushboolean(L, preferences.dark_mode);
    lua_setfield(L, -2, "darkMode");
    lua_pushboolean(L, preferences.reduced_motion);
    lua_setfield(L, -2, "reducedMotion");
    lua_pushboolean(L, preferences.high_contrast);
    lua_setfield(L, -2, "highContrast");
    l_set_number(L, "fontScale", preferences.font_scale);
    lua_newtable(L);
    l_set_number(L, "top", preferences.safe_top);
    l_set_number(L, "right", preferences.safe_right);
    l_set_number(L, "bottom", preferences.safe_bottom);
    l_set_number(L, "left", preferences.safe_left);
    lua_setfield(L, -2, "safeArea");
    l_set_number(L, "keyboardInset", preferences.keyboard_inset);
    return 1;
}

static int l_device_set_cursor(lua_State *L)
{
    DeviceCursor cursor = DEVICE_CURSOR_DEFAULT;
    const char *name = luaL_optstring(L, 1, "default");
    if (!device_cursor_from_name(name, &cursor))
        return luaL_error(L, "sys.device.setCursor: unknown cursor '%s'", name);
    lua_pushboolean(L, device_set_cursor(cursor) ? 1 : 0);
    return 1;
}

DeviceContext *lua_device_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    DeviceContext *state = device_binding_state_create();
    if (!state)
        return NULL;

    lua_getglobal(L, "sys");

    lua_newtable(L);
    lua_pushlightuserdata(L, state);
    lua_pushcclosure(L, l_device_keep_screen_on, 1);
    lua_setfield(L, -2, "keepScreenOn");
    lua_pushcfunction(L, l_device_set_clipboard_text);
    lua_setfield(L, -2, "setClipboardText");
    lua_pushcfunction(L, l_device_get_clipboard_text);
    lua_setfield(L, -2, "getClipboardText");
    lua_pushcfunction(L, l_device_haptic);
    lua_setfield(L, -2, "haptic");
    lua_pushcfunction(L, l_device_get_preferences);
    lua_setfield(L, -2, "getPreferences");
    lua_pushcfunction(L, l_device_set_cursor);
    lua_setfield(L, -2, "setCursor");
    lua_setfield(L, -2, "device");

    lua_pop(L, 1); 
    return state;
}