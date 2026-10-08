# Budo Runtime Context for Coding LLMs

## Maintenance preamble

Objective: retrieval-first context that lets coding agents build Budo apps without assuming browser, Node, DOM, p5.js, Love2D, Unity, or generic game-engine APIs. Use exact names, compact lists/constraints, and tiny recipes; avoid tutorial narrative.

On an approximate "update this file" request, regenerate from: declarations (`budo.d.ts` / `types/`), `src/**` bindings, `examples/**`, `src/core/**` CLI/init, then this file. Preserve exact symbols/signatures, policies, platform caveats, and runtime differences; remove stale, repeated, or changelog-style prose.

## Execution protocol for small models

Do not code before steps 1-4.

1. Classify: `2D canvas`, `canvas + shader`, `3D GPU`, `audio/MIDI`, `network/data`, `sensors`, `neural`, or combined.
2. Pick one family: TypeScript (`main.ts`) by default; JS when requested/types add nothing; Lua/WASM/native C only when requested. Never mix managed entrypoint families.
3. Pick desktop/web/Android/portable; consult support before optional APIs. Portable apps feature-gate them and provide fallback behavior.
4. List and create real project files: conventional entrypoint, optional policy/metadata `app.json`, shaders, and project-relative assets. JSON cannot embed or generate virtual source files. No package files/browser pipeline.
5. Use the smallest architecture below; split only distinct state, simulation, rendering, I/O, or loading.
6. Use exact documented signatures/recipes; never guess absent symbols. Prefer `sys.canvas` for 2D, adding `sys.gl` only when needed.
7. Run `budo run <project>` (`--watch` while iterating); fix the first runtime/shader error before adding features.
8. Verify resize, pointer/keyboard, unavailable capabilities, target caveats, relative paths, frame scheduling, and resource cleanup.

Output discipline for generated apps:

- Generate runnable source, descriptive names, and grouped constants—not pseudocode. The only Budo JS runtime globals are `sys`, `console.log`, and global `fetch`; standard JS objects remain available. Managed entrypoints execute directly: there is no `budo`, `bframe`, or `bsys` global/package; no `budo.run()`, `app` object, event-emitter lifecycle, `createPipeline()`, or direct WebGL context. Never use DOM/browser canvas (`document`, HTML canvas/2D context), Node/npm, p5.js, Love2D, Unity, React, engine APIs, package files, bundlers, or transpilation; Budo strips `main.ts`.
- Do not claim an API is asynchronous unless documented. Global JS `fetch()` returns a promise; most other calls are immediate.
- Runtime dimensions, pointer/canvas coordinates, and shader resolution are physical pixels; never multiply them by density or assume CSS pixels. Use `getDisplayDensity()` for all authored UI sizes/offsets (fonts, margins, baselines, strokes, hit targets), not runtime-provided values.
- Create typed-array scratch storage, matrices, buffers, vertex layouts, render targets, paths, shaders, textures, databases, audio nodes, and models once; resize only size-dependent resources. Bound producer work and collections from input/callbacks/network/generated data: cap samples per frame and cap/expire/remove/reuse entries.
- Separate simulation/rendering and keep units explicit: seconds (`age += dt`), units/s (`position += velocity*dt`), units/s² (`velocity += acceleration*dt`), degrees/s (`angle += angularSpeed*dt`). Integrate continuous rates with `dt`; apply one-frame event impulses once, without `dt`.
- Clamp stalls, e.g. `dt = Math.min(Math.max(input.deltaTime,0),0.05)`. Damping uses `Math.pow(retentionAt60Hz,dt*60)`, not a fixed per-frame multiplier.
- Edges (`pointer.pressed`, `mouse.leftPressed/rightPressed`, `isKeyPressed`) last one frame; held state (`pointer.down`, `mouse.left/right/middle`, `isKeyDown`) persists. Use edges once, held state continuously, and let a zero-initialized time gate accept its first event.
- Zero is a valid runtime value: never use `pointer.x || fallback`; test state/presence explicitly. Portable primary input uses `input.pointer`; multitouch uses `input.pointers`; reserve `input.mouse` for optional right/middle/wheel behavior and provide touch/keyboard alternatives.
- Density-scale authored hit targets independently of visuals; make them large and separated enough for touch.
- `input.deltaTime` and `input.totalTime` are seconds; animation callback timestamps and `sys.timer` delays are milliseconds. Do not compare or combine them without converting units.
- End every JS/Lua frame callback with `sys.animation.requestFrame(frame)` unless intentionally stopping.

## Architecture chooser

- **Simple 2D**: `main.ts` constants/state + `update(input,dt)` + `render()` + `frame()`.
- **App UI** (forms, settings, tools, editors): `budo init <dir> --template ui`, then build on the budo-ui widgets in `ui/` (see UI toolkit) instead of hand-drawing controls.
- **Medium 2D**: `main.ts` orchestration; `model.ts` deterministic state; `view.ts` canvas; `io.ts` DB/file/network/audio.
- **Canvas + shader**: `main.ts` transparent Skia UI → flush → fullscreen shader; vertex/fragment files generate/post-process/composite.
- **3D**: `main.ts` resources/update/ordered passes; math/scene modules; explicit mesh/fullscreen shaders.
- **Data/network**: least-privilege `app.json`; nonblocking loading/error/ready state; parameterized SQLite or sandboxed-file storage module.

Architecture invariants:

- Frame order: input → update → canvas → flush if sampled → GL → next frame. Canvas/GL are immediate; set state directly before its draw.
- Dimensions are dynamic: detect changes, recompute layout/targets, and clamp or proportionally remap affected positions without resetting unrelated state. Keep rectangles nonnegative/in-bounds; measure, split, reposition, shorten, or omit text/content in small windows—canvas never auto-fits/wraps.
- Separate capability policy from feature logic: probe once, store availability, and route to enabled/disabled behavior.
- Use SQLite for structured persistent state, files for import/export/assets, and network only for remote data. Do not emulate one with another.
- For a native C app, use `budo init <dir> --language c --template canvas|gpu` and `budo compile`; native projects are trusted, compiled, and architecturally separate from managed `main.*` apps.

Canvas GUI invariants:

- Canvas primitives are not widgets: no DOM/control tree or built-in form, button, list, selector, focus, layout, clipping, or accessibility. Own widget state, layout/draw/hit order, capture/focus, text sessions, scrolling/selection, visual states, and cleanup.
- Paint is one stateful object: `setFillColor()` selects fill mode; `setStrokeColor()` selects stroke mode; alpha/style persist. To fill **and** outline a shape, draw it twice (fill, then stroke). Set explicit contrasting style per layer and restore alpha 255. Use one authoritative rectangle set for drawing, hits, clips, and IME caret; never reconstruct it independently.
- JS pointer has `pressed`/`down`, no `released`. Capture on press, drag only the capture while down, derive release via `wasDown && !down`, and activate only when released inside; press activation breaks cancellation/scroll intent.
- Lists: `save()` → `clipRect()` → visible rows → `restore()`; unmatched state leaks. Re-clamp scroll after size/content changes and distinguish tap from drag with a movement threshold.
- Text fields have no automatic clip/scroll/select/wrap/caret. Clip inner content; keep the caret visible with horizontal/vertical scroll; map pointer x to UTF-16 boundaries via measured prefixes; normalize reversed selections; cap every edit path. For long documents, cache line-start offsets/metrics and draw visible lines only—canvas measurement is immediate.

## Agent rules

- Budo is host-driven, not the web application model; web exports preserve it. Use injected `sys`, inventing no browser/Node APIs beyond documented `fetch`, `console.log`, promises/modules, and web caveats.
- Prefer JS/TS unless user asks for Lua/WASM. `main.ts` is stripped at load time and run by QuickJS; no build step needed.
- Entrypoint resolution: JS family prefers `main.ts` over `main.js`; WASM family prefers `main.wasm` over `main.wat`. A directory containing more than one family (JS/TS, Lua, WASM) is rejected as ambiguous.
- Managed JS has no `onInit`, `onExit`, `onFrame`, or `onPointer` registration API. Top-level code is initialization; interaction is polled in the callback registered with `sys.animation.requestFrame(frame)`. `requestFrame` stores one persistent callback (unlike one-shot RAF); another replaces it and `cancelFrame()` clears it. Exceptions are logged without clearing it, so frame errors repeat until stop/reload/cancel. Without a registered frame callback, an app has no update/render loop.
- Use `sys.window.getWidth()/getHeight()/getDisplayDensity()`, `sys.input.get()`, and SDL scancodes for `isKeyDown/isKeyPressed`.
- GL operations are immediate. Default-`u_canvas` fullscreen/region draws auto-flush Skia on desktop/web/Android. Passing `CanvasTexture` to the JS wrapper flushes it.
- Canvas+shader compositing: clear Skia to transparent (`sys.canvas.clear('#00000000')`), draw UI, then a fullscreen shader samples built-in `u_canvas` and composites it over generated content. The shader is what reaches the screen. See "Canvas + shader compositing" recipe.
- Asset paths are project-relative. Absolute paths and `..` traversal are rejected by file/audio/neural/SVG/font/texture APIs.
- Use `app.json` for network, filesystem, neural, orientation, and packaging metadata.
- Gate optional `neural|midi|udp|http|sensors` via `sys.capabilities.*.available`; for older web wiring use e.g. `sys.capabilities?.udp?.available === true`.
- Author app GLSL as GLSL ES 300: `#version 300 es`. Desktop translates to GLSL 150.
- Examples show architecture; `budo.d.ts` is the JS/TS signature authority. `examples/budo-ui` is the UI toolkit: immediate-mode widgets with motion, keyboard focus, accessibility, authoritative edits, IME/candidate placement, and the Android keyboard lifecycle.

## Project, CLI, metadata

Run formats:

