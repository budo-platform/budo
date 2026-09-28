# Native GPU example

Native C code is trusted and unsandboxed. Only compile and package source you
trust. Generate this advanced starter with `budo init my-native-gpu-app
--language c --template gpu`.

This native C application renders a rotating indexed quad with embedded GLSL ES 3.00 shaders and a procedural RGBA8 checker texture. It demonstrates capability discovery, shader programs, uniforms, vertex/index buffers, textures, mesh layouts, draw state, and context-loss-safe resource recreation.

Build and run it with:

    budo compile examples/13_native_gpu --run

For source-level debugging or Clang/GCC application-code sanitizers:

    budo compile examples/13_native_gpu --debug
    budo compile examples/13_native_gpu --debug --sanitize address,undefined

Package the same source for Android with Budo Pro:

    budo android-apk examples/13_native_gpu

The application does not define `main()`. GPU resources are created in `surface_created`, destroyed in `context_lost`, and recreated after surface restoration.
