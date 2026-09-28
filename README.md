# Budo

A lightweight runtime combining **Skia** (2D graphics), **SDL2** (windowing/input), **QuickJS** (JavaScript), and **Wasmtime** (WebAssembly) to create animated graphics applications — on desktop, Android, and the web.

## Features

- **Cross-platform**: Works on Linux, macOS, Windows, Android, and the **web** (Emscripten)
- **Dual runtime support**: JavaScript (QuickJS) or WebAssembly (Wasmtime)
- **Hardware-accelerated 2D graphics** via Skia
- **Full JavaScript runtime** with QuickJS
- **WebAssembly support** with WAT/WASM files via Wasmtime
- **Canvas-like API** familiar to web developers
- **Animation loop** with `requestAnimationFrame` (JS) or `frame` export (WASM)
- **Mouse and keyboard input** accessible from both runtimes
- **Path drawing** with bezier curves
- **Transform stack** (save/restore, translate, rotate, scale)
- **Immediate OpenGL ES 3-style rendering** with fullscreen shaders, render targets, meshes, and Skia canvas textures

## Prerequisites

### macOS

```bash
brew install cmake sdl2
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake build-essential libsdl2-dev libfreetype6-dev libfontconfig1-dev
```

### Windows

(I've never tried to build on Windows)

- Install Visual Studio 2019+ or MinGW-w64
- Install CMake
- Install SDL2 development libraries (or use vcpkg)

```powershell
# Using vcpkg
vcpkg install sdl2:x64-windows
```

## Building

To build Budo, run those commands:

```bash
# download third parties
./scripts/setup-skia.sh
./scripts/setup-onnx.sh
./scripts/setup-stb.sh

# build ./build/budo
make build
```

### Optional llama.cpp inference

Local GGUF inference is disabled by default. Enable the pinned dependency with
`cmake -S . -B build -DENABLE_LLAMACPP=ON`, then build normally. CPU support is
always included; macOS builds also include Metal. Model paths must use Budo's
virtual mounts, for example `assets/model.gguf` or `files/models/model.gguf`.
Private Android arm64 builds include the CPU backend. Web and desktop Wasmtime
do not expose this subsystem.

Desktop Linux/Windows acceleration can be requested as dynamic ggml backend
plugins with `-DENABLE_LLAMACPP_CUDA=ON` or
`-DENABLE_LLAMACPP_VULKAN=ON`. CUDA requires the CUDA toolkit and `nvcc`;
Vulkan requires the Vulkan SDK. Plugin builds switch llama.cpp/ggml to their
supported shared-library mode and stage the core/backend libraries beside the
`budo` executable. The default build remains static CPU plus Metal on macOS.

Successful automatic placement is cached under
`files/.llamacpp-placement-cache`, keyed by the pinned llama.cpp revision,
model file identity and size, context size, and batch size. Explicit device or
GPU-layer settings bypass the cache; stale model/revision/settings records are
ignored automatically.

## Android application packaging

> **Budo Pro:** Android APK/AAB packaging is a paid feature. Public builds
> show the commands for discoverability, but require the private Android feature
> pack installed under `private/android` before they can build artifacts.

Budo Pro can package a JS/TS or Lua application as an Android APK or Android
App Bundle. The public repository documents the CLI and app metadata contract;
the Android runtime architecture, lifecycle, source layout, and build backend
are private implementation details of the feature pack.

### Prerequisites

- Private Budo Pro Android feature pack installed under `private/android`
- Android SDK and NDK compatible with the installed pack
- Android-ready native dependencies prepared by the Pro setup flow
- A rebuilt desktop `budo` binary after installing or updating the pack

### Build an Android APK

```bash
budo android-apk examples/demo
budo android-apk examples/demo --release
```

### Install on Device

```bash
budo android-apk examples/demo --install
```

### Customizing the Android App

Configure the packaged app through the app folder's `app.json`. Supported
metadata includes application ID, display name, version, orientation,
fullscreen mode, permissions, icon source, signing, and release options. See
the Android packaging reference for the stable public surface:
[documentation/android-packaging.md](documentation/android-packaging.md).

## Web Export

Budo can compile any JavaScript or Lua application into a self-contained web page
that runs entirely in the browser using **Emscripten**. The web target replaces Skia
with the browser's Canvas2D API, uses WebGL 2 for shader passes, and maps all
subsystems (audio, MIDI, SQLite, networking, file, sensors) to their Web API
equivalents.

### Prerequisites

- No system Emscripten install is required. The web build downloads a pinned
  project-local emsdk into `third_party/emsdk` and sources it only for the build.
- `git`, `python3`, and `ninja` are required for the one-time Skia WASM setup.

### Supported runtimes

| Runtime | Web support | Notes |
|---------|-------------|-------|
| JavaScript (QuickJS) | **Yes** | QuickJS is pure C, compiles cleanly with Emscripten |
| Lua 5.4 | **Yes** | Pure C, compiles cleanly with Emscripten |
| WebAssembly (Wasmtime) | **No** | Wasmtime requires OS-level memory mapping; cannot run inside a browser WASM sandbox |

### Quick start

```bash
# Build and preview locally
make web-run WEB_APP=examples/hello_world

# Package a deployable web folder
make web-export WEB_APP=examples/demo

# Export and serve locally
make web-serve WEB_APP=examples/demo
```

The `web-export` target produces a self-contained folder under `dist/web/`
containing `index.html`, `budo.js`, `budo.wasm`, and `budo.data`.
Upload this folder to any static HTTP server to deploy.

### Known limitations

- **Wasmtime runtime**: not supported (WASM-in-WASM is not possible)
- **RTP-MIDI / UDP sockets**: not available (browser security model forbids raw sockets)
- **ONNX Runtime / Neural**: not supported on web
- **Rendering**: Canvas2D rendering may differ slightly from Skia (anti-aliasing, text metrics)
- **MIDI**: Web MIDI API is only available in Chromium-based browsers (Chrome, Edge, Opera)
- **Synchronous HTTP**: `network_request()` is not available; use the async `fetch()` API
- **CORS**: Browser fetch is subject to Cross-Origin Resource Sharing restrictions

## Usage

```bash
budo <project_dir|file> [options]
```

- `project_dir` should contain one of these entrypoints:
- `main.js` - JavaScript entry point (uses QuickJS)
- `main.ts` - TypeScript entry point (stripped and run via QuickJS)
- `main.lua` - Lua entry point (uses Lua 5.4)
- `main.wat` - WebAssembly text format (uses Wasmtime)
- `main.wasm` - WebAssembly binary format (uses Wasmtime)

That same directory can also contain vertex and fragment shader files, which application code can load relative to the project root.

The runtime checks for files in this order: `main.ts` → `main.js` → `main.lua` → `main.wat` → `main.wasm`.
You can also pass a single `.js`, `.ts`, `.lua`, `.wat`, or `.wasm` file; Budo stages it as the only file in a temporary virtual app directory and runs it as the matching `main.*` entrypoint.

```bash
# Run a JavaScript example
budo examples/demo

# Run a WebAssembly example
budo examples/wasm_shapes

# Run the shader example
budo examples/shader_demo

# Run a single-file sketch
budo sketch.js

# Run code piped from stdin
echo 'console.log("hello from stdin")' | budo run --from-input js

# Run the graphics orientation diagnostics example
budo examples/orientation_lab

# Run the frozen terrain FPS demo
budo examples/frozenator

# Available options
Options:
  --width <n>      Window width (default: 800)
  --height <n>     Window height (default: 600)
  --title <str>    Window title
  --fullscreen     Start in fullscreen mode
  --no-vsync       Disable vertical sync
  --watch          Restart the app when project files change
  --from-input <k> Read stdin as a virtual app main file (js, ts, lua, wat, wasm)
  --help           Show help message
```

## WebAssembly API

WebAssembly modules can use the Skia canvas through imported host functions. The module should export an `init` function (called once) and a `frame` function (called each frame).

### Module Structure

```wat
(module
  ;; Import canvas functions from "env" module
  (import "env" "draw_circle" (func $draw_circle (param f32 f32 f32)))
  ;; ... more imports
  
  ;; Optional: called once at startup
  (func (export "init")
    ;; initialization code
  )
  
  ;; Required: called each frame with timestamp in milliseconds
  (func (export "frame") (param $timestamp f32)
    ;; drawing code
  )
)
```

### Imported Functions

All functions are imported from the `"env"` module.

#### Drawing Functions

```wat
;; Clear canvas with ARGB color
(import "env" "canvas_clear" (func (param i32)))

;; Draw shapes (x, y, width, height as f32)
(import "env" "draw_rect" (func (param f32 f32 f32 f32)))
(import "env" "draw_round_rect" (func (param f32 f32 f32 f32 f32 f32)))
(import "env" "draw_circle" (func (param f32 f32 f32)))  ;; cx, cy, radius
(import "env" "draw_oval" (func (param f32 f32 f32 f32)))
(import "env" "draw_line" (func (param f32 f32 f32 f32)))  ;; x1, y1, x2, y2
(import "env" "draw_point" (func (param f32 f32)))
(import "env" "draw_arc" (func (param f32 f32 f32 f32 f32 f32 i32)))  ;; x,y,w,h,start,sweep,useCenter
```

#### Style Functions

```wat
(import "env" "set_fill_color" (func (param i32)))     ;; ARGB color
(import "env" "set_stroke_color" (func (param i32)))   ;; ARGB color
(import "env" "set_stroke_width" (func (param f32)))
(import "env" "set_anti_alias" (func (param i32)))     ;; 0 or 1
(import "env" "set_alpha" (func (param i32)))          ;; 0-255
```

#### Transform Functions

```wat
(import "env" "canvas_save" (func))
(import "env" "canvas_restore" (func))
(import "env" "canvas_translate" (func (param f32 f32)))
(import "env" "canvas_rotate" (func (param f32)))       ;; degrees
(import "env" "canvas_rotate_around" (func (param f32 f32 f32)))  ;; degrees, px, py
(import "env" "canvas_scale" (func (param f32 f32)))
(import "env" "canvas_skew" (func (param f32 f32)))
(import "env" "canvas_reset_transform" (func))
(import "env" "canvas_clip_rect" (func (param f32 f32 f32 f32)))
```

#### Path Functions

```wat
(import "env" "path_create" (func (result i32)))        ;; returns path id
(import "env" "path_reset" (func (param i32)))
(import "env" "path_move_to" (func (param i32 f32 f32)))
(import "env" "path_line_to" (func (param i32 f32 f32)))
(import "env" "path_quad_to" (func (param i32 f32 f32 f32 f32)))
(import "env" "path_cubic_to" (func (param i32 f32 f32 f32 f32 f32 f32)))
(import "env" "path_close" (func (param i32)))
(import "env" "path_add_rect" (func (param i32 f32 f32 f32 f32)))
(import "env" "path_add_circle" (func (param i32 f32 f32 f32)))
(import "env" "path_draw" (func (param i32)))           ;; draw path
```

#### Input Functions

```wat
(import "env" "get_width" (func (result i32)))
(import "env" "get_height" (func (result i32)))
(import "env" "get_mouse_x" (func (result i32)))
(import "env" "get_mouse_y" (func (result i32)))
(import "env" "get_mouse_button" (func (param i32) (result i32)))  ;; 0=left, 1=middle, 2=right
(import "env" "get_key_down" (func (param i32) (result i32)))      ;; SDL scancode
(import "env" "get_delta_time" (func (result f32)))
(import "env" "get_total_time" (func (result f32)))
```

#### Math Functions

```wat
(import "env" "sin" (func (param f32) (result f32)))
(import "env" "cos" (func (param f32) (result f32)))
(import "env" "sqrt" (func (param f32) (result f32)))
(import "env" "atan2" (func (param f32 f32) (result f32)))
```

#### Debug Functions

```wat
(import "env" "log_int" (func (param i32)))
(import "env" "log_float" (func (param f32)))
```

#### OpenGL Shader Functions

Shader paths and uniform names are passed as UTF-8 byte slices stored in the module memory.

```wat
(import "env" "gl_create_program" (func (param i32 i32 i32 i32) (result i32)))
(import "env" "gl_destroy_program" (func (param i32)))
(import "env" "gl_draw_fullscreen" (func (param i32)))
(import "env" "gl_set_uniform_1i" (func (param i32 i32 i32 i32)))
(import "env" "gl_set_uniform_1f" (func (param i32 i32 i32 f32)))
(import "env" "gl_set_uniform_2f" (func (param i32 i32 i32 f32 f32)))
(import "env" "gl_set_uniform_3f" (func (param i32 i32 i32 f32 f32 f32)))
(import "env" "gl_set_uniform_4f" (func (param i32 i32 i32 f32 f32 f32 f32)))
```

For `gl_create_program`, the arguments are `(vertex_ptr, vertex_len, fragment_ptr, fragment_len)`. For the uniform setters, the string pair is `(name_ptr, name_len)`.

### WebAssembly Example

```wat
;; Simple circle that follows the mouse
(module
  (import "env" "canvas_clear" (func $clear (param i32)))
  (import "env" "draw_circle" (func $circle (param f32 f32 f32)))
  (import "env" "set_fill_color" (func $fill (param i32)))
  (import "env" "get_mouse_x" (func $mx (result i32)))
  (import "env" "get_mouse_y" (func $my (result i32)))
  
  (func (export "init"))
  
  (func (export "frame") (param $time f32)
    ;; Clear with white
    (call $clear (i32.const 0xFFFFFFFF))
    
    ;; Set red fill color
    (call $fill (i32.const 0xFFFF0000))
    
    ;; Draw circle at mouse position
    (call $circle
      (f32.convert_i32_s (call $mx))
      (f32.convert_i32_s (call $my))
      (f32.const 30.0))
  )
)
```

## Dependencies

- **Skia**: 2D graphics library (must be installed, see setup scripts)
- **QuickJS**: JavaScript engine (fetched via CMake)
- **Wasmtime**: WebAssembly runtime (fetched via CMake)
- **SDL2**: Cross-platform windowing and input (must be installed)
- **ONNX Runtime** *(optional)*: neural inference library, loaded at runtime (see above)

## License

MIT License

## Acknowledgments

- [Skia Graphics Library](https://skia.org/)
- [QuickJS JavaScript Engine](https://bellard.org/quickjs/)
- [Wasmtime WebAssembly Runtime](https://wasmtime.dev/)
- [SDL2](https://www.libsdl.org/)


## Windows build

### Preliminary

```bash
# Install QEMU + Vagrant
brew install qemu vagrant          # macOS
sudo apt install qemu-system-x86 qemu-utils vagrant  # Linux

# Install the vagrant-qemu plugin
vagrant plugin install vagrant-qemu
```

### Build

Télécharger un iso

et ensuite...


## TODO

séparer init context et init window et canvas (presque fini)

window c'est pour getWidth et getHeight, les mettre dans canvas plutot (et aussi pour CanvasTexture)

TARGET API namespace organisation

sys
    device
    db

    io
        files
        assets
    timer

    math (mostly matrix, vec and quad)

    g
        animation
        window
        canvas
        path
        svg
        gl
        graphics

    audio
    midi
    
    net
        udp

    sensors
    
    neural
    llamacpp

