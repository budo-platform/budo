# Budo UI

An immediate-mode UI toolkit for Budo canvas apps, with motion built in. The
app describes the UI every frame; the library animates between frames. Run the
demo with `./build/budo examples/budo-ui`, and see [ROADMAP.md](ROADMAP.md) for
where it is going.

Start a project with it using `budo init my-app --template ui`: the library goes
to `my-app/ui/` (import `./ui/budo-ui.js`), next to a starter `main.js`.
Examples in this repository that use it (`examples/07_file_explorer`,
`examples/midi_preset_saver`) carry the
same `ui/` copy; `scripts/sync-budo-ui.py` refreshes those copies after a
library change, and CTest fails when one is stale.

## A simple UI stays simple

```js
import { createUI } from './budo-ui.js';

const ui = createUI();
const name = { value: '' };
let enabled = false;

function frame() {
    ui.begin(sys.input.get());          // bounds default to the window
    ui.clear();
    const [field, toggle, submit] = ui.rows(ui.inset(ui.bounds, ui.dp(16)), [48, 48, 48].map(ui.dp));
    ui.field('name', field, name, { placeholder: 'Name' });
    enabled = ui.toggle('enabled', 'Enabled', toggle, enabled);
    if (ui.button('submit', 'Submit', submit, { primary: true })) console.log(name.value, enabled);
    ui.end();
    ui.nextFrame(frame); // next frame while something moves, otherwise on the next input
}
sys.animation.requestFrame(frame);
```

Nothing here mentions animation, yet hover and press colors, the button's press
scale, the toggle's sliding thumb, and the field's focus ring all animate.

## Principles

- **The app owns its data.** Rebuild the view from it every frame and give
  interactive controls stable ids. The library keeps only interaction state
  (active drag, focus, scroll offsets, splitter ratios) and motion state, and
  forgets ids that stop being drawn. Create the UI once, outside the frame loop.
- **Controls return results immediately.** `button` and `choice` return `true`
  when clicked, `toggle`, `checkbox`, `slider`, and `select` return the new
  value, and `field` edits a `{value}` model in place (with IME composition and
  soft keyboards through Budo text sessions).
- **Plain rectangles for layout.** `rows` and `columns` split a rectangle by
  fixed sizes or `{weight: n}` shares; `inset`, `split`, `scroll`, and `panel`
  compose them. Coordinates are physical canvas pixels; `ui.dp(n)` converts
  density-independent sizes, and theme sizes (fonts, radii, gaps) are already
  density-independent.
- **Draw back to front.** When widgets overlap, the one drawn last gets the
  pointer. `ui.overlay(draw)` draws above everything (menus, popovers, toasts).
- **One theme.** `createUI(overrides)` merges tokens with `themes.light`;
  `ui.setTheme(themes.dark)` switches theme and every color animates there.
  Besides colors and sizes, `elevation` sets the three shadow levels and
  `accentGradient` (for example `['#14A394', '#006F67']`, top to bottom)
  replaces flat accent fills on primary buttons, progress bars, and toggles.

## Controls

- Actions: `button` (variants `secondary`, `primary`, `ghost`, `danger`, and an
  optional `icon`), `iconButton`, `menu`, `contextMenu`.
- Values: `toggle`, `checkbox`, `radio`, `segmented`, `tabs`, `select`,
  `slider`, `field`, `inlineEdit`, `textArea` (multiline).
- Content: `label`, `paragraph`, `richText`, `progress`, `spinner`,
  `skeleton`, `tooltip`, `toast`.
- Data: `table` (virtualized rows, sortable headers, resizable columns) and
  `tree` (expand and collapse, virtualized); both move the selection with the
  arrows and report Enter or a double click as `activatedId`.
- Structure: `panel`, `split`, `scroll`, `list` (reorder, swipe actions),
  `navigator` and `shared` (page transitions), `dialog`, `sheet`, `glass`,
  `layer`, `overlay`.
- Drawing: `icon(name, rect, { color })` draws one of the built-in icons
  (`ICONS`: check, close, plus, chevrons, arrows, menu, more, search, trash,
  edit, copy, star, heart, home, user, info, alert, refresh, sun, moon, folder,
  file), an
  icon added with `registerIcon(name, d)`, or raw SVG path data on a 24-unit
  grid, in any color. `shadow(rect, level)` draws the drop shadow of an
  elevated surface (levels 1 to 3).
- Custom widgets: `fill`, `outline`, `text`, and `interact(id, rect, options)`
  (returns `{hovered, held, clicked, focused}`); pass `a11y` to make them
  focusable and readable.

