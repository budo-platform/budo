#include "accessibility/accessibility_service.h"

#include "core/json_util.h"
#include "core/platform_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BudoMutex g_mutex;
static bool g_mutex_ready;
static AccessibilityNode g_nodes[ACCESSIBILITY_MAX_NODES];
static int g_node_count = -1;
static AccessibilityAction g_actions[ACCESSIBILITY_MAX_ACTIONS];
static int g_action_count;

static void lock(void)
{
    
    if (!g_mutex_ready)
        g_mutex_ready = budo_mutex_init(&g_mutex);
    budo_mutex_lock(&g_mutex);
}

static void unlock(void)
{
    budo_mutex_unlock(&g_mutex);
}

static void copy_text(char *dst, size_t size, const char *src)
{
    size_t length = src ? strlen(src) : 0;
    if (length >= size)
        length = size - 1;
    if (length)
        memcpy(dst, src, length);
    dst[length] = '\0';
}

bool accessibility_available(void)
{
    return accessibility_platform_available();
}

bool accessibility_active(void)
{
    return accessibility_platform_available() && accessibility_platform_active();
}

bool accessibility_update(const AccessibilityNode *nodes, int count)
{
    if (count < 0 || count > ACCESSIBILITY_MAX_NODES || (count && !nodes))
        return false;
    lock();
    bool changed = count != g_node_count ||
                   (count && memcmp(g_nodes, nodes, sizeof(AccessibilityNode) * (size_t)count) != 0);
    if (changed)
    {
        if (count)
            memcpy(g_nodes, nodes, sizeof(AccessibilityNode) * (size_t)count);
        g_node_count = count;
    }
    unlock();
    if (changed && accessibility_platform_available())
        accessibility_platform_publish(nodes, count);
    return true;
}

int accessibility_copy_nodes(AccessibilityNode *out, int max)
{
    lock();
    int count = g_node_count < 0 ? 0 : g_node_count < max ? g_node_count : max;
    if (count && out)
        memcpy(out, g_nodes, sizeof(AccessibilityNode) * (size_t)count);
    unlock();
    return count;
}

int accessibility_take_actions(AccessibilityAction *out, int max)
{
    lock();
    int count = g_action_count < max ? g_action_count : max;
    if (count && out)
        memcpy(out, g_actions, sizeof(AccessibilityAction) * (size_t)count);
    memmove(g_actions, g_actions + count, sizeof(AccessibilityAction) * (size_t)(g_action_count - count));
    g_action_count -= count;
    unlock();
    return count;
}

void accessibility_post_action(const char *id, const char *action, const char *value)
{
    if (!id || !action)
        return;
    lock();
    if (g_action_count < ACCESSIBILITY_MAX_ACTIONS)
    {
        AccessibilityAction *entry = &g_actions[g_action_count++];
        copy_text(entry->id, sizeof(entry->id), id);
        copy_text(entry->action, sizeof(entry->action), action);
        copy_text(entry->value, sizeof(entry->value), value);
    }
    unlock();
}

static float json_float(const char *body, const char *key, float fallback)
{
    const char *value = json_find_key(body, key);
    return value && (*value == '-' || (*value >= '0' && *value <= '9')) ? (float)atof(value) : fallback;
}

