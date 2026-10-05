# Shared immutable dependency coordinates for native and web builds.
# Keep versions unchanged unless the dependency update is intentional.

set(BUDO_DEPENDENCY_LOCK_VERSION 1)

set(BUDO_QUICKJS_IMPL_DEFAULT "ng")
set(BUDO_QUICKJS_NG_GIT_REPOSITORY "https://github.com/quickjs-ng/quickjs.git")
set(BUDO_QUICKJS_NG_GIT_TAG "3c8f3d68953955950074c41c6e4d999562ae82a7")

set(BUDO_MINIAUDIO_GIT_REPOSITORY "https://github.com/mackron/miniaudio.git")
set(BUDO_MINIAUDIO_GIT_TAG "9634bedb5b5a2ca38c1ee7108a9358a4e233f14d")

set(BUDO_LLAMACPP_GIT_REPOSITORY "https://github.com/ggml-org/llama.cpp.git")
set(BUDO_LLAMACPP_GIT_TAG "571d0d540df04f25298d0e159e520d9fc62ed121")

set(BUDO_SKIA_GIT_REPOSITORY "https://skia.googlesource.com/skia.git")
set(BUDO_SKIA_DESKTOP_GIT_TAG "255bd243276b9c7eec3c7d9cfda15941dd318fb5")
set(BUDO_SKIA_WEB_GIT_TAG "2c40548f24187b2bcb8d0cc73e3cde7275d4103d")
set(BUDO_SKIA_ANDROID_GIT_TAG "207ea96ce120b683a1e48ca6ad478d86b6db8cbe")

set(BUDO_EMSDK_VERSION "5.0.5")
set(BUDO_EMSDK_GIT_REPOSITORY "https://github.com/emscripten-core/emsdk.git")
set(BUDO_EMSDK_GIT_TAG "bafd64c26bdaf10bd829163d1575b50b759a72d8")

set(BUDO_APPIMAGETOOL_VERSION "1.9.1")
set(BUDO_APPIMAGETOOL_URL "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-@ARCH@.AppImage")
set(BUDO_APPIMAGETOOL_X86_64_URL_HASH "SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0")
set(BUDO_APPIMAGETOOL_AARCH64_URL_HASH "SHA256=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158")

set(BUDO_SDL2_WINDOWS_VERSION "2.30.3")
set(BUDO_SDL2_WINDOWS_URL
    "https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-devel-2.30.3-VC.zip")
set(BUDO_SDL2_WINDOWS_URL_HASH
    "SHA256=89999a6f3dc8f5e4fb03aaea6b4ad01747d9a81690f9f9a0674806e750483a38")

# SDL2 source, built as a static library for the standalone release binaries
# (scripts/setup-sdl2-static.sh).
set(BUDO_SDL2_SOURCE_VERSION "2.30.3")
set(BUDO_SDL2_SOURCE_URL "https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-2.30.3.tar.gz")
set(BUDO_SDL2_SOURCE_SHA256 "820440072f8f5b50188c1dae104f2ad25984de268785be40c41a099a510f0aec")

# zlib source, built as a static library on Windows (no system zlib there).
set(BUDO_ZLIB_SOURCE_VERSION "1.3.1")
set(BUDO_ZLIB_SOURCE_URL "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz")
set(BUDO_ZLIB_SOURCE_SHA256 "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23")

set(BUDO_ONNXRUNTIME_VERSION "1.21.0")
set(BUDO_ONNXRUNTIME_OSX_ARM64_URL_HASH "SHA256=5c3f2064ee97eb7774e87f396735c8eada7287734f1bb7847467ad30d4036115")
set(BUDO_ONNXRUNTIME_OSX_X86_64_URL_HASH "SHA256=8305afd2d75ee5702844a23b099d41885af30ad3d1b4cf3d8d795e3d8c1f9396")
set(BUDO_ONNXRUNTIME_LINUX_X64_URL_HASH "SHA256=7485c7e7aac6501b27e353dcbe068e45c61ab51fbaf598d13970dfae669d20bf")
set(BUDO_ONNXRUNTIME_WINDOWS_X64_URL_HASH "SHA256=5c07bb2805cd666dda75fa9bfa60e75f2f90d478b952298dd9d55c00740d81bf")
set(BUDO_ONNXRUNTIME_ANDROID_AAR_URL_HASH "SHA256=8b675e9680b8cc02dca706a5e3b4e35cc8506de5bdf206fdae68081cbd804414")