```js
color = ui.select('color', rect, ['Red', 'Green', 'Blue'], color);
const result = ui.list('library', rect, items, { selectedId, reorder: true });
ui.split('workspace', rect, drawSidebar, drawInspector, { ratio: 0.33 });
ui.scroll('inspector', rect, contentHeight, content => { /* draw in content */ });
ui.scope('row:' + item.id, () => drawRow(item));   // ids inside are prefixed

const { height } = ui.measureParagraph(notes, rect.width);  // size it, then draw it
ui.paragraph(notes, rect, { color: ui.colors.muted, maxLines: 4 });
ui.textArea('notes', rect, notesModel, { placeholder: 'Notes' });  // edits notesModel.value
ui.richText(['Saved ', { text: 'Dashboard', color: ui.colors.accent }], rect);
ui.skeleton(rect);                                 // shimmering placeholder while loading
if (ui.iconButton('delete', 'trash', rect, { label: 'Delete' })) remove();
ui.button('add', 'Add', rect, { primary: true, icon: 'plus' });
ui.registerIcon('bolt', 'M13 3L5 14h6l-1 7 8-11h-6z');  // 24-unit SVG path data
ui.glass(rect);                                    // frosted panel over what is drawn below

// Tables ask only for the rows they show; the app sorts when the header asks.
const files = ui.table('files', rect, {
    columns: [{ key: 'name', label: 'Name', sortable: true }, { key: 'size', label: 'Size', width: 90, align: 'right' }],
    rowCount: rows.length, row: i => rows[i], rowId: i => rows[i].id, selectedId, sort,
});
if (files.sort) { sort = files.sort; sortRows(sort); }
selectedId = ui.tree('outline', rect, nodes, { selectedId }).selectedId;  // nodes: { id, label, children?, icon? }

size = ui.segmented('size', rect, ['S', 'M', 'L'], size);
const action = ui.menu('more', rect, 'More', [{ value: 'delete', label: 'Delete', danger: true }]);
if (ui.dialog('confirm', open, content => drawBody(content), { title: 'Delete?' })) open = false;
ui.toast('Saved');                                 // shows, stacks, and leaves by itself

// Navigation: the app owns the stack; pages slide, shared elements morph.
if (ui.navigator('nav', rect, stack, (route, page) => drawPage(route, page))) stack.pop();
ui.shared('photo:' + id, thumbRect, r => drawPhoto(r));
```

`select` takes `enabled: false`; `list` rows take `draggable: false` (no
reorder handle) and reorder only among rows of the same `kind` and `groupId`;
`scroll` and `list` take `dragThreshold` (dp) for touch slop, larger on stage
or for shaky hands.

`rows` and `columns` also take `{ content: n }` sizes measured with
`ui.textWidth(text)` or `ui.buttonWidth(label)`: they get their natural size
and shrink together when the space runs out.

## Keyboard and gestures

- Tab and Shift+Tab move the keyboard focus through the widgets in draw
  order (Up and Down too); Enter or Space activates the focused one. A focus
  ring glides between widgets after keyboard use. Scroll areas reveal the
  focused widget, menus and selects move the focus into their items, and
  dialogs and sheets keep it inside until they close.
- Text areas wrap and scroll to follow the caret. The mouse selects by
  dragging, double-click selects a word and triple-click a line. Alt or Ctrl
  with the arrows moves by words, Up and Down keep the column, Home and End
  go to the line ends (with Ctrl or Cmd, the text ends), held keys repeat, and
  Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) undo and redo whole runs of typing. On the
  web and Android the platform editor handles text changes and undo.
- On touch screens, text fields and text areas focus (and open the on-screen
  keyboard) when a tap lifts, so a finger that starts a scroll on a field
  scrolls instead; a scroll keeps the field being typed in, and a tap
  elsewhere ends editing. While the keyboard is up, `ui.bounds` shrinks above
  it (`sys.device.getPreferences().keyboardInset`) and the focused field
  scrolls into view in its scroll area.
- Touch scrolling carries momentum and stretches past the ends; list rows
  with a `swipe` action slide away (the list returns `swipedId`); sheets drag
  down to dismiss; dragging from a navigator's left edge previews going back.

## Gallery and snapshots