```text
budo run <project_dir|file> [--width n --height n --title s --fullscreen --no-vsync --watch --file-root path]
budo run --from-input js|ts|lua|wat|wasm [options]
budo <project_dir|file>                 # shorthand for run
budo compile <project_dir|file> [--build-dir DIR --sdk DIR --offline --run --debug|--release --sanitize address,undefined]
budo cache native|sdk inspect [--json]
budo cache native|sdk clean --all
budo cache android inspect [--json]     # per-app Android build directories (Budo Pro)
budo cache android clean --all|<package>
budo init <project_dir> [--template ui | --language c --template canvas|gpu]
budo web-serve <project_dir>
budo web-export <project_dir> [-o DIR]
budo android-apk <project_dir> [--release|--debug -o DIR --install --no-build --clean]
budo android-aab <project_dir> [--release|--debug -o DIR --no-build --clean]
budo budo.d.ts
budo budo-llm.md
budo version                            # also --version, -v; include the output in bug reports
budo help
```
A release APK without a `signing` block in app.json is signed with the Android debug key (installable for testing, never publish it); release AAB requires the release key. app.json `permissions` lists Android permissions, e.g. `"RECORD_AUDIO"` for `sys.audio.openInput`.

Android APK/AAB packaging is a Budo Pro feature. The released Budo binaries
include it for free during the public launch (a later release makes it paid).
Builds from source without the private Android feature pack under
`private/android` keep the commands, which then report packaging as unavailable.

Single `.js/.ts/.lua/.wat/.wasm` files are staged as matching `main.*` in a temporary virtual app; project-relative assets and `app.json` are absent unless using a real project dir. `--from-input` stages stdin similarly.

`app.json` optional fields:

```json
{
  "name": "My App",
  "author": "Name",
  "date": "2026-05-03",
  "version": "1.0",
  "orientation": "portrait",
  "network": "*",
  "filesystem": true,
  "neural": true
}
```

The metadata filename and shape are exact: use project-root `app.json`, not `project.json`, manifests, `entryPoint`, `permissions`, or schema IDs. Entrypoints remain conventional `main.ts|main.js|main.lua|main.wasm|main.wat`; SQLite needs no metadata permission. Network allowlists belong directly in `app.json` as `"network":"api.example.com"` (or array/`"*"`).

Each entrypoint/shader/module/asset must physically exist at its project-relative path; `app.json` does not contain a `files`, `entry`, or embedded-source tree. Budo does not interpret CSS, HTML elements, native `confirm()` dialogs, or style strings. Build confirmation dialogs, selectors, sliders, and toolbars as canvas state using the GUI invariants above (the only system dialogs documented are explicit file picker/save flows).

Defaults: `name` directory name, `author` `Unknown`, `version` `1.0`, `date` empty, orientation unspecified. `orientation`: `portrait|landscape|unspecified`. `network`: absent/null disables HTTP; `"*"` allows all; string comma-list or array allows exact/suffix domains. `filesystem:true`: Android external shared storage permission; desktop file access always works within project dir or `--file-root`. `neural:true`: opt in to ONNX inference support/packaging.

`version` is canonical. Equal `version_name` is accepted as a compatibility alias; conflicting `version` and `version_name` invalidate metadata. Do not use deprecated `app_name`.

## JavaScript/TypeScript runtime

QuickJS globals: `sys`, `console.log`, global `fetch`. ES module syntax is auto-detected. Relative module specifiers resolve from the importing file; bare specifiers resolve from the project root; `.js` then `.ts` may be appended; absolute and escaping paths are rejected. TypeScript modules are stripped before evaluation. Runtime globals are visible in modules.

Input is polled from `sys.input.get()` inside the Budo frame callback; there is no `addEventListener('click'|'keydown',...)`. Keep scheduling frames while a fetch is pending so UI/loading state and main-thread promise jobs continue to progress. Draw only through `sys.canvas`; it has no HTML canvas/context/font/fillStyle API.

### TypeScript stripper limitations

`main.ts` uses Budo's whitespace-preserving heuristic stripper—not TypeScript compilation/code generation. It handles common annotations, interfaces, type aliases, generics, `as`, non-null assertions, access modifiers, `implements`, and `declare`. These remain unsupported and cause QuickJS parse errors:

```text
enum, namespace/module blocks, decorators, satisfies
```

It may blank legal value syntax as a type, yielding invalid/altered JS. Observed risks:

- Ternaries in calls/assignments with call/object branches: `setWidth(active ? dp(2) : dp(1))`.
- Typed parameter lists split across lines.
- Dense typed functions mixing `<`, `>`, equality, `.length`, ternaries, or nested objects/arrays; comparisons such as `distance < closestDistance` may look generic.
- Compact guard/ternary combinations: `if (end <= start) return value >= end ? 1 : 0`.

Prefer boring, explicit value-level statements:

```ts
let borderWidth = dp(1);
if (active) borderWidth = dp(2);
sys.canvas.setStrokeWidth(borderWidth);
function drawPanel(x: number, y: number, pointer: PointerHit): void { /* simple steps */ }
```

On altered behavior/lost mutation/`Unexpected token`/`Unexpected end of input`, inspect Budo's stripped JS before runtime logic. Editor diagnostics are insufficient; validate with the same stripper plus JS syntax, and treat `budo run <project>` as authoritative. Optional local helper:

```text
/tmp/budo-ts-strip path/to/main.ts | node --input-type=module --check
```

For portable code, keep TS to declarations/simple annotations, split complex expressions into intermediates, or use `main.js` when advanced TS matters.

Namespaces:

```text
sys.canvas, paint, transform, path, svg, font, graphics, gl, math, input, window,
animation, timer, audio, midi, db, files, assets, network, udp, magneto, device,
capabilities, neural
```

Colors: JS/Lua accept `"#RRGGBB"`, `"#RRGGBBAA"`, numeric ARGB `0xAARRGGBB`. The alpha byte is **last** in a string but **first** in a numeric color. Examples: translucent white is `"#FFFFFF80"` or `0x80FFFFFF`; translucent black is `"#00000080"` or `0x80000000`. Do not write an ARGB-looking string such as `"#80FFFFFF"` expecting alpha `0x80`—Budo parses that string as red `0x80`, green/blue `0xFF`, alpha `0xFF`. WASM uses numeric ARGB only.

### Window, input, animation, timers

```js
sys.window.getWidth(); sys.window.getHeight(); sys.window.getDisplayDensity();
sys.animation.requestFrame(callback); sys.animation.cancelFrame(handle);
sys.animation.waitForInput(callback, timeoutMs?); // like requestFrame, but runs on the next input/resize/timeout
sys.timer.once(delayMs, callback); sys.timer.every(intervalMs, callback); sys.timer.clear(id);
sys.input.get(); sys.input.isKeyDown(scancode); sys.input.isKeyPressed(scancode);
```

Battery: when nothing animates, end the frame with `sys.animation.waitForInput(frame, timeoutMs?)` instead of `requestFrame(frame)`. The last frame stays on screen, desktop sleeps on events and Android stops rendering until input, a resize, the timeout, or the next JS timer. Apps that draw to the screen with `sys.gl` passes after the canvas (post effects) must keep calling `requestFrame` while those passes run.

`sys.input.get()` creates a fresh aggregate JS object; call it once per frame and pass that snapshot through update/render. Its `focused` field means **host window focus**, not editor/widget focus; track widget focus separately and use window focus only to pause/blur as intended.

Window-less apps: the window (web canvas, Android surface) opens only on the first graphics call (`sys.canvas`, `sys.gl`, `sys.window`, `sys.animation`, `sys.input`, ...), even from a later timer. An app that never uses graphics shows nothing and ends once no timers, promise jobs, fetches, UDP/MIDI listeners, file pickers, or llama.cpp requests remain. `sys.exit(code = 0)` ends any app at once with that status (`budo run` exit status; web dispatches a `budoexit` event with `detail.code`). Load errors exit with 1.

Width/height, pointer, canvas, and `u_resolution` are matching physical pixels; density scales only authored constants (e.g. `margin=16*density`, `fontSize=18*density`, `hitRadius=Math.max(visualRadius,22*density)`). On resize, recompute UI, resize targets, and clamp or preserve relative placement (`x*=newWidth/oldWidth`) without recreating unrelated state. Measure text; no automatic layout/wrap.

`sys.input.get()` shape:

```js
{
  pointer:{id,x,y,dx,dy,down,pressed,type:'mouse'|'touch'},
  pointers:[...],
  mouse:{x,y,dx,dy,wheelX,wheelY,left,middle,right,leftPressed,rightPressed},
  keyboard:{shift,ctrl,alt,meta},
  text:'layout-aware text committed since the previous frame',
  textEdit:null|{text,selectionStart,selectionEnd},
  composition:{active,changed,text,selectionStart,selectionEnd},
  textInputActive:boolean,
  nativeTextEditing:boolean, // web/Android edit text sessions themselves (paste, selection): do not handle editing shortcuts
  deltaTime,totalTime,frameCount,focused
}
```

```js
function frame(timestampMs) {
  const input = sys.input.get();
  const dt = Math.min(Math.max(input.deltaTime, 0), 0.05);
  update(input, dt); render();
  sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
```

Edges reset each frame; held states persist (see output discipline). `pointer` is primary mouse/touch, `pointers` active multitouch, `mouse` desktop-specific. For cooldowns, store second-based deadlines and initialize `nextAllowed=0` so `totalTime>=nextAllowed` accepts the first event.

`input.text` is layout-aware committed text. Full editors use `sys.input.startTextInput({text,selectionStart,selectionEnd,multiline?})`; each frame apply authoritative `textEdit`, render persistent composition, then `sys.input.updateTextInput({text,selectionStart,selectionEnd,caret:{x,y,width,height}})`; call `sys.input.stopTextInput()` on blur. Selections are UTF-16; caret geometry is physical pixels. Desktop/web/Android support composition; web/Android add native replacement, paste/autocorrection, and soft keyboards. Android Back and single-line Done/Return finalize composition into `textEdit`; stale pre-commit snapshots are rejected. Physical controls/navigation use scancodes (QWERTY positions); letter shortcuts match `input.text` (when no text session is active) so they follow the keyboard layout. Timers <=0 run next frame; `every` minimum is 1 ms.

