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

On Windows, unzip (budo-windows-x86_64.zip)[https://github.com/budo-platform/budo/releases/latest/download/budo-windows-x86_64.zip] and keep the DLLs next to budo.exe.

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
```

### Android application packaging

Note: Android SDK and NDK need to be installed on your system for the Android features to work.

> **Budo Pro:** Android APK/AAB packaging is a paid feature. But during a limited period 
> of time, the public build of Budo includes the Android
> feature for free! This will last only during Budo's public launch.

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
