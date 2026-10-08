#include "accessibility/lua_accessibility_bindings.h"
#include "accessibility/accessibility_service.h"

#include "lauxlib.h"
#include "lua.h"

#include <stdlib.h>
#include <string.h>

static int l_accessibility_is_available(lua_State *L)
{
    lua_pushboolean(L, accessibility_available());
    return 1;
}

static int l_accessibility_is_active(lua_State *L)
{
    lua_pushboolean(L, accessibility_active());
    return 1;
}

static void field_string(lua_State *L, const char *key, char *dst, size_t size)
{
    lua_getfield(L, -1, key);
    const char *text = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
    size_t length = text ? strlen(text) : 0;
    if (length >= size)
        length = size - 1;
    if (length)
        memcpy(dst, text, length);
    dst[length] = '\0';
    lua_pop(L, 1);
}

static bool field_number(lua_State *L, const char *key, float *out)
{
    lua_getfield(L, -1, key);
    bool present = lua_isnumber(L, -1);
    if (present)
        *out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return present;
}

static int field_flag(lua_State *L, const char *key)
{
    lua_getfield(L, -1, key);
    int flag = lua_isnil(L, -1) ? -1 : lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    return flag;
}

static int l_accessibility_update(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int count = (int)lua_rawlen(L, 1);
    if (count > ACCESSIBILITY_MAX_NODES)
        return luaL_error(L, "sys.accessibility.update: at most %d nodes", ACCESSIBILITY_MAX_NODES);
    AccessibilityNode *nodes = count ? calloc((size_t)count, sizeof(AccessibilityNode)) : NULL;
    if (count && !nodes)
        return luaL_error(L, "sys.accessibility.update: out of memory");
    for (int i = 0; i < count; i++)
    {
        AccessibilityNode *node = &nodes[i];
        lua_rawgeti(L, 1, i + 1);
        if (!lua_istable(L, -1))
        {
            free(nodes);
            return luaL_error(L, "sys.accessibility.update: node %d is not a table", i + 1);
        }
        field_string(L, "id", node->id, sizeof(node->id));
        field_string(L, "role", node->role, sizeof(node->role));
        if (!node->role[0])
            strcpy(node->role, "group");
        field_string(L, "label", node->label, sizeof(node->label));
        field_string(L, "value", node->value, sizeof(node->value));
        field_number(L, "x", &node->x);
        field_number(L, "y", &node->y);
        field_number(L, "width", &node->width);
        field_number(L, "height", &node->height);
        node->checked = (int8_t)field_flag(L, "checked");
        node->selected = field_flag(L, "selected") == 1;
        node->focused = field_flag(L, "focused") == 1;
        node->disabled = field_flag(L, "disabled") == 1;
        node->expanded = field_flag(L, "expanded") == 1;
        node->range_max = 1.0f;
        node->has_range = field_number(L, "max", &node->range_max);
        if (node->has_range)
        {
            field_number(L, "min", &node->range_min);
            field_number(L, "rangeValue", &node->range_value);
        }
        lua_pop(L, 1);
    }
    bool ok = accessibility_update(nodes, count);
    free(nodes);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_accessibility_take_actions(lua_State *L)
{
    AccessibilityAction actions[ACCESSIBILITY_MAX_ACTIONS];
    int count = accessibility_take_actions(actions, ACCESSIBILITY_MAX_ACTIONS);
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; i++)
    {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, actions[i].id);
        lua_setfield(L, -2, "id");
        lua_pushstring(L, actions[i].action);
        lua_setfield(L, -2, "action");
        lua_pushstring(L, actions[i].value);
        lua_setfield(L, -2, "value");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static const luaL_Reg accessibility_funcs[] = {
    {"isAvailable", l_accessibility_is_available},
    {"isActive", l_accessibility_is_active},
    {"update", l_accessibility_update},
    {"takeActions", l_accessibility_take_actions},
    {NULL, NULL}};

void lua_accessibility_init(void *L_void)
{
    lua_State *L = (lua_State *)L_void;
    lua_getglobal(L, "sys");
    lua_newtable(L);
    luaL_setfuncs(L, accessibility_funcs, 0);
    lua_setfield(L, -2, "accessibility");
    lua_pop(L, 1);
}