# Trusted release metadata for the support bundle consumed by installed Budo
# binaries. Update this only when publishing a newly reviewed support bundle
# (`make android-support-release`, then `make android-support-publish`).
set(BUDO_ANDROID_SUPPORT_BUNDLE_URL_HASH
    "SHA256=9f33273f4c2a42ceecc733af1aa2e5c51dc3972d55f4269406a874afa1f2add1")
# GitHub release that hosts that bundle, in BUDO_GITHUB_REPOSITORY.
set(BUDO_ANDROID_SUPPORT_BUNDLE_TAG "android-support-9f33273f4c2a")

# GitHub repository ("owner/name") whose releases host downloads such as the
# Android support bundle. CI passes its own repository; empty keeps local
# builds on the Budo website.
set(BUDO_GITHUB_REPOSITORY "")

# Archive of the private Android pack (private/android) that CI downloads to
# build Android packaging into the GitHub binaries. Its URL is the GitHub
# secret BUDO_ANDROID_PACK_URL; this hash pins its contents. Updated by
# `make android-pack` (see export-to-public/android-ci-build.md).
set(BUDO_ANDROID_PACK_COMMIT "789e385c181998c6ec9d8289179786effba1c7fb")
set(BUDO_ANDROID_PACK_SHA256 "069afd6069f20f2bdc2a7a90e1d6aabe044d162ed99a2ae8af220c14b22d8249")

set(BUDO_STB_GIT_REPOSITORY "https://github.com/nothings/stb.git")
set(BUDO_STB_GIT_TAG "f0569113c93ad095470c54bf34a17b36646bbbb5")
set(BUDO_STB_IMAGE_SHA256 "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3")

set(BUDO_QUICKJS_BELLARD_VERSION "2024-01-13")
set(BUDO_QUICKJS_BELLARD_URL
    "https://bellard.org/quickjs/quickjs-${BUDO_QUICKJS_BELLARD_VERSION}.tar.xz")
set(BUDO_QUICKJS_BELLARD_URL_HASH
    "SHA256=3c4bf8f895bfa54beb486c8d1218112771ecfc5ac3be1036851ef41568212e03")

set(BUDO_LUA_VERSION "5.4.7")
set(BUDO_LUA_URL "https://www.lua.org/ftp/lua-${BUDO_LUA_VERSION}.tar.gz")
set(BUDO_LUA_URL_HASH
    "SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30")

set(BUDO_SQLITE_VERSION "3450100")
set(BUDO_SQLITE_URL
    "https://www.sqlite.org/2024/sqlite-amalgamation-${BUDO_SQLITE_VERSION}.zip")
set(BUDO_SQLITE_URL_HASH
    "SHA256=5592243caf28b2cdef41e6ab58d25d653dfc53deded8450eb66072c929f030c4")

set(BUDO_WASMTIME_VERSION "26.0.1")
set(BUDO_WASMTIME_AARCH64_MACOS_URL_HASH
    "SHA256=25080ef6fa70a2b41ab5ccd74361d2cd3c111b786ee78db7fad866fc6a93144a")
set(BUDO_WASMTIME_X86_64_MACOS_URL_HASH
    "SHA256=4b32be08f04016adba20c48cc4a8ec9149020914ac7f75265015d52fe2c83f72")
set(BUDO_WASMTIME_X86_64_LINUX_URL_HASH
    "SHA256=08afc2baf0e3b94d0bcb45310cbc0bd68523984badf4bb5b2e91691e7f4b6040")
