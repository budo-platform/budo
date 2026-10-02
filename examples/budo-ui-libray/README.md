# Budo UI library

A small, opinionated immediate-style toolkit for Budo canvas apps. The spelling
`budo-ui-libray` is the example directory name; import `./ui-library.js` from a
JavaScript app. Run with `./build/budo examples/budo-ui-libray`.

## Principles

- Application data belongs to the app. Rebuild the view from that data on every
  frame, and assign stable, unique IDs to interactive controls. The UI instance
  keeps only interaction state: active drag/click, text focus/composition,
  scroll offsets, and splitter ratios. Create it once, outside the frame loop.
- Coordinates are Budo physical canvas pixels. Rectangles have `{x, y, width,
  height}`; `rows` and `columns` return rectangles, rather than creating a
  retained tree. Numbers are fixed sizes, `{weight: n}` divides remaining space.
- Controls draw and return actions or new values immediately. The app applies
  changes to its models. `field` takes a `{value: string}` model to integrate
  Budo's authoritative text edits and IME composition; it uses UTF-16 selection
  offsets and calls `startTextInput`, `updateTextInput`, and `stopTextInput`.
- Draw in normal back-to-front order. `scroll` clips both drawing and hit tests,
  and calls its painter with translated content coordinates. Avoid interactive
  controls overlapping each other; IDs are checked for duplicates each frame.
- Colors and metrics come from one theme. `createUI(overrides)` merges custom
  tokens with defaults, so layouts and controls share a visual language.

## Usage

```js
import { createUI } from './ui-library.js';

const ui = createUI({ accent: '#006F67' });
const name = { value: '' };
let enabled = false;

function frame() {
    const rect = { x: 16, y: 16, width: sys.window.getWidth() - 32,
        height: sys.window.getHeight() - 32 };
    const input = sys.input.get();
    sys.canvas.clear(ui.colors.background);
    ui.begin(input, rect);
    const [editor, switchRow, action] = ui.rows(rect, [48, 48, 48]);
    ui.field('name', editor, name, { placeholder: 'Name' });
    enabled = ui.toggle('enabled', 'Enabled', switchRow, enabled);
    if (ui.button('submit', 'Submit', action, { primary: true }))
        console.log(name.value, enabled);
    ui.end();
    sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
```

`button` and `choice` return `true` on a press-and-release inside the same
control. `toggle` and `slider` return their new values. `split(id, rect, first,
second, {ratio, min, max, vertical})` invokes two painters and remembers drag
position; its divider has an expanded hit area. `scroll(id, rect, contentHeight,
draw)` persists a clamped wheel/touch offset, clips the painter, and returns the
offset. `panel(rect, draw, {padding, color})` provides an inset surface;
`label`, `fill`, `text`, and `interact` allow custom widgets. `Smoothed`,
`clamp`, `lerp`, and `smoothing` cover frame-rate-independent animation.

`inlineEdit(id, value, rect, { maxLength })` draws a clickable label that becomes
an editable field with an OK button. It returns the trimmed new string on OK or
Enter, and `null` otherwise. Blank names cannot be confirmed; Escape or a click
outside cancels the draft. Use a stable ID tied to the item being edited so a
selection change cannot carry an unfinished draft into another item:

```js
const renamed = ui.inlineEdit('name:' + selected.id, selected.name, heading,
  { maxLength: 48 });
if (renamed !== null) selected.name = renamed;
```

### Grouped lists

`list(id, rect, items, { selectedId, reorder, rowHeight, indent, emptyText })`
draws clipped, scrollable rows with optional subtitle, indentation, action and
drag handle. Each item has a unique string `id` and `title`; it may also supply
`kind: 'group' | 'item'`, `groupId`, `depth`, `height`, `subtitle`, `action: true`,
and `actionLabel`. Use distinct IDs for groups and children. Each frame returns
`{ selectedId, actionId, move }`, with `null` for events that did not happen.
`move` is `{ fromId, toId }` on drop and is restricted to rows of the same kind
and `groupId`. Apply it to your app's array; the library never mutates items.
Reordering uses a dedicated handle, supports mouse and touch, and scrolls near
the top or bottom edge. Taps elsewhere still select rows; wheel/touch drags
scroll the list. For example:

```js
const result = ui.list('presets', listRect, songs.flatMap(song => [
  { id: 'song:' + song.id, title: song.name, kind: 'group', action: true, actionLabel: '+' },
  ...song.presets.map(preset => ({ id: 'preset:' + preset.id, title: preset.name,
    subtitle: preset.summary, kind: 'item', groupId: String(song.id), depth: 1,
    action: true, actionLabel: '-' })),
]), { selectedId, reorder: true });
if (result.selectedId) selectedId = result.selectedId;
if (result.move) reorderWithinGroup(result.move.fromId, result.move.toId);
```

This extracts the responsive layout and animation helpers from `layout_demo`,
clipped scrolling and selection patterns from `07_file_explorer`, value-oriented
controls from `midi_preset_saver`, and split panes plus Budo text sessions from
`gui-framework`. It is not a complete replacement for their application-specific
file handling, MIDI workflows, or custom rendering. Today text fields support
single-line edits, IME, and basic caret navigation; they are not a multiline
editor or a full keyboard accessibility layer. Scrolling currently uses the
mouse wheel and touch drag; touch inertia and multi-touch gestures are not implemented.