Text-session rules for canvas forms:

- Start only on focus entry: restarting replaces platform model/selection and clears composition. Stop on blur/submit. Methods are only under `sys.input`; start/update return acceptance booleans.
- Route `text|textEdit|composition` only to the focused editor. `textEdit` replaces the full model/UTF-16 selection before other input; `text` is the desktop/simple committed path. Normalize selection bounds for insertion/rendering and enforce document limits on `textEdit`, committed text, and programmatic edits, then clamp the resulting selection.
- Composition is transient, not persistent until commit. Draw the preedit text itself (an underline alone is insufficient) at persistent selection; visual/IME caret follows `composition.text.slice(0,composition.selectionEnd)`. `updateTextInput()` receives persistent model/selection but composed-caret geometry.
- After edits and final responsive layout/scroll, call `updateTextInput()` every focused frame with synchronized physical caret geometry; do not swallow failures and let models diverge.

Desktop multiline editors must handle non-text physical keys themselves; `input.text` supplies committed characters, not editing/navigation. Common SDL scancodes: Return 40, Backspace 42, Tab 43, Home 74, PageUp 75, Delete 76, End 77, PageDown 78, Right 79, Left 80, Down 81, Up 82, A 4. `isKeyPressed()` is edge-only; implement held-key repeat explicitly with `isKeyDown()` plus second-based timing. Use `input.keyboard.shift/ctrl/meta` for selection and shortcuts.

### Canvas, paint, transform, path

```js
sys.canvas.clear(color?); sys.canvas.readPixels(); // {width,height,pixels:ArrayBuffer}, RGBA8, stalls
sys.canvas.drawRect(x,y,w,h); sys.canvas.drawRoundRect(x,y,w,h,rx,ry);
sys.canvas.drawCircle(cx,cy,r); sys.canvas.drawOval(x,y,w,h);
sys.canvas.drawLine(x1,y1,x2,y2); sys.canvas.drawPoint(x,y);
sys.canvas.drawText(text,x,y,fontSize=32); sys.canvas.measureText(text,fontSize=32); // y is the text BASELINE
sys.canvas.measureTextRect(text,fontSize=32); sys.canvas.drawArc(x,y,w,h,startDeg,sweepDeg,useCenter=false);
// measureText -> advance width (number). measureTextRect -> {width, height}: width is advance width,
// height is the tight glyph bounding-box height. To vertically center text at cy: baselineY = cy + height/2.
// JS CanvasTexture.canvas.drawText uses the same default font size of 32.

sys.canvas.setFillColor(color); sys.canvas.setStrokeColor(color); sys.canvas.setStrokeWidth(width);
sys.canvas.setAntiAlias(bool); sys.canvas.setAlpha(0..255);
sys.canvas.setStrokeCap('butt'|'round'|'square'); sys.canvas.setStrokeJoin('miter'|'round'|'bevel');
sys.canvas.setBlendMode(mode); sys.canvas.setImageFilter(name,...args); sys.canvas.setColorFilter(name,...args);

sys.canvas.save(); sys.canvas.restore(); sys.canvas.translate(dx,dy);
sys.canvas.rotate(deg); sys.canvas.rotate(deg,px,py); sys.canvas.scale(sx,sy);
sys.canvas.skew(sx,sy); sys.canvas.reset(); sys.canvas.clipRect(x,y,w,h);
sys.canvas.clipRoundRect(x,y,w,h,rx,ry=rx); sys.canvas.clipPath(path); // anti-aliased
sys.canvas.saveLayer(alpha=255, x?,y?,w?,h?, backdropBlur=0); // ... draw ...; sys.canvas.restore();

sys.canvas.setGradient('linear',x0,y0,x1,y1,colors,stops?); sys.canvas.setGradient('radial',cx,cy,r,colors,stops?);
sys.canvas.setGradient('sweep',cx,cy,colors,stops?); sys.canvas.setGradient(null); // clear

sys.canvas.drawParagraph(text,x,y,width,fontSize=32,{align:'left'|'center'|'right',lineHeight:1.25,maxLines:0}); // y is the TOP
sys.canvas.measureParagraph(text,width,fontSize=32,{lineHeight,maxLines}); // both -> {width,height,lines}
sys.canvas.drawRichText(['plain ', {text:'bold', size:24, color:'#C00', font:'Heavy'}], x, y, width, {size:16, align, lineHeight, maxLines});
sys.canvas.measureRichText(spans, width, options); // mixed sizes/colors/fonts, baselines aligned, ≤64 spans

const path = sys.path.create(); sys.path.reset(path); sys.path.moveTo(path,x,y); sys.path.lineTo(path,x,y);
sys.path.quadTo(path,x1,y1,x2,y2); sys.path.cubicTo(path,x1,y1,x2,y2,x3,y3);
sys.path.close(path); sys.path.addRect(path,x,y,w,h); sys.path.addCircle(path,cx,cy,r); sys.canvas.drawPath(path);
sys.path.addSvg(path, 'M5 12h14M12 5v14'); // SVG `d` data, all commands; returns false (path unchanged) if it does not parse
```

Advanced paint: blend modes `src-over|src|dst-over|dst-in|dst-out|src-in|src-out|clear|plus|multiply|screen|overlay|darken|lighten|color-dodge|color-burn|hard-light|soft-light|difference|exclusion|hue|saturation|color|luminosity`. Image filters: `setImageFilter('blur',sigmaX,sigmaY?)`, `setImageFilter('drop-shadow',dx,dy,sigmaX,sigmaY?,color)`, `setImageFilter('drop-shadow-only',dx,dy,sigmaX,sigmaY?,color)`, `setImageFilter('none'|null|undefined)`. Color filters: `setColorFilter('matrix',array20)`, `setColorFilter('blend',color,blendMode)`, `setColorFilter('none'|null|undefined)`. Unknown names throw `RangeError`; missing required args throw `TypeError`.

Gradients: `colors` is an array of 2–16 colors, `stops` an optional array of one 0..1 position per color (default evenly spaced). Colors interpolate in OKLab. A gradient replaces the paint color for fills, strokes, and text, makes the paint opaque (`setAlpha` then fades it), and stays until `setGradient(null)` or the next `setFillColor`/`setStrokeColor`. Sweep starts at the positive x axis.

Layers: `saveLayer(alpha)` groups what follows and composites it on `restore()` with one opacity, so overlapping shapes do not show through each other (true group fade-out). With a rectangle, the layer is clipped to it. `backdropBlur > 0` starts the layer with a blurred copy of what is already drawn below: frosted glass. Typical panel: `save(); clipRoundRect(x,y,w,h,r); saveLayer(255,x,y,w,h,20); setFillColor('#FFFFFF80'); drawRect(x,y,w,h); restore(); restore();`. Layers cost an offscreen pass; keep them to panels, not every row.

Paragraphs: lines break at spaces and `\n`; words wider than the width break between characters; `width` 0 disables wrapping. `maxLines` ends the last line with an ellipsis. Lines are vertically centered in `fontSize*lineHeight`, so a paragraph occupies exactly `height`. Use `measureParagraph` for layout, then `drawParagraph` at the same width. Single-style text only (no mixed fonts/colors inside one paragraph).

### SVG, fonts, graphics textures

```js
const svg = sys.svg.load('icon.svg'); const svg2 = sys.svg.loadFromBuffer(buffer);
sys.canvas.drawSvg(svg,x,y,w,h); sys.svg.destroy(svg); sys.svg.getWidth(svg); sys.svg.getHeight(svg);
sys.font.load(path, name=path); sys.font.loadFromBuffer(buffer, name='buffer'); sys.canvas.setFont(name); sys.canvas.setFont(null); // null (Lua nil) = default font

const surface = sys.graphics.createCanvasTexture(w,h); // JS only
surface.canvas.clear('#00000000'); surface.canvas.drawRect(x,y,w,h); surface.canvas.drawCircle(cx,cy,r);
surface.canvas.drawText(text,x,y,size); surface.resize(w,h); surface.destroy();
// CanvasTexture fields: id,width,height,texture,target,canvas
```

SVG `load*` returns id >= 0 or `-1`. `CanvasTexture` is Skia canvas plus GL texture/FBO; use for UI/text/vector content sampled by shaders.

### GL and shaders

Budo writes built-ins immediately before each draw: `uniform sampler2D u_canvas`=unit 0, `uniform vec2 u_resolution`=active viewport/target physical size, `uniform float u_time`=frame seconds; region draws add `uniform vec2 u_offset`. Do not call app setters for these names—the draw overwrites them; use distinct custom uniforms. App samplers use units 1..7. Fullscreen attributes: `in vec2 a_position`, `in vec2 a_texCoord`.

Canvas/pointer pixels are top-left, and so is fullscreen/region `a_texCoord`/`v_uv` ((0,0) = top-left): sample `texture(u_canvas, v_uv)` directly (upright; canvas textures and render targets drawn by these passes too), and compare with input via `vec2 fragPx = v_uv * u_resolution`. Only `gl_FragCoord` is bottom-left: `vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y)` converts it. Convert one side once, never mix origins. `u_canvas` is premultiplied RGBA: over opaque `bg`, output `vec4(ui.rgb+bg*(1.0-ui.a),1.0)`, not `mix(bg,ui.rgb,ui.a)` (which applies alpha twice).

Programs are numeric handles; every operation is a `sys.gl` function taking the handle first (there is no program object: `createShaderProgram` does not exist). Setters and draws bind the program themselves, so `useProgram` is optional.

Program creation throws on compile/link failure; after draws, `getLastError()` returns the last error or `""`. GLSL removes uniforms not read by the shader body: built-in assignment silently skips them, but explicit `uniform*` throws `InternalError: Uniform not found`. Remove the declaration+setter or use the value; inside `frame()`, this repeats because animation callbacks survive exceptions.

