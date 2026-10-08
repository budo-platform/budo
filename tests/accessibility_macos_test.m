/* VoiceOver bridge: published nodes become accessibility elements of the
 * window's content view, with screen frames and working actions. */

#import <AppKit/AppKit.h>

#include "accessibility/accessibility_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static AccessibilityNode node(const char *id, const char *role, const char *label, float x, float y)
{
    AccessibilityNode n;
    memset(&n, 0, sizeof(n));
    strcpy(n.id, id);
    strcpy(n.role, role);
    strcpy(n.label, label);
    n.x = x;
    n.y = y;
    n.width = 200;
    n.height = 40;
    n.checked = -1;
    return n;
}

int main(void)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 400, 300)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.title = @"Budo test";
        [window orderFrontRegardless];
        CGFloat scale = window.backingScaleFactor;

        AccessibilityNode nodes[4] = {
            node("save", "button", "Save", 0, 0),
            node("dark", "switch", "Dark mode", 0, 50 * scale),
            node("level", "slider", "Volume", 0, 100 * scale),
            node("name", "textbox", "Name", 0, 150 * scale),
        };
        nodes[1].checked = 1;
        nodes[2].has_range = true;
        nodes[2].range_max = 100;
        nodes[2].range_value = 40;
        strcpy(nodes[2].value, "40%");
        strcpy(nodes[3].value, "Ada");
        assert(accessibility_available());
        accessibility_platform_publish(nodes, 4);

        NSArray *children = window.contentView.accessibilityChildren;
        assert(children.count == 4);
        id save = children[0], dark = children[1], level = children[2], name = children[3];
        assert([[save accessibilityRole] isEqualToString:NSAccessibilityButtonRole]);
        assert([[save accessibilityLabel] isEqualToString:@"Save"]);
        assert([[dark accessibilitySubrole] isEqualToString:NSAccessibilitySwitchSubrole]);
        assert([[dark accessibilityValue] isEqual:@1]);
        assert([[level accessibilityRole] isEqualToString:NSAccessibilitySliderRole]);
        assert([[level accessibilityMaxValue] isEqual:@100]);
        assert([[level accessibilityValueDescription] isEqualToString:@"40%"]);
        assert([[name accessibilityValue] isEqualToString:@"Ada"]);
        assert([name accessibilityParent] == window.contentView);

        /* Frames are in screen points, origin bottom-left: the first node
         * sits at the top of the content area. */
        NSRect content = [window convertRectToScreen:window.contentView.frame];
        NSRect frame = [save accessibilityFrame];
        assert(fabs(frame.size.width - 200 / scale) < 0.5 && fabs(frame.size.height - 40 / scale) < 0.5);
        assert(fabs(NSMaxY(frame) - NSMaxY(content)) < 0.5 && fabs(frame.origin.x - content.origin.x) < 0.5);

        [save accessibilityPerformPress];
        [level accessibilityPerformIncrement];
        [name setAccessibilityValue:@"Grace"];
        AccessibilityAction actions[4];
        int count = accessibility_take_actions(actions, 4);
        assert(count == 3);
        assert(!strcmp(actions[0].id, "save") && !strcmp(actions[0].action, "press"));
        assert(!strcmp(actions[1].id, "level") && !strcmp(actions[1].action, "increment"));
        assert(!strcmp(actions[2].action, "setValue") && !strcmp(actions[2].value, "Grace"));

        /* Elements are reused by id, so VoiceOver keeps its place. */
        accessibility_platform_publish(nodes, 2);
        assert(window.contentView.accessibilityChildren.count == 2);
        assert(window.contentView.accessibilityChildren[0] == save);
        [window close];
    }
    puts("accessibility_macos_test: ok");
    return 0;
}
