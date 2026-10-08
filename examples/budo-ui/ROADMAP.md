# Budo UI roadmap

Goal: grow `budo-ui` from a small immediate-mode toolkit into a complete UI
framework for Budo apps, with motion as a first-class part of the experience,
while a simple UI stays exactly as simple to write as today.

## The rule: simple stays simple

This must keep working, unchanged, at every step:

```js
import { createUI } from './budo-ui.js';
const ui = createUI();
const name = { value: '' };
let enabled = false;

function frame() {
    ui.begin(sys.input.get());          // bounds default to the window
    ui.clear();
    const [field, toggle, submit] = ui.rows(ui.inset(ui.bounds, 16), [48, 48, 48]);
    ui.field('name', field, name, { placeholder: 'Name' });
    enabled = ui.toggle('enabled', 'Enabled', toggle, enabled);
    if (ui.button('submit', 'Submit', submit, { primary: true })) console.log(name.value);
    ui.end();
    sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
```

Design rules that protect this:

- **Immediate API, retained motion.** The app describes where the UI should be
  every frame. The library remembers, per widget id, how it looked last frame
  and animates toward the new state. Hover, press, focus, toggles, selection,
  list reordering, and theme changes animate with no app code.
- **Every new capability is opt-in** through an option or a new function.
  Existing functions keep their signatures and return values.
- **The app owns its data.** The library keeps only interaction and motion
  state, keyed by id, and forgets ids that stop being drawn.
- **Plain rectangles for layout.** No retained widget tree.

## Phases

Each phase ships on its own. Status: **done** (this pass), *next*, or later.

### 0. Foundations — done

- Folder `examples/budo-ui`, entry `budo-ui.js`, implementation split into
  modules under `lib/` (math, color, motion, theme, layout, core, widgets).
  `ui-library.js` re-exports the entry for existing imports.
- Density-aware sizes: theme sizes (`font`, `row`, `radius`, `gap`, ...) are
  density-independent pixels; widgets scale them by the display density.
  `ui.dp(n)` converts app constants. Desktop density is 1, so nothing changes
  there; Android stops getting tiny widgets.
- Overlapping widgets: hover and press go to the topmost widget under the
  pointer, using last frame's hit list (draw order, then overlay layer). An
  overlay layer (`ui.overlay(draw)`) is drawn after the main content.
- Id scopes: `ui.scope(id, draw)` prefixes the ids created inside `draw`, so
  repeated components need not build unique ids by hand.
- Per-id motion state is collected after a widget stops being drawn.
- Theme v2: the same flat tokens as before plus `gap`, `padding`, `danger`,
  `focus`, `shadow`, and motion presets; `themes.light` and `themes.dark`.
- Text measurement cache (caret placement no longer re-measures every prefix
  every frame).

### 1. Motion core — done

- `Spring`: frame-rate independent, interruptible (keeps its velocity when the
  target changes), with presets `default`, `snappy`, `gentle`, `bouncy`.
- `ui.spring(key, target, preset?)`, `ui.color(key, target)` (interpolated in
  OKLab, so color transitions stay perceptually even), `ui.rect(key, rect)`.
- Built into every widget: hover/press fills, press scale, toggle thumb and
  track, checkbox check, slider thumb, field focus ring, selection highlights,
  divider, and scrollbar.
- `ui.reducedMotion = true` makes every animation jump to its target.
- `ui.animating` tells the app whether anything is still moving, so it can
  stop requesting frames when idle.
- `ui.setTheme(theme)` animates every color, including `ui.clear()`.

### 2. Layout and presence animation — partly done

- Done: `ui.presence(key, visible, draw)` for enter and exit animations, and
  `ui.layer(rect, {opacity, x, y, scale}, draw)` for fading, sliding, and
  scaling a group (hit testing follows the transform).
- Done: lists animate rows to their new positions (insertions, removals of
  rows above, filtering, reorders). While dragging, the dragged row follows the
  pointer above the others and the rest make room for it.