```js
const p = sys.gl.createProgram('shader.vert','shader.frag');
const p2 = sys.gl.createProgramFromBuffer(vertexBytes, fragmentBytes);
sys.gl.destroyProgram(p); sys.gl.useProgram(p); sys.gl.bindScreen(); sys.gl.bindRenderTarget(rt?);
sys.gl.bindScreen(); // alias of sys.gl.bindScreen(): bind default framebuffer + window-size viewport
sys.gl.drawFullscreen(p); sys.gl.drawFullscreenImmediate(shaderOrId, sourceTexture?);
sys.gl.drawRegion(p,x,y,w,h,targetId?); sys.gl.drawRegionImmediate(shaderOrId,x,y,w,h,targetId?);
sys.gl.setUniform1f(p,'u_strength',0.5); sys.gl.bindTexture2D(p,'u_tex',tex,1); sys.gl.drawFullscreen(p);
sys.gl.drawFullscreenImmediate(p, textureOrCanvasTexture); // samples that source as u_canvas for this pass

sys.gl.setUniform1i(p,name,i); sys.gl.setUniform1f(p,name,f); sys.gl.setUniform2f(p,name,x,y);
sys.gl.setUniform3f(p,name,x,y,z); sys.gl.setUniform4f(p,name,x,y,z,w);
sys.gl.setUniformMatrix3(p,name,mat9); sys.gl.setUniformMatrix4(p,name,mat16);
sys.gl.setUniform1fv(p,name,f32); sys.gl.setUniform2fv(p,name,f32); sys.gl.setUniform3fv(p,name,f32); sys.gl.setUniform4fv(p,name,f32);
sys.gl.setUniform1iv(p,name,i32); sys.gl.getLastError(); sys.gl.getProjectDir();

const rt = sys.gl.createRenderTarget(w,h,depth=false); sys.gl.resizeRenderTarget(rt,w,h);
sys.gl.bindTexture(program,'u_prev',rt,1); sys.gl.destroyRenderTarget(rt);
sys.gl.bindCanvasTexture(program,'u_ui',surface,1); // flushes the surface's pending drawing first
```

A render target exposes its color texture through the target ID. Sample it with `sys.gl.bindTexture(program,name,targetId,unit)`; `bindTexture2D` is for ordinary texture IDs and `bindCanvasTexture` for CanvasTextures, not render-target IDs. `depth=true` adds an internal depth renderbuffer for testing, not a sampleable depth texture/`target.depth`; encode linear depth into a color channel for depth post-processing. Shaders come from project-relative files or ArrayBuffer-backed `create*FromBuffer`; there is no inline pipeline-description API.

Render-target color persists across frames and Budo exposes no `sys.gl.clear`; clear it with a fullscreen/region pass whose fragment shader outputs the clear color, e.g. `sys.gl.drawRegion(clearProgram,0,0,w,h,targetId)`. Create/resize targets only with positive dimensions. `drawMesh` without `target` draws into the render target selected by `sys.gl.bindRenderTarget(rt)`, otherwise to the screen; `target: -1` forces the screen. An offscreen depth target's depth is cleared by the frame's first depth-tested mesh draw into it, like the screen's, so the mesh draws of one frame occlude one another (draw a whole 3D scene into a target, then post-process it).

Multi-pass order is explicit: draw source texture → target A; bind A to pass B with `sys.gl.bindTexture`; draw B → target B; draw/flush transparent canvas; bind screen; final shader samples target B plus built-in `u_canvas`. Before/after mode should feed the chosen source/effect texture into the same final HUD compositor, not bypass it. Resize existing render targets with `resizeRenderTarget()`; do not destroy/recreate unchanged programs/source textures on every window resize. A fullscreen pass overwrites every target pixel, so no separate color-clear pass is needed when that pass is guaranteed to cover the full target.

3D:

```js
const vbo = sys.gl.createBuffer('vertex', floatArray, 'static'|'dynamic'|'stream');
const ibo = sys.gl.createBuffer('index', indexArray);
sys.gl.updateBuffer(vbo, data, byteOffset=0); sys.gl.destroyBuffer(vbo);

const tex = sys.gl.loadTexture2D('image.png'); const tex2 = sys.gl.loadTexture2DFromBuffer(bytes);
const tex3 = sys.gl.createTexture2D(w,h,'rgba8'|'rgb8'|'r8', pixels?);
const cube = sys.gl.loadTextureCube([px,nx,py,ny,pz,nz]); const cube2 = sys.gl.loadTextureCubeFromBuffer([px,nx,py,ny,pz,nz]);
sys.gl.updateTexture2D(tex,x,y,w,h,pixels); sys.gl.destroyTexture(tex); // 'r8' is one channel (red)

const layout = sys.gl.createVertexLayout();
sys.gl.setAttribute(layout, location, buffer, size, 'float'|'byte'|'ubyte'|'short'|'ushort'|'int'|'uint', normalized, stride, offset, divisor?); // other names silently mean 'float'
sys.gl.setIndexBuffer(layout, ibo, 'u16'|'u32'); sys.gl.getAttribLocation(program,name); sys.gl.destroyVertexLayout(layout);
sys.gl.bindTexture2D(program,'u_albedo',tex,1); sys.gl.bindTextureCube(program,'u_env',cube,2);
sys.gl.drawMesh(program, layout, {mode:'triangles', first:0, count, target, depthTest:true, depthWrite:true, cull:'back', blend:'alpha'});
sys.gl.drawMeshImmediate(program, layout, options);
```

Instancing: give per-instance attributes `divisor` 1 in `setAttribute` and pass `instanceCount` in the `drawMesh` options (there is no `drawMeshInstanced`). Otherwise reuse persistent layouts with bounded `drawMesh` calls, or update one preallocated combined vertex buffer and draw once. Never allocate combined arrays in the frame loop.

Dynamic combined meshes trade draw-call count for QuickJS CPU work and upload bandwidth: keep object/vertex counts bounded and prefer rigid per-object draws when batching would require large per-frame CPU transforms. Post-process taps offset from the base UV must clamp UVs and account for each sampled pixel's occupancy/depth mask; otherwise cleared background samples bleed dark/color fringes around silhouettes.

GPU handles are bounded integer IDs, not mutable objects. Cross-platform limits are 128 programs, 32 render targets, 256 buffers, 128 textures, 64 vertex layouts, and 8 attributes/layout. Create one persistent layout per distinct buffer/index configuration during initialization, store its numeric ID, and destroy replaced layouts/resources; never create layouts per frame or assign properties such as `layout.id`. Exceeding a pool throws (for example `Maximum number of vertex layouts reached`).

Uniforms default to zero and persist on a program. Before every body draw, set every changing uniform used by that shader (`u_model`, `u_mvp`, material/color, camera/light data), then draw with that body's persistent layout. Omitting `u_model` can collapse world-space positions/normals even if `u_mvp` still places geometry; omitting color commonly yields transparent/black output.

Draw options: `mode triangles|triangle_strip|triangle_fan|lines|line_strip|points`; `count` required; `first`, `target`, `instanceCount` (default 1) optional; `depthTest` true and `depthWrite` true by default; `cull none|back|front` (default none); `blend alpha|premult|add|none` (default alpha).

Standard mesh attribute locations: 0 `a_position` vec3, 1 `a_uv` vec2, 2 `a_normal` vec3, 3 `a_color` vec3/vec4, 4 `a_tangent` vec3.

On the first mesh draw to the screen, Budo flushes/blits the default canvas as the screen background, clears screen depth, then draws meshes over it. Canvas content drawn first is therefore **behind** 3D, not a true HUD overlay. For UI over 3D, render 3D to a depth render target, then fullscreen-composite that target with transparent `u_canvas`. Lighting requires shader math plus normals at location 2; hard-edged cubes need per-face vertices/normals, not eight shared corner positions.

Preallocate projection/view/model/MVP and vec3 scratch arrays; recompute projection only when nonzero dimensions change. Pointer deltas are physical pixels, so use `dx/density` for density-independent orbit sensitivity. Keep lighting spaces consistent: if fragments use world positions, pass world-space camera position for view direction (`-worldPos` assumes the camera is at the origin); animated/deformed positions also require matching normals or lighting visibly diverges.

### Math

Caller-owned Float32Array helpers; mat4 is column-major, angles/FOV are radians, quaternions `(x,y,z,w)`. Matrix transforms require both output and source arrays—do not omit the source or pass scalar eye/target/up components.

```text
mat4Identity(out); mat4Multiply(out,a,b); mat4Perspective(out,fovyRad,aspect,near,far);
mat4Ortho(out,left,right,bottom,top,near,far); mat4LookAt(out,eye3,target3,up3);
mat4Translate(out,a,v3); mat4RotateX(out,a,angleRad); mat4RotateY(out,a,angleRad); mat4RotateZ(out,a,angleRad); mat4Scale(out,a,v3);
mat4Invert(out,a)->boolean; mat4Transpose(out,a).
vec3Add(out,a,b); vec3Sub(out,a,b); vec3Cross(out,a,b); vec3Scale(out,a,s); vec3Normalize(out,a);
vec3Dot(a,b)->number; vec3Length(a)->number; vec3TransformMat4(out,v3,m);
vec3NormalizeMany(out,values,count); vec3TransformMat4Many(out,values,m,count).
quatFromAxisAngle(out,axis3,angleRad); quatMultiply(out,a,b); quatSlerp(out,a,b,t); quatToMat4(out,q).
```

### Audio

