/**
 * macOS accessibility backend (VoiceOver).
 *
 * Each published node becomes an NSAccessibilityElement child of the window's
 * content view (the SDL view Budo draws into). Frames are converted from
 * canvas pixels to screen points; presses, increments, decrements, value
 * changes, and focus requests come back as Budo accessibility actions.
 * Elements are reused by id so VoiceOver keeps its place across updates.
 * Runs on the main thread, where the desktop runtime calls the service.
 */

#import <AppKit/AppKit.h>

#include "accessibility/accessibility_service.h"

#include <stdlib.h>
#include <string.h>

@interface BudoAccessibilityElement : NSAccessibilityElement
@property(nonatomic) AccessibilityNode node;
@end

static NSString *text(const char *utf8)
{
    return [NSString stringWithUTF8String:utf8 ? utf8 : ""] ?: @"";
}

static void post(BudoAccessibilityElement *element, const char *action, NSString *value)
{
    AccessibilityNode node = element.node;
    accessibility_post_action(node.id, action, value ? value.UTF8String : "");
}

@implementation BudoAccessibilityElement

- (NSAccessibilityRole)accessibilityRole
{
    AccessibilityNode node = self.node;
    const char *role = node.role;
    if (!strcmp(role, "button") || !strcmp(role, "tab"))
        return NSAccessibilityButtonRole;
    if (!strcmp(role, "checkbox") || !strcmp(role, "switch"))
        return NSAccessibilityCheckBoxRole;
    if (!strcmp(role, "radio"))
        return NSAccessibilityRadioButtonRole;
    if (!strcmp(role, "slider"))
        return NSAccessibilitySliderRole;
    if (!strcmp(role, "textbox"))
        return NSAccessibilityTextFieldRole;
    if (!strcmp(role, "combobox"))
        return NSAccessibilityPopUpButtonRole;
    if (!strcmp(role, "list"))
        return NSAccessibilityListRole;
    if (!strcmp(role, "listitem") || !strcmp(role, "option"))
        return NSAccessibilityCellRole;
    if (!strcmp(role, "image"))
        return NSAccessibilityImageRole;
    if (!strcmp(role, "link"))
        return NSAccessibilityLinkRole;
    if (!strcmp(role, "group"))
        return NSAccessibilityGroupRole;
    return NSAccessibilityStaticTextRole;
}

- (NSAccessibilitySubrole)accessibilitySubrole
{
    AccessibilityNode node = self.node;
    if (!strcmp(node.role, "switch"))
        return NSAccessibilitySwitchSubrole;
    if (!strcmp(node.role, "tab"))
        return NSAccessibilityTabButtonSubrole;
    return nil;
}

- (NSString *)accessibilityLabel
{
    AccessibilityNode node = self.node;
    return text(node.label);
}

- (id)accessibilityValue
{
    AccessibilityNode node = self.node;
    if (node.checked >= 0)
        return @(node.checked);
    if (node.has_range)
        return @(node.range_value);
    if (!strcmp(node.role, "text") || !strcmp(node.role, "heading"))
        return text(node.label);
    return text(node.value);
}

- (NSString *)accessibilityValueDescription
{
    AccessibilityNode node = self.node;
    return node.value[0] ? text(node.value) : nil;
}

- (id)accessibilityMinValue
{
    AccessibilityNode node = self.node;
    return node.has_range ? @(node.range_min) : nil;
}

- (id)accessibilityMaxValue
{
    AccessibilityNode node = self.node;
    return node.has_range ? @(node.range_max) : nil;
}

- (BOOL)isAccessibilityEnabled
{
    AccessibilityNode node = self.node;
    return !node.disabled;
}

- (BOOL)isAccessibilitySelected
{
    AccessibilityNode node = self.node;
    return node.selected;
}

- (BOOL)isAccessibilityExpanded
{
    AccessibilityNode node = self.node;
    return node.expanded;
}

- (BOOL)isAccessibilityFocused
{
    AccessibilityNode node = self.node;
    return node.focused;
}

- (void)setAccessibilityFocused:(BOOL)focused
{
    if (focused)
        post(self, "focus", nil);
}

- (BOOL)accessibilityPerformPress
{
    post(self, "press", nil);
    return YES;
}

- (BOOL)accessibilityPerformIncrement
{
    post(self, "increment", nil);
    return YES;
}

- (BOOL)accessibilityPerformDecrement
{
    post(self, "decrement", nil);
    return YES;
}

- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector
{
    AccessibilityNode node = self.node;
    if (selector == @selector(setAccessibilityValue:))
        return !strcmp(node.role, "textbox");
    if (selector == @selector(accessibilityPerformIncrement) || selector == @selector(accessibilityPerformDecrement))
        return node.has_range;
    return [super isAccessibilitySelectorAllowed:selector];
}

- (void)setAccessibilityValue:(id)value
{
    AccessibilityNode node = self.node;
    if (!strcmp(node.role, "textbox"))
        post(self, "setValue", [value description]);
}

@end

static NSMutableDictionary<NSString *, BudoAccessibilityElement *> *g_elements;

bool accessibility_platform_available(void)
{
    return true;
}

bool accessibility_platform_active(void)
{
    const char *forced = getenv("BUDO_ACCESSIBILITY");
    if (forced && strcmp(forced, "0") != 0)
        return true;
    NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
    return workspace.voiceOverEnabled || workspace.switchControlEnabled;
}

void accessibility_platform_publish(const AccessibilityNode *nodes, int count)
{
    @autoreleasepool
    {
        NSWindow *window = NSApp.keyWindow ?: NSApp.mainWindow ?: NSApp.windows.firstObject;
        NSView *view = window.contentView;
        if (!view)
            return;
        if (!g_elements)
            g_elements = [NSMutableDictionary dictionary];
        CGFloat scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 1.0;
        CGFloat height = view.bounds.size.height;
        NSMutableArray *children = [NSMutableArray arrayWithCapacity:(NSUInteger)count];
        NSMutableDictionary *kept = [NSMutableDictionary dictionaryWithCapacity:(NSUInteger)count];
        bool focus_changed = false;
        for (int i = 0; i < count; i++)
        {
            NSString *key = text(nodes[i].id);
            BudoAccessibilityElement *element = g_elements[key] ?: [[BudoAccessibilityElement alloc] init];
            focus_changed = focus_changed || element.node.focused != nodes[i].focused;
            element.node = nodes[i];
            NSRect local = NSMakeRect(nodes[i].x / scale, nodes[i].y / scale,
                                      nodes[i].width / scale, nodes[i].height / scale);
            if (!view.isFlipped)
                local.origin.y = height - local.origin.y - local.size.height;
            element.accessibilityFrame = [window convertRectToScreen:[view convertRect:local toView:nil]];
            element.accessibilityParent = view;
            children[i] = element;
            kept[key] = element;
        }
        g_elements = kept;
        view.accessibilityElement = YES;
        view.accessibilityRole = NSAccessibilityGroupRole;
        view.accessibilityLabel = window.title;
        view.accessibilityChildren = children;
        NSAccessibilityPostNotification(view, NSAccessibilityLayoutChangedNotification);
        if (focus_changed)
            NSAccessibilityPostNotification(view, NSAccessibilityFocusedUIElementChangedNotification);
    }
}
