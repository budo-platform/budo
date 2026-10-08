#ifndef BUDO_ACCESSIBILITY_SERVICE_H
#define BUDO_ACCESSIBILITY_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ACCESSIBILITY_MAX_NODES 512
#define ACCESSIBILITY_MAX_ACTIONS 32

    typedef struct
    {
        char id[64];
        char role[16];   

        char label[256];
        char value[128]; 
        float x, y, width, height; 
        int8_t checked;  
        bool selected;
        bool focused;
        bool disabled;
        bool expanded;
        bool has_range;
        float range_min, range_max, range_value;
    } AccessibilityNode;

    typedef struct
    {
        char id[64];
        char action[16]; 
        char value[256];
    } AccessibilityAction;

    bool accessibility_available(void);
    bool accessibility_active(void);

    bool accessibility_update(const AccessibilityNode *nodes, int count);

    int accessibility_take_actions(AccessibilityAction *out, int max);

    void accessibility_post_action(const char *id, const char *action, const char *value);

    int accessibility_copy_nodes(AccessibilityNode *out, int max);

    int accessibility_parse_json(const char *json, AccessibilityNode *out, int max);

    char *accessibility_nodes_to_json(const AccessibilityNode *nodes, int count);

    bool accessibility_platform_available(void);
    bool accessibility_platform_active(void);
    void accessibility_platform_publish(const AccessibilityNode *nodes, int count);

#ifdef __cplusplus
}
#endif

#endif