`gallery.js` draws every widget (the demo's "Gallery" view). The snapshot
tests draw each gallery section with a recording canvas and compare the draw
calls with `snapshots/*.json`, so any visual change shows up as a diff:

```sh
node --test examples/budo-ui/budo-ui.snapshots.test.mjs
UPDATE_SNAPSHOTS=1 node --test examples/budo-ui/budo-ui.snapshots.test.mjs   # accept changes
```

Lists animate on their own: rows glide to new positions when items are
inserted, removed, filtered, or reordered, and new rows fade in. With
`reorder`, the dragged row follows the pointer while the others make room;
on drop the list returns `move: {fromId, toId}` for the app to apply.

## Motion

```js
const x = ui.spring('drawer', open ? 0 : -ui.dp(320));       // animated number
const tint = ui.color('banner', error ? ui.theme.danger : ui.theme.accent);
const box = ui.rect('card', targetRect);                     // animated rectangle

// Enter and exit: draw(t) runs while visible or animating out, t: 0 → 1.
ui.presence('toast', visible, t =>
    ui.layer(rect, { opacity: t, y: (1 - t) * ui.dp(24) }, () => drawToast(rect)));
```

- `ui.spring(key, target, preset?)` starts at its target and moves when the
  target changes. Springs are frame-rate independent and interruptible: a new
  target redirects the motion without a jump. Presets: `springs.default`,
  `snappy`, `gentle`, `bouncy`, or `{stiffness, damping}`.
- `ui.color` interpolates in OKLab, so transitions stay perceptually even.
- `ui.layer(rect, {opacity, x, y, scale, backdrop}, draw)` fades, slides, and
  scales a group around the rectangle's center; hit testing follows the
  transform. The group fades as a whole, and `backdrop` blurs what is below it
  (the blur grows with the opacity), for frosted menus and toasts.
- `ui.reducedMotion = true` makes every animation jump to its end.
- `ui.animating` (after `ui.end()`) tells whether anything is still moving.
- `Spring`, `springs`, `mixColors`, `withAlpha`, and the older `Smoothed`,
  `clamp`, `lerp`, `smoothing` helpers are exported for custom work.

## The system

budo-ui follows the platform without any code in the app:

- **Preferences.** Reduced motion follows the system until the app sets
  `ui.reducedMotion`; `ui.setTheme('system')` follows dark mode; text sizes
  (`ui.sp(n)`) follow the user's text scale; default bounds avoid notches and
  system bars. `ui.system` holds the raw preferences.
- **Cursors.** Hovered widgets set the mouse cursor (pointer, text, resize,
  grab). Custom widgets pass `{ cursor }` to `ui.interact`.
- **Clipboard.** Fields support select all, copy, cut, paste, and Shift+arrow
  selection on desktop (web and Android editors handle it themselves).
- **Haptics.** Toggles, picks, and list drops tap on devices with haptics;
  `ui.haptic(kind)` plays one, `ui.haptics = false` turns them off.
- **Screen readers.** Every widget describes itself (role, label, state, value)
  and screen-reader actions press buttons, flip toggles, step sliders, and fill
  fields. Custom widgets pass `{ a11y: { role, label, ... } }` to `ui.interact`.
- **Battery.** `ui.nextFrame(frame)` asks for the next frame while something
  animates and otherwise waits for input; `ui.wakeAfter(seconds)` wakes the app
  for timed changes (the field caret blinks this way).

## Files

- `budo-ui.js`: the entry point; `ui-library.js` re-exports it for older imports.
- `lib/core.js`: frame lifecycle, hit testing, ids, motion state, layers, overlays.
- `lib/platform.js`: preferences, cursors, accessibility, haptics, frame scheduling.
- `lib/icons.js`: the built-in icons and `icon`/`registerIcon`.
- `lib/focus.js`: keyboard focus, focus ring, focus traps.
- `lib/widgets/`: `basic.js`, `containers.js`, `text.js`, `textarea.js`, `data.js`, `select.js`, `content.js`,
  `choices.js`, `overlays.js`, `feedback.js`, `navigation.js`.
- `gallery.js` and `snapshots/`: the widget gallery and its draw-command snapshots.
- `lib/motion.js`, `lib/color.js`, `lib/theme.js`, `lib/layout.js`, `lib/math.js`.
- `budo-ui.test.mjs`, `budo-ui.snapshots.test.mjs`: run with `node --test` (CTest runs both).
- `main.js`: the demo (themes, select, list reordering, wrapped notes, a
  skeleton-then-gradient preview, a frosted toast, reduced motion).

Gradients, rounded clips, layers, paragraphs, and icons need a Budo runtime
with `sys.canvas.setGradient`, `clipRoundRect`, `saveLayer`, `drawParagraph`,
and `sys.path.addSvg`. On older runtimes the library falls back to flat fills,
rectangular clips, per-color opacity, and single truncated lines, and draws no
icons.