```js
sys.audio.SINE=0; SQUARE=1; SAWTOOTH=2; TRIANGLE=3; NOISE=4;
sys.audio.start(); sys.audio.stop(); sys.audio.isPlaying(); sys.audio.setMasterGain(0..1); sys.audio.getMasterGain();
const osc = sys.audio.createOscillator(); sys.audio.destroyOscillator(osc);
sys.audio.setOscillatorType(osc, number|'sine'|'square'|'sawtooth'|'triangle'|'noise');
sys.audio.setOscillatorFrequency(osc,hz); sys.audio.setOscillatorGain(osc,0..1); sys.audio.setOscillatorDetune(osc,cents);
sys.audio.startOscillator(osc); sys.audio.stopOscillator(osc);
sys.audio.setOscillatorEnvelope(osc, attackSec, decaySec, sustain0to1, releaseSec); sys.audio.noteOn(osc); sys.audio.noteOff(osc);
const b = sys.audio.createBuffer(sampleRate, channels1or2, numSamples);
const b2 = sys.audio.loadBuffer('hit.ogg'); const b3 = sys.audio.loadBufferFromBuffer(arrayBuffer); // WAV/MP3/Ogg/FLAC
sys.audio.setBufferData(b, samplesMinus1To1, offset=0); sys.audio.destroyBuffer(b);
const play = sys.audio.playBuffer(b, loop=false, gain=1); sys.audio.stopBuffer(play);
sys.audio.midiToFreq(note); sys.audio.getError();
// Streams: Budo calls back to fill output chunks / hand over captured ones (Float32Array, interleaved, info.frames*info.channels,
// info: {frames:256, channels, sampleRate, time, latency, underruns|overruns}); outputs play exactly what is written (no master gain).
const out = sys.audio.openOutput({channels:2}, (buffer, info) => { /* fill buffer */ }); sys.audio.closeOutput(out); // -1 on failure; latencyMs:N fixes the queue, omitted = adaptive
const mic = sys.audio.openInput({channels:1}, (samples, info) => { /* copy what you keep */ }); sys.audio.closeInput(mic);
sys.audio.getSampleRate(); // the device rate streams run at
// Decoders: WAV/MP3/Ogg/FLAC to float frames a block at a time (any size); path (assets/|files/, seekable) or bytes; push mode for byte sources.
const dec = sys.audio.openDecoder('assets/song.mp3', {sampleRate: sys.audio.getSampleRate(), channels:2}); // -1 on failure
sys.audio.decode(dec, float32Out); // frames decoded: 0 at the end (push: or bytes needed), -1 error
sys.audio.getDecoderInfo(dec); // {sampleRate, channels, sourceSampleRate, sourceChannels, duration (-1 unknown), position, ready, ended, needsData, seekable}
sys.audio.seekDecoder(dec, seconds); sys.audio.closeDecoder(dec);
const push = sys.audio.createDecoder({sampleRate, channels}); sys.audio.feedDecoder(push, bytes, isLast); // no seek, no duration
// Analysis (native, ~40x faster than the same maths in JS): Float32Array in, no allocation per call on the JS side.
sys.audio.fft(re, im, inverse); // in place, same power-of-two length (2..2^20); inverse scaled by 1/n
sys.audio.getSpectrum(samples, outDb); // Hann-windowed dB, samples.length/2 bins (bin k = k*rate/length Hz), full-scale sine ~0 dB
sys.audio.detectPitch(samples, sampleRate, {minFrequency:50, maxFrequency:4000, minClarity:0.7, minLevel:-60}); // {frequency (0: none), clarity 0..1, level dB}; McLeod method, fundamental even under louder harmonics; lowest = rate/(length/2)
```

Streams and analysis are JS only. Their callbacks run on the app thread before and after each frame's drawing: keep them quick. Output queues are adaptive unless `latencyMs` is given (start 20 ms, 40 Android, 30 web; grow after an underrun, shrink when steady; `info.latency` is the current size). Recording needs `RECORD_AUDIO` in app.json `permissions` on Android (the first openInput asks and returns -1: open again once granted) and https or localhost on the web.

Start audio lazily from a user gesture; web audio may fail/stay silent at load. `input.keyboard` contains modifiers only, so it is not an "any key" signal: use `pointer.pressed` or explicit `isKeyPressed(scancode)`. Initialize transactionally—mark ready only after the required resources exist; on failure destroy partial oscillators and allow a later gesture to retry.

`startOscillator()` resets phase and sustains sound until `stopOscillator()`; gain 0 does not stop/free a voice. For timed notes, precreate a bounded voice pool, configure ADSR, pair `noteOn()` with a scheduled `noteOff()`, and explicitly return/steal voices—active voices do not expire automatically. Sequencers should preserve clock remainder (`while (accumulator>=stepDuration) accumulator-=stepDuration`) with a bounded catch-up count instead of resetting to zero; stop/release voices when playback stops.

Audio handles are integer oscillator/buffer IDs, not voice objects. There is no `createVoice`, `setVoice`, `startVoice`, `stopVoice`, `releaseVoice`, `audio.now()`, or timestamp parameter on oscillator calls: setters and `noteOn`/`noteOff` execute immediately. Schedule sequences in the frame loop with second-based `input.totalTime`/accumulators, or use JS `sys.timer` with millisecond delays; clear pending timers and stop/destroy owned oscillators when deliberately ending/replacing the sequence. Runtime teardown handles final cleanup—there is no managed JS exit callback.

### MIDI and RTP-MIDI

```js
constants: NOTE_OFF 0x80, NOTE_ON 0x90, POLY_PRESSURE 0xA0, CONTROL_CHANGE 0xB0,
PROGRAM_CHANGE 0xC0, CHANNEL_PRESSURE 0xD0, PITCH_BEND 0xE0, SYSEX 0xF0;
CC_MOD_WHEEL 1, CC_BREATH 2, CC_FOOT 4, CC_VOLUME 7, CC_PAN 10, CC_EXPRESSION 11,
CC_SUSTAIN 64, CC_ALL_SOUND_OFF 120, CC_ALL_NOTES_OFF 123.

sys.midi.getInputCount(); sys.midi.getOutputCount(); sys.midi.getInputDevices(); sys.midi.getOutputDevices(); sys.midi.refreshDevices();
sys.midi.onDevicesChanged(event => { event.inputs; event.outputs; event.generation; }); // replaces listener; pass null to unsubscribe; frame-thread callback
const ih = sys.midi.openInput(index, msg => {}); sys.midi.closeInput(ih);
const oh = sys.midi.openOutput(index); sys.midi.closeOutput(oh);
sys.midi.sendMessage(oh,status,d1,d2); sys.midi.sendRaw(oh, byteArray);
sys.midi.noteOn(oh,ch,note,vel); sys.midi.noteOff(oh,ch,note,vel); sys.midi.controlChange(oh,ch,cc,value);
sys.midi.programChange(oh,ch,program); sys.midi.pitchBend(oh,ch,-8192..8191);
sys.midi.channelPressure(oh,ch,pressure); sys.midi.polyPressure(oh,ch,note,pressure);
sys.midi.getType(status); sys.midi.getChannel(status);

const s = sys.midi.createSession(name, port=0); sys.midi.connectSession(s,host,port); sys.midi.destroySession(s);
sys.midi.onSessionMessage(s, callback); sys.midi.sessionSend(s,status,d1,d2); sys.midi.sessionNoteOn(s,ch,note,vel);
sys.midi.sessionNoteOff(s,ch,note,vel); sys.midi.sessionControlChange(s,ch,cc,value); sys.midi.sessionSendRaw(s,bytes); sys.midi.getSessions();
```

Message shape: `{status,data1,data2,type,channel,timestamp}` (timestamp: monotonic µs, 0 if unknown). SysEx adds `data` (complete F0..F7, reassembled across packets, ≤65535 bytes). Messages and SysEx arrive in device order; real-time bytes are not reported; ≤1024 events (32 SysEx) per frame. RTP messages add `network:true`. Device indices shift on hot-plug: remember devices by name and re-resolve in `onDevicesChanged`.

### SQLite, files/assets, network, UDP

```js
const db = sys.db.open('mydata'); // db name: alnum, underscore, hyphen; creates .db
sys.db.execute(db, sql); sys.db.run(db, sql, ...params); sys.db.query(db, sql, ...params);
sys.db.lastInsertId(db); sys.db.getError(); sys.db.close(db); // params: null,bool,number,string

sys.files.list(); // [{name:'assets',type:'directory'}, {name:'files',type:'directory'}]
sys.files.list('assets'); sys.files.readText('assets/config.json'); sys.files.readBinary('assets/image.png');
const f = sys.files.openRead('files/big.bin'); sys.files.read(f, 65536); /* ArrayBuffer, empty at the end, null on error */ sys.files.seek(f, byteOffset); sys.files.getReadInfo(f); /* {size, position} */ sys.files.closeRead(f); // block reads, JS only; openRead returns null on failure
sys.assets.list(); sys.assets.readText('config.json'); sys.assets.readBinary('image.png'); // read-only wrapper over sys.files assets/
sys.assets.exists('config.json'); sys.assets.isDirectory('images'); sys.assets.size('image.png'); sys.assets.getError();
sys.files.list('files'); sys.files.readText('files/settings.json');
sys.files.writeText('files/settings.json', text); sys.files.writeBinary('files/data.bin', arrayBufferOrView); // web also downloads
sys.files.exists(mountedPath); sys.files.isDirectory(mountedPath); sys.files.size(mountedPath); sys.files.getError();
sys.files.pickText((file,error) => { if (file) console.log(file.name, file.text); }, '.json'); // JS only, optional enforced extension, max 8 MiB
sys.files.saveText('backup.json', text, error => { if (error) console.log(error); }); // JS only, user-visible Save As

const response = await fetch(url, {method, headers, body}); // also sys.net.fetch
response.ok; response.status; response.statusText; response.url; response.redirected; response.type; response.bodyUsed; response.headers;
response.text(); response.json(); response.arrayBuffer(); // body methods sync after promise resolves; one body read only
response.headers.get(name); has(name); entries(); keys(); values(); forEach((value,name)=>{});

const sock = sys.net.udp.bind(portOr0); sys.net.udp.getPort(sock);
sys.net.udp.send(sock, host, port, stringOrArrayBufferOrTypedArray); // returns bytes or -1
sys.net.udp.onMessage(sock, msg => { msg.data; msg.host; msg.port; }); sys.net.udp.close(sock);
```

