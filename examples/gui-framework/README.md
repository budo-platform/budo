# Retained GUI framework

This example keeps the public API deliberately small:

```ts
custom(spec)
label(text, options)
input(model, options)
button(text, onClick, options)
hStack(children, options)
vStack(children, options)
hSplit(first, second, options)
vSplit(first, second, options)
Gui(root)
```

`custom()` is the leaf-element primitive. Labels, inputs, and buttons are built
with it, so application-defined elements participate in measurement, layout,
drawing, pointer routing, and focus exactly like built-in controls.

`input()` demonstrates the complete Budo text-session lifecycle. It supports
layout-aware committed text, caret navigation, full native replacement edits,
IME preedit rendering, candidate-window positioning, and Android/web soft
keyboards while retaining physical scancodes for navigation and shortcuts.

Each axis can request a pixel size, `'content'`, or `'fill'`. A stack measures
content children first, then shares the remaining space between fill children.
Splitters assign both child rectangles directly and expose a draggable ratio.
Their visible gap and pointer target are independent: the default divider stays
10 pixels wide while its active drag band is 48 pixels. Set `hitSize` to tune it.

Run the example with:

```sh
budo run examples/gui-framework --watch
```