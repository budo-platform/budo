# Budo Feature Packs

Optional proprietary or separately distributed features integrate through a
versioned feature-pack descriptor. Build systems must not infer availability by
probing implementation files.

## Android packaging pack

The default discovery path is `private/android/feature-pack.json`. Override the
pack root with `BUDO_ANDROID_PACK_ROOT` in Make or CMake.

The descriptor owns:

- Feature identity and descriptor format version.
- Feature-pack release version.
- Supported host Budo version range.
- C implementation entry point for the desktop CLI.
- Android template root embedded by CMake.
- Private source include directory.
- Make target overlay entry point.
- Full-release target group.
- Required pack files and directories.

Both Make and CMake invoke `scripts/resolve-feature-pack.py`. The resolver is the
only component that validates and interprets the descriptor; it emits native
Make/CMake fragments containing canonical absolute paths. An absent descriptor
is a supported public-build state. A present but malformed, incomplete, or
incompatible descriptor is a hard configuration error.

Public builds continue to compile `src/core/android_package_unavailable.c`, so
`android-apk` and `android-aab` remain discoverable. Compatible packs substitute
the manifest-declared implementation and embedded Android template through the
same CLI contract.

Run the resolver directly when diagnosing pack installation:

```sh
python3 scripts/resolve-feature-pack.py \
  --repo . \
  --pack-root private/android \
  --format cmake \
  --output /tmp/budo-android-feature-pack.cmake
```

`feature_pack_resolver` exercises absent, valid, malformed, incomplete, and
version-incompatible packs. The self-hosted Android CI workflow resolves the
manifest before configuring the Pro graph.
