# Budo

A lightweight runtime combining **Skia** (2D graphics), **SDL2** (windowing/input), **QuickJS** (JavaScript), and **Wasmtime** (WebAssembly) to create animated graphics applications — on desktop, Android, and the web.

## Features

- **Cross-platform**: Works on Linux, macOS, Windows, Android, and the **web** (Emscripten)
- **Multi language support**: JavaScript (QuickJS), Lua and WebAssembly (Wasmtime)
- **Hardware-accelerated 2D graphics** via Skia
- **Full JavaScript runtime** with QuickJS
- **WebAssembly support** with WAT/WASM files via Wasmtime
- **Canvas-like API** familiar to web developers
- **Immediate mode API** 100% customizable and modern UIs
- **Mouse, Touch and keyboard input** accessible from both runtimes
- **Immediate OpenGL ES 3-style rendering** with fullscreen shaders, render targets, meshes, and canvas textures

## Installation from public release binaries

On MacOS;

```bash
curl -fL -o budo https://github.com/budo-platform/budo/releases/latest/download/budo-macos-aarch64
chmod +x budo && ./budo help
```

On Linux

```bash
curl -fL -o budo https://github.com/budo-platform/budo/releases/latest/download/budo-linux-x86_64
chmod +x budo && ./budo help
```

On Windows, run [budo-windows-x86_64.exe](https://github.com/budo-platform/budo/releases/latest/download/budo-windows-x86_64.exe) directly. These downloads require a release published with the desktop packaging support.

## Distribute a desktop app as one executable

```bash
budo package ./my-app self --name my-app -o ./MyApp
budo package ./my-app linux --name my-app -o ./MyApp-linux
budo package ./my-app macos --name my-app -o ./MyApp-macos
budo package ./my-app windows --name my-app -o ./MyApp.exe
```

The project must have a managed Budo entry point (JavaScript, TypeScript, Lua,
or WebAssembly). Budo embeds the directory as a gzip-compressed archive, including
hidden files. Package only what you intend to distribute; symlinks, special
files, non-ASCII names, and names invalid on Windows are rejected. Write the
output outside the project so later packages don't include earlier builds.

`self` reuses the running executable and doesn't need the network. A development
build may depend on locally installed libraries; on Windows, `self` requires
the standalone build. The other targets use `curl` to download and verify
the runtime from the latest GitHub release (Linux x86-64, macOS Apple Silicon,
Windows x64). The release must support desktop packaging. Use `--release TAG`
to select a specific release.

Linux and macOS Release builds strip the base runtime at link time, so
packaging needs no `strip` tool. For a smaller local `self` package, use a
Release rather than a debug build.

Without `-o`, the output is named after the app ID, with `.exe` on Windows;
existing files are not overwritten. macOS packages are not Developer ID signed
or notarized.

Run the result with no arguments to start the app. Explicit commands such as
`--help` and `run` still work. Budo caches each payload version separately and
keeps `sys.files` writes and relative SQLite databases in a shared `data/`
directory under the app ID. Use the same `--name` for every upgrade, and use
different IDs for unrelated apps. The ID defaults to the lowercased project
directory name. The per-user root is `$XDG_DATA_HOME/budo/desktop-apps` on Linux
(defaulting to `~/.local/share` for `$XDG_DATA_HOME`),
`~/Library/Application Support/budo/desktop-apps` on macOS, and
`%LOCALAPPDATA%\budo\desktop-apps` on Windows.

## Local build

### Prerequisites

#### macOS

```bash
brew install cmake sdl2
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake build-essential libsdl2-dev libfreetype6-dev libfontconfig1-dev
```

#### Windows

- Install Visual Studio 2019+ or MinGW-w64
- Install CMake
- Install SDL2 development libraries (or use vcpkg)

```powershell
# Using vcpkg
vcpkg install sdl2:x64-windows
```

### Building

To build Budo, first download third parties code with those commands:

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

## Running

### Run a demo application

```bash
./build/budo run examples/demo
```

### Create a new application

```bash
./build/budo init MY-APPLICATION-DIRECTORY
./build/budo init MY-APPLICATION-DIRECTORY --template ui   # with the budo-ui widgets
```

### Android application packaging

Note: Android SDK and NDK need to be installed on your system for the Android features to work.

> **Budo Pro:** Android APK/AAB packaging is a Budo Pro feature. During Budo's
> public launch, the released Budo binaries include it for free; a later release
> will make it a paid feature. Budo built from source without the private Android
> feature pack keeps the commands, which then report that packaging is unavailable.

Those commands will compile any Budo application into an Android application.

```bash
# produces the apk file
budo android-apk examples/demo

# this one also installs it on the adb connected Android device
budo android-apk examples/demo --install

# produces an Android app ready to be sent to Google Play Console
budo android-aab examples/demo
```

### Exporting for the web

Budo can compile any JavaScript or Lua application into a self-contained web page
that runs entirely in the browser. The web target replaces Skia
with the browser's Canvas2D API, uses WebGL 2 for shader passes, and maps all
subsystems (audio, MIDI, SQLite, networking, file, sensors) to their Web API
equivalents.

```bash
# Package a deployable web folder
budo web-export examples/demo

# Export and serve locally
budo web-serve examples/demo
```

## License

MIT License

## Acknowledgments

- [Skia Graphics Library](https://skia.org/)
- [QuickJS JavaScript Engine](https://bellard.org/quickjs/)
- [Wasmtime WebAssembly Runtime](https://wasmtime.dev/)
- [SDL2](https://www.libsdl.org/)