- Done: `ui.navigator` (a page stack with push/pop transitions and an
  interactive edge-swipe back) and `ui.shared` (elements morph between pages).

### 3. Gestures with physics — done

- Done: velocity tracking and momentum scrolling with rubber-band overscroll,
  swipe actions on list rows, swipe-to-dismiss sheets, long press (context
  menus), the navigator's back gesture.
- Later: pull-to-refresh, drag and drop between containers with snapping.

### 4. Widget completeness — mostly done

- Done: `ui.select` (a dropdown in the overlay layer, animated, closes on
  outside press or Escape).
- Done: keyboard focus navigation (Tab/Shift-Tab, Up/Down, Enter/Space) with
  an animated focus ring, scroll reveal, and focus traps; button variants
  (ghost, danger); radio group, segmented control, tabs, menu and context menu,
  tooltip, dialog, bottom sheet, toast, progress, spinner; `{ content }`
  layout sizes (from the retired `examples/gui-framework`).
- Done: icons (`ui.icon`, 27 built-in, `ui.registerIcon`), icon buttons, and
  buttons with an icon.
- Done: `ui.paragraph` / `ui.measureParagraph` (wrapped text with alignment
  and a line limit) and `ui.skeleton` (shimmering loading placeholder).
- Done: `ui.textArea`: wrapping, drag/double/triple-click selection, word
  and line navigation, key repeat, clipboard, undo/redo, caret-following
  scrolling.
- Done: `ui.table` (virtualized, sortable, resizable columns, keyboard
  selection) and `ui.tree` (virtualized, expand/collapse with the chevron or
  Left/Right).
- *Next:* touch targets of at least 44 dp; multi-selection in tables and
  lists; editable cells.

### 5. Effects and polish — started

- Done: `ui.layer` fades a group as a whole through a canvas layer
  (`saveLayer`), so overlapping parts do not show through each other.
- Done: frosted glass: `ui.glass(rect)` and `ui.layer(..., { backdrop })`,
  whose blur grows as the layer fades in. The select menu and the demo toast
  use it.
- Done: scroll areas clip to rounded corners.
- Done: elevation shadows as theme tokens (`elevation`, `ui.shadow`) and a
  gradient accent token (`accentGradient`).
- Later: per-widget surface gradients, inner shadows, animated icon morphs.

### 6. Product — started

- Done: the widget gallery (`gallery.js`, the demo's Gallery view) and
  draw-command snapshot tests of every section (`snapshots/`), run by CTest.
  `examples/gui-framework` is merged (content sizes) and retired.
- Done: `budo init --template ui` (the library in `ui/` and a starter), the
  Building interfaces guide in the documentation.
- *Next:* live snippets in the guide, versioned releases.

## Needs from the Budo runtime

Done (in `sys.canvas` for JavaScript, Lua, and WebAssembly on desktop, web,
and Android): linear, radial, and sweep gradients (`setGradient`), rounded and
path clipping (`clipRoundRect`, `clipPath`), layers with opacity and backdrop
blur (`saveLayer`), wrapped paragraphs (`drawParagraph`, `measureParagraph`),
and SVG path data (`sys.path.addSvg`). The library feature-detects them and falls back on older
runtimes.

Also done: `sys.device` clipboard, haptics, system preferences (dark mode,
reduced motion, contrast, text scale, safe areas), and mouse cursors;
`sys.animation.waitForInput` (frames on demand); `sys.canvas.drawRichText`
(mixed sizes, colors, and fonts); `sys.accessibility` (web ARIA mirror,
Android TalkBack, macOS VoiceOver). budo-ui uses all of them: it follows the system settings,
sets cursors, gives haptic feedback, copies and pastes in fields on desktop,
publishes an accessibility tree with every widget and obeys screen-reader
actions, and `ui.nextFrame(frame)` stops drawing while nothing moves.

| Still needed | Unblocks |
|---|---|
| Windows and Linux accessibility bridges (UI Automation, AT-SPI) | Screen readers there |
| Desktop environments other than GNOME on Linux | Preferences there (today: GNOME settings, `GTK_THEME`) |