JS SQLite semantics: `open()` throws on failure; `execute()` returns `true`; `run()` returns the changed-row count; `query()` returns row objects; execute/run/query throw `InternalError` on SQL failures. `sys.db.getError()` takes no handle. Catch at the event boundary, show the error, and mutate in-memory UI state only after success; otherwise an uncaught callback error aborts that frame. Check expected change counts and use `lastInsertId()` rather than requerying the whole table after every insert.

`sys.db.open(name)` returns an integer handle, not a database object. Every DB call remains under `sys.db` and receives that handle. Parameters are variadic (`sys.db.run(db,sql,p1,p2)` / `query(db,sql,p1)`), not one array argument; `execute(db,sql)` is for SQL without parameters. After the fetch promise resolves, `response.json()`, `text()`, and `arrayBuffer()` are synchronous one-shot body reads—do not invent streaming or framework response methods.

Open/create schema once; load on initialization or controlled page refresh, never each frame. Enforce bounds when loading and writing (`LIMIT`/pagination and, where suitable, schema constraints)—a UI-only add limit does not bound pre-existing/database growth. Track editing/confirmation/selection by stable row id, not array index. For multi-step changes, stage a copy then `BEGIN`/writes/`COMMIT`, publishing UI state only after commit; on `ROLLBACK`, restore/reload local state rather than leaving it ahead of the DB. Close handles on initialization failure or when deliberately finished; runtime teardown closes the rest. Text-session rules still apply inside rows: preserve authoritative selection/composition and synchronize every focused frame even if scrolling hides the edited row.

SQLite returns row objects. `sys.files` root contains read-only bundle `assets/` and sole-writable sandbox `files/`; mount prefixes are required except root `list()`. `sys.assets` is a read-only, shared-context/error wrapper: `readText('x')`=`sys.files.readText('assets/x')`, `list()`=`sys.files.list('assets')`. Its paths omit the prefix. Writes create parents; listings omit hidden names; absolute/`..` paths are rejected. Desktop: assets=project, files=`--file-root` (project default). Android: files=external-files, or granted broad storage with `"filesystem":true`. Web: files=IDBFS and writes also download.

Physical project paths remain part of the asset-relative name: a project-root `notes.json` is `sys.assets.readText('notes.json')`; `assets/notes.json` is `sys.assets.readText('assets/notes.json')`. For seeded persistent data, load writable `files/` first and fall back to bundled assets only when no saved state exists—asset-first loading discards visible persistence on every restart. `"filesystem":true` is not needed for ordinary `assets/`/sandboxed `files/`; it requests Android broad external storage.

`writeText`/`writeBinary` return the saved path (or throw on JS wrapper failure); update the in-memory model only after save success, or restore it on failure. Treat imported JSON as untrusted: enforce total item count, per-field lengths/types, unique/stable IDs, and valid dates before replacing state, then persist transactionally. `pickText` already caps bytes, but parsed object counts/strings still require app bounds. Picker/save callbacks return on the owning frame thread; keep pending flags to prevent overlapping dialogs. Canvas form controls still require text sessions and capture/release semantics—file callbacks do not relax GUI invariants.

Fetch requires `app.json` policy and rejects disabled/denied/DNS/connect/TLS/invalid-URL cases; desktop socket read/write timeout is 30 s. UDP is polled once/frame. JS `pickText(callback,extension?)` explicitly accesses outside mounts via desktop dialog/browser picker/Android SAF (Downloads/cloud/SD, no broad permission): the optional extension filters pickers and is enforced on the returned name; result is `{name,text}` or error, one picker may be active, and the cap is 8 MiB. `saveText(name,text,callback)` opens Save As; Android uses `ACTION_CREATE_DOCUMENT`, not app-scoped `Android/data`.

### Sensors, device, neural, capabilities

```js
sys.sensors.isAvailable(); sys.sensors.hasAccelerometer(); sys.sensors.hasCompass();
sys.sensors.start(); sys.sensors.stop(); sys.sensors.isActive();
sys.sensors.getAccel();   // {x,y,z} m/s^2 or null
sys.sensors.getCompass(); // {x,y,z,heading} uT/degrees or null
sys.device.keepScreenOn(true|false); // returns accepted bool
sys.device.setClipboardText(text); sys.device.getClipboardText(); // string|null; web returns the last copied/pasted text
sys.device.haptic('light'|'medium'|'heavy'|'selection'|'success'|'warning'|'error'); // false without haptics (desktop)
sys.device.getPreferences(); // {darkMode, reducedMotion, highContrast, fontScale, safeArea:{top,right,bottom,left}, keyboardInset} (physical px; keyboardInset: height the on-screen keyboard covers, 0 when hidden)
sys.device.setCursor('default'|'text'|'pointer'|'grab'|'grabbing'|'move'|'ew-resize'|'ns-resize'|'nwse-resize'|'nesw-resize'|'not-allowed'|'wait'|'crosshair'|'none');

sys.accessibility.isAvailable(); // web, Android, macOS (VoiceOver); false on Windows/Linux for now
sys.accessibility.isActive();    // a screen reader probably runs (always true on web)
sys.accessibility.update([{id, role, label, value?, x, y, width, height, checked?, selected?, focused?, disabled?, expanded?, min?, max?, rangeValue?}]);
sys.accessibility.takeActions(); // [{id, action:'press'|'focus'|'increment'|'decrement'|'setValue', value}]

sys.capabilities.neural.available; sys.capabilities.midi.available; sys.capabilities.udp.available;
sys.capabilities.http.available; sys.capabilities.sensors.available;

sys.neural.isAvailable(); sys.neural.getError();
const model = sys.neural.loadModel('model.onnx'); const model2 = sys.neural.loadModelFromBuffer(buffer);
const info = sys.neural.getModelInfo(model); // {description,producerName,graphName,domain,version,inputs,outputs}
sys.neural.setInput(model, tensorName, typedArray, shape?); sys.neural.run(model); const out = sys.neural.getOutput(model, outputName);
const outputs = sys.neural.run(model, {[inputName]: typedArray}); sys.neural.unloadModel(model);
```

### Local GGUF chat with llama.cpp

Desktop builds configured with `ENABLE_LLAMACPP=ON` and private Android builds
expose `sys.llamacpp` to JavaScript/TypeScript and Lua. CPU support is mandatory;
macOS builds also include Metal. Model paths must use `assets/` or `files/`; absolute paths, traversal,
and symlink escapes are rejected. Loading and generation run on one worker and
callbacks are delivered during runtime polling.

```js
sys.llamacpp.loadModel("files/models/model.gguf", { device: "auto" }, (model, error) => {
  if (!model) { console.log(error.message); return; }
  const chat = model.createChat({ systemPrompt: "Answer concisely." });
  chat.send("Hello", { maxTokens: 128, stop: ["</s>"] }, {
    onText(text) { console.log(text); },
    onComplete(result) { console.log(result.generatedTokensPerSecond); },
    onError(generationError) { console.log(generationError.message); }
  });
});
```

`getDevices()` reports initialized ggml devices and available memory.
`sys.capabilities.llamacpp.available` reports compile-time managed availability;
Wasmtime always reports false. Web, CUDA/Vulkan plugins, and placement caching
remain unavailable in web builds. Desktop CUDA/Vulkan plugins are opt-in CMake
profiles (`ENABLE_LLAMACPP_CUDA` / `ENABLE_LLAMACPP_VULKAN`) and require their
vendor SDK. Automatic placement is cached by pinned revision, model identity,
context, and batch settings; `fallbackReason` reports `placement cache hit` when
the cached placement is reused. Android currently ships CPU inference and
cancels generation on pause.

Tensor info: `{name, shape:number[], dtype:'float32'|'int32'|'int64'|'uint8'}`. JS tensor arrays: `Float32Array|Int32Array|BigInt64Array|Uint8Array`. Dynamic shape dimension is `-1`; inferred from input element count when possible. Execution providers: macOS CoreML then CPU; Android NNAPI then CPU; Linux CPU. ONNX only. Requires `app.json` `{ "neural": true }` for apps.

### UI toolkit (budo-ui)

JavaScript only. `budo init <dir> --template ui` writes the library to `ui/` (the app owns and may edit it; `ui/README.md` lists every widget) and a starter `main.js`. Immediate mode: draw the whole interface from app data every frame; the library keeps only interaction and motion state under stable ids.

```js
import { createUI, themes } from './ui/budo-ui.js';
const ui = createUI();            // once, outside the frame
ui.setTheme('system');            // or themes.light / themes.dark; colors animate
const name = { value: '' }; let on = false, level = 0.5, choice = 'A';
function frame() {
  ui.begin(sys.input.get());      // ui.bounds = window minus notches/system bars
  ui.clear();
  const [a, b, c, d, e] = ui.rows(ui.inset(ui.bounds, ui.dp(16)), [48, 48, 48, 48, 48].map(ui.dp), ui.dp(8));
  ui.field('name', a, name, { placeholder: 'Name' });     // edits name.value in place
  on = ui.toggle('on', 'Enabled', b, on);                  // returns the new value
  level = ui.slider('level', c, level, { label: 'Level' });
  choice = ui.segmented('choice', d, ['A', 'B', 'C'], choice);
  if (ui.button('save', 'Save', e, { primary: true })) ui.toast('Saved');  // true on click
  ui.end();
  ui.nextFrame(frame);            // next frame while animating, else wait for input
}
sys.animation.requestFrame(frame);
```

