# Budo

A lightweight runtime combining **Skia** (2D graphics), **SDL2** (windowing/input), **QuickJS** (JavaScript), and **Wasmtime** (WebAssembly) to create animated graphics applications — on desktop, Android, and the web.

## Features

- **Cross-platform**: Works on Linux, macOS, Windows, Android, and the **web** (Emscripten)
- **Multi language support**: JavaScript (QuickJS), Lua and WebAssembly (Wasmtime)
- **Hardware-accelerated 2D graphics** via Skia
- **Full JavaScript runtime** with QuickJS
- **WebAssembly support** with WAT/WASM files via Wasmtime
- **Canvas-like API** familiar to web developers
- **Immediate mode API** customizable and moderne UIs
- **Animation loop** with `requestAnimationFrame`
- **Mouse and keyboard input** accessible from both runtimes
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

To build Budo, first download third parties code:

```bash
# download third parties
./scripts/setup-skia.sh
./scripts/setup-onnx.sh
./scripts/setup-stb.sh
```

Then call the make command:

```bash
# build ./build/budo
make build
```

## Android application packaging

> **Budo Pro:** Android APK/AAB packaging is a paid feature. Public builds
> show the commands for discoverability, but require the private Android feature
> pack installed under `private/android` before they can build artifacts.

### Prerequisites

- Private Budo Pro Android feature pack installed under `private/android`
- Android SDK and NDK compatible with the installed pack
- Android-ready native dependencies prepared by the Pro setup flow

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


```bash
# Package a deployable web folder
budo web-export examples/demo

# Export and serve locally
budo web-serve examples/demo
```

### Supported runtimes

| Runtime | Web support | Notes |
|---------|-------------|-------|
| JavaScript (QuickJS) | **Yes** | QuickJS is pure C, compiles cleanly with Emscripten |
| Lua 5.4 | **Yes** | Pure C, compiles cleanly with Emscripten |
| WebAssembly (Wasmtime) | **No** | Wasmtime requires OS-level memory mapping; cannot run inside a browser WASM sandbox |

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