set(BUDO_WASMTIME_X86_64_WINDOWS_URL_HASH
    "SHA256=4d019610025b6e4280a79d8152b16f68b5a1d9652acb5e88bc3cc86483ba4191")

function(budo_report_dependency_lock consumer)
    foreach(commit_var
            BUDO_QUICKJS_NG_GIT_TAG
            BUDO_MINIAUDIO_GIT_TAG
            BUDO_LLAMACPP_GIT_TAG
            BUDO_SKIA_DESKTOP_GIT_TAG
            BUDO_SKIA_WEB_GIT_TAG
            BUDO_SKIA_ANDROID_GIT_TAG
            BUDO_EMSDK_GIT_TAG
            BUDO_STB_GIT_TAG)
        string(LENGTH "${${commit_var}}" commit_length)
        if(NOT commit_length EQUAL 40 OR NOT "${${commit_var}}" MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "Dependency lock ${commit_var} must be a full immutable Git commit")
        endif()
    endforeach()
    foreach(hash_var
            BUDO_QUICKJS_BELLARD_URL_HASH
            BUDO_LUA_URL_HASH
            BUDO_SQLITE_URL_HASH
            BUDO_ONNXRUNTIME_OSX_ARM64_URL_HASH
            BUDO_ONNXRUNTIME_OSX_X86_64_URL_HASH
            BUDO_ONNXRUNTIME_LINUX_X64_URL_HASH
            BUDO_ONNXRUNTIME_WINDOWS_X64_URL_HASH
            BUDO_ONNXRUNTIME_ANDROID_AAR_URL_HASH
            BUDO_ANDROID_SUPPORT_BUNDLE_URL_HASH
            BUDO_APPIMAGETOOL_X86_64_URL_HASH
            BUDO_APPIMAGETOOL_AARCH64_URL_HASH
            BUDO_SDL2_WINDOWS_URL_HASH
            BUDO_WASMTIME_AARCH64_MACOS_URL_HASH
            BUDO_WASMTIME_X86_64_MACOS_URL_HASH
            BUDO_WASMTIME_X86_64_LINUX_URL_HASH
            BUDO_WASMTIME_X86_64_WINDOWS_URL_HASH)
        string(LENGTH "${${hash_var}}" hash_length)
        if(NOT hash_length EQUAL 71 OR NOT "${${hash_var}}" MATCHES "^SHA256=[0-9a-f]+$")
            message(FATAL_ERROR "Dependency lock ${hash_var} must contain a SHA-256 digest")
        endif()
    endforeach()
    if(NOT BUDO_QUICKJS_IMPL_DEFAULT STREQUAL "ng")
        message(FATAL_ERROR "The project-wide default QuickJS implementation must be ng")
    endif()
    message(STATUS
        "Budo dependency lock v${BUDO_DEPENDENCY_LOCK_VERSION} (${consumer}): "
        "QuickJS=${BUDO_QUICKJS_IMPL_DEFAULT}@${BUDO_QUICKJS_NG_GIT_TAG}; "
        "miniaudio=${BUDO_MINIAUDIO_GIT_TAG}; llama.cpp=${BUDO_LLAMACPP_GIT_TAG}; "
        "Lua=${BUDO_LUA_VERSION}; "
        "SQLite=${BUDO_SQLITE_VERSION}; ONNX Runtime=${BUDO_ONNXRUNTIME_VERSION}; "
        "Wasmtime=${BUDO_WASMTIME_VERSION}")
endfunction()

function(budo_verify_locked_archive archive expected_hash)
    if(NOT EXISTS "${archive}")
        return()
    endif()
    string(REGEX REPLACE "^[Ss][Hh][Aa]256=" "" expected_sha256 "${expected_hash}")
    file(SHA256 "${archive}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR
            "Dependency archive hash mismatch: ${archive}\n"
            "Expected SHA-256: ${expected_sha256}\n"
            "Actual SHA-256:   ${actual_sha256}")
    endif()
endfunction()
