# Third-party notices — Budo native SDK

The native SDK includes or links the following third-party software. The SDK
bundle carries the corresponding license texts under `share/budo/licenses/`.

- **Skia** — BSD 3-Clause license. Copyright Google Inc.
- **SDL2** — zlib license. Copyright Sam Lantinga and contributors.
- **Expat** — MIT license. Copyright the Expat maintainers.
- **stb_image** — MIT license or public-domain dedication. Copyright Sean Barrett.
- **llama.cpp** — MIT license. Optional local GGUF inference; the immutable
  source revision is recorded in `cmake/BudoDependencyLock.cmake`.

The `examples/midi_preset_saver` application bundles **Barlow Semi Condensed
Medium**, copyright 2017 The Barlow Project Authors, under the SIL Open Font
License 1.1. Its license text is included alongside the font as
`BarlowSemiCondensed-OFL.txt`.

Platform SDK libraries and frameworks are supplied by the operating-system
vendor and are not redistributed in the Budo native SDK. Linux SDK consumers
currently use the platform OpenGL, FreeType, and Fontconfig libraries declared
by the relocatable CMake package.