- Rectangles are physical pixels; `ui.dp(n)` converts dp, `ui.sp(n)` follows the text scale. `rows`/`columns(rect, sizes, gap)` take numbers, `{weight:n}`, or `{content:n}`; also `inset`, `split`, `scroll(id, rect, contentHeight, draw)`, `panel(rect, draw)`, `scope(id, draw)`.
- Widgets: `button` (`primary`, `ghost`, `danger`, `icon: 'plus'`), `iconButton(id, icon, rect, {label})`, `menu`, `contextMenu`, `toggle`, `checkbox`, `radio`, `segmented`, `tabs`, `select`, `slider`, `field`, `inlineEdit`, `textArea(id, rect, {value}, {placeholder,label,maxLength,readOnly})` (multiline: wrap, scroll, selection, word moves, undo), `label`, `paragraph`, `richText`, `progress`, `spinner`, `skeleton`, `tooltip`, `toast`, `list` (returns `{selectedId, move, swipedId}`), `table(id, rect, {columns:[{key,label,width?,align?,sortable?}], rowCount, row:i=>obj, rowId:i=>id, selectedId, sort})` (virtualized; returns `{selectedId, activatedId, sort}`, app re-sorts), `tree(id, rect, [{id,label,children?,icon?,expanded?}], {selectedId})` (returns `{selectedId, activatedId, toggledId}`), `dialog`/`sheet` (return true when dismissed), `navigator(id, rect, stack, drawPage)` (returns true on back) with `shared(tag, rect, draw)`, `glass`, `layer`, `overlay`.
- Icons: `ui.icon(nameOrPathData, rect, {color})`; built in: check close plus minus chevronLeft/Right/Up/Down arrowLeft/Right menu more search trash edit copy star heart home user info alert refresh sun moon folder file; `ui.registerIcon(name, d)` adds 24-unit SVG path data. Theme tokens `elevation` (3 shadow levels, `ui.shadow(rect, level)`) and `accentGradient` ([top, bottom] colors for accent fills).
- Motion: `ui.spring(key, target)`, `ui.color(key, color)`, `ui.rect(key, rect)`, `ui.presence(key, visible, t => ...)`. Never hand-roll easing for widget states.
- Custom widgets: draw with `ui.fill/outline/text` or `sys.canvas`, then `ui.interact(id, rect, { cursor, a11y: { role, label } })` → `{hovered, held, clicked, focused}`; `a11y` makes it Tab-focusable and screen-reader visible.
- Built in, no app code: keyboard focus (Tab, Enter/Space, focus traps in dialogs), touch momentum, cursors, clipboard, haptics, reduced motion, dark mode, text scale, screen readers. Call `ui.nextFrame(frame)` (not `requestFrame`) at the end so idle apps sleep; `ui.wakeAfter(seconds)` schedules timed changes.

## Lua runtime

Lua 5.4 globals: `sys`, `console.log`, callback-based `fetch`, `json_parse`. No Budo module system beyond normal Lua libraries. Same core namespaces as JS except `sys.timer` and `sys.graphics` are not available. `sys.capabilities` exists on desktop, Android, and current web builds.

Lua loop:

```lua
local function frame(timestamp)
  sys.canvas.clear('#FFFFFF')
  sys.animation.requestFrame(frame)
end
sys.animation.requestFrame(frame)
```

Lua API mirrors JS method names for `canvas`, `paint`, `transform`, `path`, `svg`, `font`, `gl`, `input`, `window`, `animation`, `audio`, `midi`, `db`, `files`, `assets`, `udp`, `magneto`, `device`, `math`, `capabilities`, `neural`, with these differences:

```text
Arrays/lists are 1-based Lua tables.
No sys.timer. No sys.graphics/CanvasTexture.
Lua `sys.input` supports the same committed text, textEdit, composition, and start/update/stopTextInput session contract as JS.
sys.animation.start(callback) aliases requestFrame.
sys.exit(code) sets the status; the app ends after the current script or callback returns.
fetch(url, callback) or fetch(url, options, callback); callback(response,error).
response table: status,statusText,ok,url,redirected,headers,body,bodyLen.
json_parse(jsonString) returns Lua table.
sys.files.readBinary(path) returns a Lua binary string.
sys.assets.readBinary(path) returns a Lua binary string and reads sys.files assets/<path>.
sys.files.writeText(path,text) returns saved path or nil.
sys.files.writeBinary(path,binaryString) returns saved path or nil.
sys.files.getError() and sys.assets.getError() return the shared last file error.
sys.audio.setBufferData(bufferId, samplesTable, offset).
sys.audio.loadBufferFromBuffer(binaryString).
sys.midi.sendRaw(handle, bytesTable); SysEx data is 1-based table.
sys.db.query returns 1-based list of row tables.
sys.neural.run(model,{input={data={...},shape={...}}}) returns output tables {data,shape,dtype}.
sys.neural.setInput(model,name,dataTable,shape?) and getOutput(model,name) also exist.
sys.canvas.setGradient('linear',x0,y0,x1,y1,{'#F00','#00F'},{0,1}); colors/stops are 1-based tables.
sys.canvas.drawParagraph(text,x,y,width,size,{align='center',lineHeight=1.25,maxLines=2}) returns {width,height,lines}.
sys.canvas.drawRichText({'plain ', { text = 'big', size = 24, color = '#C00' }}, x, y, width, { size = 16 }); spans are 1-based tables.
sys.accessibility.update(nodes) takes a 1-based table of node tables; takeActions() returns one.
```

Lua `sys.gl` includes: `createProgram`, `createProgramFromBuffer`, `destroyProgram`, `drawFullscreen`, uniforms `setUniform1i/1f/2f/3f/4f`, render targets `createRenderTarget/destroyRenderTarget/resizeRenderTarget/drawRegion/bindTexture`, 3D buffers/textures/layout/uniform arrays/mesh draw, plus `loadTextureCube` and `loadTextureCubeFromBuffer`.

## WebAssembly runtime

WASM runs through Wasmtime. Imports are from module `env`. Exports: optional `init`, recommended `frame(timestamp:f32)`, optional `memory`. `init` runs once; `frame` runs per frame. A module without `frame` that never draws runs `init` without a window and ends. Import `app_exit(i32)` ends the app with that status (it traps to stop the module). String-backed host calls require exported memory, but current runtime resolves exported `memory` after `init`; therefore do string-backed calls from `frame` or later, not `init`.

Numeric-only imports can be used in `init`: drawing shapes, transforms, scalar input/window, math, capability probes. String-backed calls needing memory include GL program/uniform names, DB, file, SVG, UDP host strings, network, raw MIDI/RTP-MIDI strings, neural strings, text draw/measure.

Minimal WAT:

```wat
(module
  (import "env" "canvas_clear" (func $clear (param i32)))
  (import "env" "canvas_draw_circle" (func $circle (param f32 f32 f32)))
  (import "env" "canvas_set_fill_color" (func $fill (param i32)))
  (import "env" "window_get_width" (func $w (result i32)))
  (import "env" "window_get_height" (func $h (result i32)))
  (func (export "frame") (param $t f32)
    (call $clear (i32.const 0xFFFFFFFF))
    (call $fill (i32.const 0xFF0F4C5C))
    (call $circle
      (f32.div (f32.convert_i32_s (call $w)) (f32.const 2))
      (f32.div (f32.convert_i32_s (call $h)) (f32.const 2))
      (f32.const 60))))
```

WASM import groups (exact names):

