#include "accessibility/accessibility_service.h"

bool accessibility_platform_available(void)
{
    return false;
}

bool accessibility_platform_active(void)
{
    return false;
}

void accessibility_platform_publish(const AccessibilityNode *nodes, int count)
{
    (void)nodes;
    (void)count;
}