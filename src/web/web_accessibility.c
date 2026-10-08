#include "accessibility/accessibility_service.h"

#include <emscripten.h>
#include <stdlib.h>

EMSCRIPTEN_KEEPALIVE
void budo_web_accessibility_post(const char *id, const char *action, const char *value)
{
    accessibility_post_action(id, action, value);
}

EM_JS(void, js_hw_accessibility_publish, (const char *json), {
    var nodes = [];
    try { nodes = JSON.parse(UTF8ToString(json)); } catch (e) { return; }
    var canvas = document.getElementById("canvas");
    if (!canvas) return;
    var root = Module._hw_a11y_root;
    if (!root) {
        root = document.createElement("div");
        root.setAttribute("role", "application");
        root.setAttribute("aria-label", document.title || "Budo application");
        root.style.cssText = "position:absolute;overflow:hidden;pointer-events:none;color:transparent;" +
            "background:transparent;z-index:1;";
        (canvas.parentNode || document.body).appendChild(root);
        Module._hw_a11y_root = root;
        Module._hw_a11y_elements = {};
    }
    function post(id, action, value) {
        var args = [id, action, value || ""].map(function(text) { return stringToNewUTF8(text); });
        Module._budo_web_accessibility_post(args[0], args[1], args[2]);
        args.forEach(function(pointer) { _free(pointer); });
    }
    var rect = canvas.getBoundingClientRect();
    var parentRect = root.parentNode.getBoundingClientRect();
    root.style.left = (rect.left - parentRect.left) + "px";
    root.style.top = (rect.top - parentRect.top) + "px";
    root.style.width = rect.width + "px";
    root.style.height = rect.height + "px";
    var scaleX = rect.width / Math.max(1, canvas.width), scaleY = rect.height / Math.max(1, canvas.height);
    var elements = Module._hw_a11y_elements;
    var seen = {};
    var roles = {textbox: "textbox", combobox: "combobox", listitem: "listitem", list: "list", option: "option",
                 tab: "tab", heading: "heading", image: "img", link: "link", text: "note"};
    nodes.forEach(function(node, index) {
        var key = node.id || ("node:" + index);
        seen[key] = true;
        var element = elements[key];
        if (!element) {
            element = document.createElement("div");
            element.style.cssText = "position:absolute;pointer-events:none;overflow:hidden;white-space:nowrap;";
            element.addEventListener("click", function() { post(element._budoId, "press"); });
            element.addEventListener("focus", function() { post(element._budoId, "focus"); });
            element.addEventListener("keydown", function(event) {
                var role = element.getAttribute("role");
                if (event.key == "Enter" || event.key == " ") {
                    if (role != "textbox") { post(element._budoId, "press"); event.preventDefault(); }
                } else if (role == "slider" && (event.key == "ArrowUp" || event.key == "ArrowRight")) {
                    post(element._budoId, "increment"); event.preventDefault();
                } else if (role == "slider" && (event.key == "ArrowDown" || event.key == "ArrowLeft")) {
                    post(element._budoId, "decrement"); event.preventDefault();
                }
            });
            elements[key] = element;
            root.appendChild(element);
        }
        element._budoId = node.id;
        var role = roles[node.role] || node.role || "group";
        element.setAttribute("role", role);
        element.setAttribute("aria-label", node.label || "");
        element.textContent = node.value || node.label || "";
        element.tabIndex = role == "note" || role == "heading" || role == "img" || role == "list" ? -1 : 0;
        if (node.checked == true || node.checked == false) element.setAttribute("aria-checked", node.checked ? "true" : "false");
        else element.removeAttribute("aria-checked");
        if (role == "option" || role == "tab" || role == "listitem") element.setAttribute("aria-selected", node.selected ? "true" : "false");
        else element.removeAttribute("aria-selected");
        if (node.disabled) element.setAttribute("aria-disabled", "true"); else element.removeAttribute("aria-disabled");
        if (role == "combobox") element.setAttribute("aria-expanded", node.expanded ? "true" : "false");
        if (node.max != null) {
            element.setAttribute("aria-valuemin", node.min);
            element.setAttribute("aria-valuemax", node.max);
            element.setAttribute("aria-valuenow", node.rangeValue);
            if (node.value) element.setAttribute("aria-valuetext", node.value);
        }
        element.style.left = (node.x * scaleX) + "px";
        element.style.top = (node.y * scaleY) + "px";
        element.style.width = Math.max(1, node.width * scaleX) + "px";
        element.style.height = Math.max(1, node.height * scaleY) + "px";
        if (root.children[index] != element) root.insertBefore(element, root.children[index] || null);
        if (node.focused && document.activeElement != element && document.activeElement &&
            document.activeElement.parentNode == root) element.focus({preventScroll: true});
    });
    Object.keys(elements).forEach(function(key) {
        if (!seen[key]) { elements[key].remove(); delete elements[key]; }
    });
});

bool accessibility_platform_available(void)
{
    return true;
}

bool accessibility_platform_active(void)
{
    return true;
}

void accessibility_platform_publish(const AccessibilityNode *nodes, int count)
{
    char *json = accessibility_nodes_to_json(nodes, count);
    if (json)
        js_hw_accessibility_publish(json);
    free(json);
}