```text
Drawing: canvas_clear(i32), canvas_draw_rect(f32x4), canvas_draw_round_rect(f32x6), canvas_draw_circle(f32x3), canvas_draw_oval(f32x4), canvas_draw_line(f32x4), canvas_draw_point(f32x2), canvas_draw_arc(f32x6,i32), canvas_read_pixels(ptr,max)->bytes, canvas_draw_text(ptr,len,x,y,size)->width, canvas_measure_text(ptr,len,size)->width, canvas_measure_text_rect(ptr,len,size,outW,outH).
Canvas style: canvas_set_fill_color(i32), canvas_set_stroke_color(i32), canvas_set_stroke_width(f32), canvas_set_anti_alias(i32), canvas_set_alpha(i32), advanced canvas_set_blend_mode(i32), canvas_set_blur_filter(f32,f32), canvas_set_drop_shadow_filter(f32,f32,f32,f32,i32), canvas_set_drop_shadow_only_filter(f32,f32,f32,f32,i32), canvas_clear_image_filter(), canvas_set_color_matrix_filter(ptr,len20), canvas_set_blend_color_filter(color,blendMode), canvas_clear_color_filter().
Gradients: canvas_set_linear_gradient(x0,y0,x1,y1,colorsPtr,stopsPtr,count), canvas_set_radial_gradient(cx,cy,r,colorsPtr,stopsPtr,count), canvas_set_sweep_gradient(cx,cy,colorsPtr,stopsPtr,count), canvas_clear_gradient(). Colors are u32 ARGB, stops f32; stopsPtr -1 spaces colors evenly; count 2..16. Fill/stroke color setters clear the gradient.
Paragraphs: canvas_draw_paragraph(ptr,len,x,y,width,size,align(0 left,1 center,2 right),lineHeight,maxLines)->height, canvas_measure_paragraph(ptr,len,width,size,lineHeight,maxLines,outPtr)->height; outPtr >= 0 receives {f32 width, f32 height, i32 lines}. y is the paragraph top; lineHeight <= 0 means 1.25.
Rich text: canvas_draw_rich_text(spansPtr,count,x,y,width,align,lineHeight,maxLines)->height, canvas_measure_rich_text(spansPtr,count,width,lineHeight,maxLines,outPtr)->height; spans are 16-byte records {i32 textPtr, i32 textLen, f32 size (<=0 default), u32 argb (0 = paint)}.
Frames/device/accessibility: animation_wait_for_input(f32 timeoutMs) skips frame() calls until input, resize, or timeout (<=0: none); device_set_clipboard_text(ptr,len)->i32, device_get_clipboard_text(ptr,max)->byteLength or -1, device_haptic(kind 0 light..6 error)->i32, device_get_preference(id 0 darkMode,1 reducedMotion,2 highContrast,3 fontScale,4..7 safe top/right/bottom/left)->f32, device_set_cursor(0 default,1 text,2 pointer,3 grab,4 grabbing,5 move,6 ew,7 ns,8 nwse,9 nesw,10 not-allowed,11 wait,12 crosshair,13 none)->i32; accessibility_is_available/is_active()->i32, accessibility_update(jsonPtr,len)->i32, accessibility_take_actions(ptr,max)->JSON length (written only when it fits).
Transform/path: transform_save/restore/translate/rotate/rotate_around/scale, canvas_skew, canvas_reset_transform, canvas_clip_rect, canvas_clip_round_rect(x,y,w,h,rx,ry), canvas_clip_path(path), canvas_save_layer(alpha), canvas_save_layer_bounds(x,y,w,h,alpha,backdropBlur) (end both with transform_restore), path_create/reset/move_to/line_to/quad_to/cubic_to/close/add_rect/add_circle/draw, path_add_svg(path,ptr,len)->i32 (1 when parsed; also in the browser bridge).
SVG/window/input/log/math: svg_load/destroy/draw/get_width/get_height; window_get_width/height/display_density; input_get_mouse_x/y, input_get_mouse_button(0 left,1 middle,2 right), input_get_key_down(scancode), input_get_delta_time, input_get_total_time; log_int, log_float; sin, cos, sqrt, atan2(y,x).
GL shaders: gl_create_program(vPtr,vLen,fPtr,fLen)->id, gl_destroy_program, gl_draw_fullscreen, gl_set_uniform_1i/1f/2f/3f/4f, gl_set_uniform_matrix3fv/4fv, gl_set_uniform_1iv/1fv/2fv/3fv/4fv.
GL targets/3D: gl_create_render_target(w,h,depth)->id, gl_destroy_render_target, gl_resize_render_target, gl_draw_region(program,x,y,w,h,target), gl_bind_texture(program,namePtr,nameLen,rt,unit), gl_create_buffer, gl_buffer_data, gl_buffer_sub_data, gl_destroy_buffer, gl_create_texture_2d, gl_load_texture_2d, gl_update_texture_2d, gl_destroy_texture, gl_load_texture_cube, gl_create_vertex_layout, gl_destroy_vertex_layout, gl_set_attribute, gl_set_index_buffer, gl_bind_texture_2d, gl_bind_texture_cube, gl_draw_mesh, gl_get_attrib_location.
Audio: audio_start/stop/set_master_gain, audio_create_oscillator/destroy_oscillator, audio_oscillator_set_type/frequency/gain/detune/start/stop/set_envelope/note_on/note_off, audio_midi_to_freq. No audio buffer API in WASM.
MIDI/RTP: midi_get_input_count/output_count, midi_open_output/close_output, midi_send_message, midi_send_raw, midi_note_on/off/control_change/program_change/pitch_bend. No MIDI input callbacks/device names/getType/getChannel/refresh. RTP: rtpmidi_create_session, rtpmidi_connect, rtpmidi_destroy_session, rtpmidi_send_message, rtpmidi_get_session_count; no receive callbacks.
DB/files/UDP/network: db_open/close/execute/run/run_int/run_str/query_json/last_insert_id; files_list_json/read_text/read_binary/exists/is_directory/size (paths use `assets/` or `files/`); udp_bind/get_port/send/recv/close; network_fetch, network_fetch_status/status_code, response_body_len/response_body, response_headers_len/response_headers, response_url_len/response_url, error_len/error, network_fetch_release.
Capabilities/sensors/device/neural: capability_neural/midi/udp/http/sensors; magneto_is_available/has_accelerometer/has_compass/start/stop/is_active/get_accel_x/y/z/get_compass_x/y/z/get_compass_heading; device_keep_screen_on(i32)->i32; neural_is_available, neural_get_error, neural_load_model, neural_unload_model, neural_get_input_count/output_count, neural_get_input_info/output_info, neural_set_input_f32, neural_run, neural_copy_output_f32, neural_get_output_f32, neural_get_output_shape.
```

WASM constraints: no font load/set/reset. Paint cap/join absent. `isKeyPressed`, wheel, modifiers, focus, frame count absent. DB parameterized imports are limited (`db_run_int`, `db_run_str`). Query results are JSON strings. UDP receive is polling (`udp_recv`). HTTP is polling and each request slot must be released with `network_fetch_release`. Neural currently exposes staged f32 I/O only.

Advanced WASM paint blend mode integer encoding: 0 src-over, 1 src, 2 dst-over, 3 dst-in, 4 dst-out, 5 src-in, 6 src-out, 7 clear, 8 plus, 9 multiply, 10 screen, 11 overlay, 12 darken, 13 lighten, 14 color-dodge, 15 color-burn, 16 hard-light, 17 soft-light, 18 difference, 19 exclusion, 20 hue, 21 saturation, 22 color, 23 luminosity.

## Web platform

Web target is Emscripten self-contained page. Supports JS QuickJS and Lua 5.4. Does not support Wasmtime/WASM app runtime on web. Rendering uses Skia-WASM/WebGL2; shaders use GLSL ES 300 directly. Web status:

```text
Full: canvas, paint including advanced, transform, path, svg, font, gl, input, window, animation, audio, db, sys.files/sys.assets, JS network fetch.
JS only: sys.graphics CanvasTexture.
Partial: MIDI via Web MIDI in Chromium only, permission required, no RTP-MIDI.
Unavailable: sys.net.udp, sys.neural, Wasmtime app runtime.
Capabilities: current web JS/Lua exposes `sys.capabilities`; `udp` and `neural` report unavailable.
```

Web caveats: full text sessions use a hidden fixed-position input/textarea synchronized with the Budo model; browser `input` events provide authoritative replacement edits and composition events provide preedit state. Web Audio starts after user interaction. Browser `fetch` is subject to CORS. SQLite and the `files/` mount persist via IndexedDB/IDBFS; writes also trigger downloads. The `assets/` mount reads preloaded Emscripten VFS assets. No raw sockets.

## Cross-runtime quick matrix

```text
Canvas/text/measure/paint/transform/path/svg/window/basic animation: JS yes, Lua yes, WASM yes.
Font load/set/reset: JS yes, Lua yes, WASM no.
Timers: JS yes, Lua no, WASM no.
CanvasTexture/sys.graphics: JS yes, Lua no, WASM no.
GL shaders/render targets/3D mesh: JS yes, Lua yes, WASM yes.
Aggregated input object: JS yes, Lua yes, WASM scalar only.
Committed text/full IME text sessions: JS/Lua yes on desktop, web, Android; WASM no.
isKeyPressed: JS yes, Lua yes, WASM no.
Audio oscillators: JS yes, Lua yes, WASM yes. Audio buffers: JS/Lua yes, WASM no.
MIDI output: all. MIDI input callbacks: JS/Lua only. RTP receive: JS/Lua only.
SQLite: all; WASM query JSON and limited params.
File reads: all; binary return ArrayBuffer (JS), Lua string, guest buffer (WASM). Writes: JS/Lua only; web writes also trigger download.
HTTP: JS promise, Lua callback, WASM polling.
UDP: JS/Lua callbacks, WASM polling; web JS/Lua no UDP.
Sensors: JS objects, Lua tables/nil, WASM scalar accessors.
Neural: JS/Lua/WASM on native builds; web no neural.
ES modules and TypeScript stripping: JS only.
```

## Recipes

Network JSON JS:

```js
// app.json: { "network": "api.example.com" }
const resp = await fetch('https://api.example.com/data');
if (!resp.ok) console.log('HTTP ' + resp.status);
else console.log(JSON.stringify(resp.json()));
```

Canvas + shader compositing (Skia UI over a generated background):

```js
// fragment (GLSL ES 300): sample UI from u_canvas and composite over a generated bg
// #version 300 es
// precision highp float; in vec2 v_uv; out vec4 o; uniform sampler2D u_canvas;
// uniform vec2 u_resolution; uniform float u_time;
// void main(){ vec3 bg = 0.5+0.5*cos(u_time+v_uv.xyx+vec3(0,2,4));
//   vec4 ui = texture(u_canvas, v_uv);
//   o = vec4(ui.rgb + bg*(1.0-ui.a), 1.0); }
const program = sys.gl.createProgram('fs.vert', 'fs.frag');
function frame(t) {
  sys.canvas.clear('#00000000');
  sys.canvas.setFillColor('#FFFFFF');
  sys.canvas.drawText('Hello', 40, 80, 32);
  sys.gl.bindScreen();
  sys.gl.drawFullscreen(program); // draw supplies u_canvas/u_resolution/u_time
  sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
```

3D mesh draw:

```js
const vertices = new Float32Array([-0.5,-0.5,0, 0.5,-0.5,0, 0,0.5,0]);
const vbo = sys.gl.createBuffer('vertex', vertices);
const layout = sys.gl.createVertexLayout();
sys.gl.setAttribute(layout, 0, vbo, 3, 'float', false, 12, 0);
function frame(t) {
  sys.canvas.clear('#111111');
  sys.gl.setUniformMatrix4(program, 'u_mvp', mvp);
  sys.gl.drawMesh(program, layout, {mode:'triangles', count: 3, depthTest:true, cull:'back'});
  sys.animation.requestFrame(frame);
}
```

SQLite persistence:

```js
const db = sys.db.open('state');
sys.db.execute(db, 'CREATE TABLE IF NOT EXISTS kv (k TEXT PRIMARY KEY, v TEXT)');
sys.db.run(db, 'INSERT OR REPLACE INTO kv(k,v) VALUES (?,?)', 'score', String(score));
const rows = sys.db.query(db, 'SELECT v FROM kv WHERE k=?', 'score');
```

Neural staged flow:

```js
// app.json: { "neural": true }
if (sys.capabilities?.neural?.available && sys.neural.isAvailable()) {
  const model = sys.neural.loadModel('model.onnx');
  const info = sys.neural.getModelInfo(model);
  sys.neural.setInput(model, info.inputs[0].name, inputTypedArray);
  sys.neural.run(model);
  const output = sys.neural.getOutput(model, info.outputs[0].name);
}
```