int accessibility_parse_json(const char *json, AccessibilityNode *out, int max)
{
    const char *p = json ? json_skip_ws(json) : NULL;
    int count = 0;
    if (!p || *p != '[')
        return -1;
    p = json_skip_ws(p + 1);
    while (p && *p == '{')
    {
        if (count >= max)
            return -1;
        char *body = json_object_body(p);
        if (!body)
            return -1;
        AccessibilityNode *node = &out[count++];
        memset(node, 0, sizeof(*node));
        json_get_string(body, "id", node->id, sizeof(node->id), "");
        json_get_string(body, "role", node->role, sizeof(node->role), "group");
        json_get_string(body, "label", node->label, sizeof(node->label), "");
        json_get_string(body, "value", node->value, sizeof(node->value), "");
        node->x = json_float(body, "x", 0.0f);
        node->y = json_float(body, "y", 0.0f);
        node->width = json_float(body, "width", 0.0f);
        node->height = json_float(body, "height", 0.0f);
        const char *checked = json_find_key(body, "checked");
        node->checked = !checked ? -1 : strncmp(checked, "true", 4) == 0 ? 1 : 0;
        node->selected = json_get_bool(body, "selected", false);
        node->focused = json_get_bool(body, "focused", false);
        node->disabled = json_get_bool(body, "disabled", false);
        node->expanded = json_get_bool(body, "expanded", false);
        if (json_find_key(body, "max"))
        {
            node->has_range = true;
            node->range_min = json_float(body, "min", 0.0f);
            node->range_max = json_float(body, "max", 1.0f);
            node->range_value = json_float(body, "rangeValue", 0.0f);
        }
        free(body);
        p = json_skip_ws(json_skip_value(p));
        if (p && *p == ',')
            p = json_skip_ws(p + 1);
    }
    return p && *p == ']' ? count : -1;
}

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} JsonBuffer;

static void json_append(JsonBuffer *buffer, const char *text, size_t length)
{
    if (buffer->failed)
        return;
    if (buffer->length + length + 1 > buffer->capacity)
    {
        size_t capacity = (buffer->capacity ? buffer->capacity * 2 : 1024) + length;
        char *data = realloc(buffer->data, capacity);
        if (!data)
        {
            buffer->failed = true;
            return;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

static void json_append_string(JsonBuffer *buffer, const char *key, const char *value)
{
    char escaped[8];
    json_append(buffer, "\"", 1);
    json_append(buffer, key, strlen(key));
    json_append(buffer, "\":\"", 3);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
    {
        if (*p == '"' || *p == '\\')
            snprintf(escaped, sizeof(escaped), "\\%c", *p);
        else if (*p < 0x20)
            snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
        else
            escaped[0] = (char)*p, escaped[1] = '\0';
        json_append(buffer, escaped, strlen(escaped));
    }
    json_append(buffer, "\",", 2);
}

char *accessibility_nodes_to_json(const AccessibilityNode *nodes, int count)
{
    JsonBuffer buffer = {NULL, 0, 0, false};
    char number[160];
    json_append(&buffer, "[", 1);
    for (int i = 0; i < count; i++)
    {
        const AccessibilityNode *node = &nodes[i];
        json_append(&buffer, i ? ",{" : "{", i ? 2 : 1);
        json_append_string(&buffer, "id", node->id);
        json_append_string(&buffer, "role", node->role);
        json_append_string(&buffer, "label", node->label);
        json_append_string(&buffer, "value", node->value);
        int length = snprintf(number, sizeof(number),
                              "\"x\":%.1f,\"y\":%.1f,\"width\":%.1f,\"height\":%.1f,\"selected\":%s,"
                              "\"focused\":%s,\"disabled\":%s,\"expanded\":%s",
                              node->x, node->y, node->width, node->height, node->selected ? "true" : "false",
                              node->focused ? "true" : "false", node->disabled ? "true" : "false",
                              node->expanded ? "true" : "false");
        json_append(&buffer, number, (size_t)length);
        if (node->checked >= 0)
            json_append(&buffer, node->checked ? ",\"checked\":true" : ",\"checked\":false",
                        node->checked ? 15 : 16);
        if (node->has_range)
        {
            length = snprintf(number, sizeof(number), ",\"min\":%g,\"max\":%g,\"rangeValue\":%g",
                              node->range_min, node->range_max, node->range_value);
            json_append(&buffer, number, (size_t)length);
        }
        json_append(&buffer, "}", 1);
    }
    json_append(&buffer, "]", 1);
    if (buffer.failed)
    {
        free(buffer.data);
        return NULL;
    }
    return buffer.data;
}