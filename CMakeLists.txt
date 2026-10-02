cmake_minimum_required(VERSION 3.20)
project(budo C CXX)

include(CMakePackageConfigHelpers)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BudoDependencyLock.cmake")
budo_report_dependency_lock("root")

set(BUDO_VERSION_STRING "0.4.3")
set(BUDO_NATIVE_SDK_BUILD_ID "development" CACHE STRING "Native SDK build identity")
set(BUDO_NATIVE_SDK_RELEASE_METADATA_URL "" CACHE STRING
    "Immutable native SDK release metadata URL")
set(BUDO_NATIVE_SDK_RELEASE_METADATA_SHA256 "" CACHE STRING
    "Independently published SHA-256 of native SDK release metadata")
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    # /Zc:__cplusplus: report the real C++ standard (MSVC otherwise says
    # 199711L), which dependencies test, e.g. llama.cpp's u8"" handling under
    # C++20. /utf-8: sources are UTF-8, not the machine's legacy code page.
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>" /utf-8)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Python3 is required to embed resources into the binary.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Platform detection
if(WIN32)
    set(SKIA_PLATFORM "win")
    set(SKIA_ARCH "x64")
elseif(APPLE)
    set(SKIA_PLATFORM "mac")
    execute_process(COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(ARCH STREQUAL "arm64")
        set(SKIA_ARCH "arm64")
    else()
        set(SKIA_ARCH "x64")
    endif()
else()
    set(SKIA_PLATFORM "linux")
    set(SKIA_ARCH "x64")
endif()

if(APPLE)
    if(SKIA_ARCH STREQUAL "arm64")
        set(BUDO_NATIVE_TARGET_TUPLE "macos-arm64-apple")
        set(BUDO_NATIVE_PROCESSOR_REGEX "^(arm64|aarch64)$")
    else()
        set(BUDO_NATIVE_TARGET_TUPLE "macos-x86_64-apple")
        set(BUDO_NATIVE_PROCESSOR_REGEX "^(x86_64|AMD64)$")
    endif()
    set(BUDO_NATIVE_COMPILER_FAMILY "AppleClang")
    set(BUDO_NATIVE_COMPILER_REGEX "^AppleClang$")
    set(BUDO_NATIVE_PLATFORM_BASELINE "macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    set(BUDO_NATIVE_APPLICATION_PLATFORM_SETTINGS "")
elseif(WIN32)
    set(BUDO_NATIVE_TARGET_TUPLE "windows-x86_64-msvc")
    set(BUDO_NATIVE_PROCESSOR_REGEX "^(x86_64|AMD64)$")
    set(BUDO_NATIVE_COMPILER_FAMILY "MSVC v143 / dynamic CRT")
    set(BUDO_NATIVE_COMPILER_REGEX "^MSVC$")
    set(BUDO_NATIVE_PLATFORM_BASELINE "Windows 10; MSVC v143; /MD")
    set(BUDO_NATIVE_APPLICATION_PLATFORM_SETTINGS "  set_property(TARGET \"\${target}\" PROPERTY MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL\")")
else()
    set(BUDO_NATIVE_TARGET_TUPLE "linux-x86_64-gnu")
    set(BUDO_NATIVE_PROCESSOR_REGEX "^(x86_64|AMD64)$")
    set(BUDO_NATIVE_COMPILER_FAMILY "GNU or Clang; glibc ABI")
    set(BUDO_NATIVE_COMPILER_REGEX "^(GNU|Clang)$")
    set(BUDO_NATIVE_PLATFORM_BASELINE "glibc 2.31")
    set(BUDO_NATIVE_APPLICATION_PLATFORM_SETTINGS "  set_property(TARGET \"\${target}\" PROPERTY BUILD_RPATH \"\\$ORIGIN\")")
endif()
set(BUDO_NATIVE_TARGET_TUPLE "${BUDO_NATIVE_TARGET_TUPLE}" CACHE INTERNAL "Native SDK target tuple")
set(BUDO_NATIVE_PLATFORM_BASELINE "${BUDO_NATIVE_PLATFORM_BASELINE}" CACHE INTERNAL "Native SDK platform baseline")
set(BUDO_NATIVE_COMPILER_FAMILY "${BUDO_NATIVE_COMPILER_FAMILY}" CACHE INTERNAL "Native SDK compiler family")

# Skia configuration - using skia4delphi prebuilt binaries
# You can also build Skia from source: https://skia.org/docs/user/build/
set(SKIA_VERSION "6.1.0")
set(SKIA_URL "https://github.com/nicoowr/skia-builder/releases/download/v${SKIA_VERSION}/skia-${SKIA_PLATFORM}-${SKIA_ARCH}.tar.gz")
set(SKIA_DIR "${CMAKE_BINARY_DIR}/skia")

include(FetchContent)

option(ENABLE_LLAMACPP "Enable local GGUF inference through llama.cpp" ON)
option(ENABLE_LLAMACPP_CUDA "Build llama.cpp CUDA as a dynamic ggml backend" OFF)
option(ENABLE_LLAMACPP_VULKAN "Build llama.cpp Vulkan as a dynamic ggml backend" OFF)
if((ENABLE_LLAMACPP_CUDA OR ENABLE_LLAMACPP_VULKAN) AND NOT ENABLE_LLAMACPP)
    message(FATAL_ERROR "CUDA/Vulkan llama.cpp backends require ENABLE_LLAMACPP=ON")
endif()
if(ENABLE_LLAMACPP)
    set(BUDO_LLAMACPP_DYNAMIC_BACKENDS OFF)
    if(ENABLE_LLAMACPP_CUDA OR ENABLE_LLAMACPP_VULKAN)
        set(BUDO_LLAMACPP_DYNAMIC_BACKENDS ON)
    endif()
    if(ENABLE_LLAMACPP_CUDA)
        include(CheckLanguage)
        check_language(CUDA)
        if(NOT CMAKE_CUDA_COMPILER)
            message(FATAL_ERROR
                "ENABLE_LLAMACPP_CUDA=ON requires the CUDA toolkit and nvcc")
        endif()
    endif()
    if(ENABLE_LLAMACPP_VULKAN)
        find_package(Vulkan REQUIRED)
    endif()
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS ${BUDO_LLAMACPP_DYNAMIC_BACKENDS}
        CACHE BOOL "Build shared llama.cpp libraries for backend plugins" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_MTMD OFF CACHE BOOL "" FORCE)
    set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
    set(GGML_CPU ON CACHE BOOL "" FORCE)
    set(GGML_METAL ${APPLE} CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ${APPLE} CACHE BOOL "" FORCE)
    set(GGML_BACKEND_DL ${BUDO_LLAMACPP_DYNAMIC_BACKENDS} CACHE BOOL "" FORCE)
    if(BUDO_LLAMACPP_DYNAMIC_BACKENDS)
        set(GGML_BACKEND_DIR "." CACHE PATH "" FORCE)
    else()
        set(GGML_BACKEND_DIR "" CACHE PATH "" FORCE)
    endif()
    set(GGML_CUDA ${ENABLE_LLAMACPP_CUDA} CACHE BOOL "" FORCE)
    set(GGML_VULKAN ${ENABLE_LLAMACPP_VULKAN} CACHE BOOL "" FORCE)
    FetchContent_Declare(
        llamacpp
        GIT_REPOSITORY ${BUDO_LLAMACPP_GIT_REPOSITORY}
        GIT_TAG        ${BUDO_LLAMACPP_GIT_TAG}
        GIT_SHALLOW    FALSE
    )
    FetchContent_MakeAvailable(llamacpp)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "Build shared libraries" FORCE)
endif()

# Decode-only audio file support (WAV, MP3, Ogg Vorbis, FLAC via miniaudio).
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY ${BUDO_MINIAUDIO_GIT_REPOSITORY}
    GIT_TAG        ${BUDO_MINIAUDIO_GIT_TAG}
)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    message(STATUS "Downloading miniaudio...")
    FetchContent_Populate(miniaudio)
endif()

# Option to use system Skia or download
option(USE_SYSTEM_SKIA "Use system-installed Skia instead of downloading" OFF)
set(SKIA_PATH "" CACHE PATH "Path to pre-built Skia installation")

if(SKIA_PATH)
    set(SKIA_DIR ${SKIA_PATH})
    if(NOT EXISTS "${SKIA_DIR}")
        message(FATAL_ERROR "SKIA_PATH specified but directory does not exist: ${SKIA_DIR}")
    endif()
    message(STATUS "Using Skia from: ${SKIA_DIR}")
elseif(USE_SYSTEM_SKIA)
    find_path(SKIA_DIR include/core/SkCanvas.h
        HINTS /usr/local /opt/skia $ENV{SKIA_DIR}
    )
    if(NOT SKIA_DIR)
        message(FATAL_ERROR "USE_SYSTEM_SKIA specified but Skia not found in system paths")
    endif()
    message(STATUS "Using system Skia from: ${SKIA_DIR}")
else()
    # Check if Skia exists in common locations
    set(POSSIBLE_SKIA_DIRS
        "${CMAKE_SOURCE_DIR}/third_party/skia"
        "${CMAKE_SOURCE_DIR}/skia"
        "$ENV{SKIA_DIR}"
    )
    
    foreach(DIR ${POSSIBLE_SKIA_DIRS})
        if(EXISTS "${DIR}/include/core/SkCanvas.h" OR EXISTS "${DIR}/out/Release/libskia.a")
            set(SKIA_DIR ${DIR})
            message(STATUS "Found Skia at: ${SKIA_DIR}")
            break()
        endif()
    endforeach()
    
    if(NOT SKIA_DIR OR NOT EXISTS "${SKIA_DIR}")
        message(STATUS "")
        message(STATUS "=== SKIA NOT FOUND ===")
        message(STATUS "")
        message(STATUS "Please build or download Skia first:")
        message(STATUS "")
        message(STATUS "Option 1: Run the setup script")
        message(STATUS "  ./scripts/setup-skia.sh")
        message(STATUS "")
        message(STATUS "Option 2: Build Skia manually")
        message(STATUS "  git clone https://skia.googlesource.com/skia.git third_party/skia")
        message(STATUS "  cd third_party/skia && python3 tools/git-sync-deps")
        message(STATUS "  bin/gn gen out/Release --args='is_official_build=true'")
        message(STATUS "  ninja -C out/Release")
        message(STATUS "")
        message(STATUS "Option 3: Specify existing Skia path")
        message(STATUS "  cmake -DSKIA_PATH=/path/to/skia ..")
        message(STATUS "")
        message(FATAL_ERROR "Skia is required to build this project.")
    endif()
endif()

# QuickJS runtime. Use QuickJS-NG by default, or opt into Bellard's official
# tarball with: -DBUDO_QUICKJS_IMPL=bellard
set(BUDO_QUICKJS_IMPL "${BUDO_QUICKJS_IMPL_DEFAULT}" CACHE STRING "QuickJS implementation: bellard or ng")
set_property(CACHE BUDO_QUICKJS_IMPL PROPERTY STRINGS bellard ng)

if(BUDO_QUICKJS_IMPL STREQUAL "ng")
    FetchContent_Declare(
        quickjs_ng
        GIT_REPOSITORY ${BUDO_QUICKJS_NG_GIT_REPOSITORY}
        GIT_TAG        ${BUDO_QUICKJS_NG_GIT_TAG}
    )

    FetchContent_GetProperties(quickjs_ng)
    if(NOT quickjs_ng_POPULATED)
        message(STATUS "Downloading QuickJS-NG (${BUDO_QUICKJS_NG_GIT_TAG})...")
        FetchContent_Populate(quickjs_ng)
    endif()
    set(quickjs_SOURCE_DIR ${quickjs_ng_SOURCE_DIR})
    set(QUICKJS_VERSION_DEF "quickjs-ng")
    set(QUICKJS_EXTRA_DEFS QUICKJS_NG_BUILD QJS_BUILD_LIBC)
    set(QUICKJS_SOURCES
        ${quickjs_SOURCE_DIR}/quickjs.c
        ${quickjs_SOURCE_DIR}/libregexp.c
        ${quickjs_SOURCE_DIR}/libunicode.c
        ${quickjs_SOURCE_DIR}/dtoa.c
        ${quickjs_SOURCE_DIR}/quickjs-libc.c
    )
elseif(BUDO_QUICKJS_IMPL STREQUAL "bellard")
    FetchContent_Declare(
        quickjs_fallback
        URL      ${BUDO_QUICKJS_BELLARD_URL}
        URL_HASH ${BUDO_QUICKJS_BELLARD_URL_HASH}
    )

    FetchContent_GetProperties(quickjs_fallback)
    if(NOT quickjs_fallback_POPULATED)
        message(STATUS "Downloading QuickJS...")
        FetchContent_Populate(quickjs_fallback)
    endif()
    set(quickjs_SOURCE_DIR ${quickjs_fallback_SOURCE_DIR})
    set(QUICKJS_VERSION_DEF "${BUDO_QUICKJS_BELLARD_VERSION}")

    # Remove 'version' file that conflicts with C++ standard library <version> header
    file(REMOVE ${quickjs_SOURCE_DIR}/version)

    set(QUICKJS_SOURCES
        ${quickjs_SOURCE_DIR}/quickjs.c
        ${quickjs_SOURCE_DIR}/libregexp.c
        ${quickjs_SOURCE_DIR}/libunicode.c
        ${quickjs_SOURCE_DIR}/cutils.c
        ${quickjs_SOURCE_DIR}/quickjs-libc.c
    )

    # Add libbf.c if it exists (for BigInt support)
    if(EXISTS ${quickjs_SOURCE_DIR}/libbf.c)
        list(APPEND QUICKJS_SOURCES ${quickjs_SOURCE_DIR}/libbf.c)
        set(QUICKJS_BIGNUM_DEF CONFIG_BIGNUM)
    endif()
else()
    message(FATAL_ERROR "Unsupported BUDO_QUICKJS_IMPL='${BUDO_QUICKJS_IMPL}'. Use 'bellard' or 'ng'.")
endif()

add_library(quickjs STATIC ${QUICKJS_SOURCES})
target_include_directories(quickjs PRIVATE ${quickjs_SOURCE_DIR})
target_include_directories(quickjs SYSTEM INTERFACE ${quickjs_SOURCE_DIR})
target_compile_definitions(quickjs PRIVATE
    CONFIG_VERSION="${QUICKJS_VERSION_DEF}"
    ${QUICKJS_BIGNUM_DEF}
    ${QUICKJS_EXTRA_DEFS}
)

if(NOT WIN32)
    target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
    target_link_libraries(quickjs PRIVATE m dl pthread)
endif()

if(WIN32)
    # Matches QuickJS-NG's own Windows build: its cutils.h includes winsock2.h,
    # which clashes with a full <windows.h>, and its atomics need MSVC's opt-in
    # C11 atomics.
    target_compile_definitions(quickjs PRIVATE
        WIN32
        _WIN32
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601
    )
    if(MSVC)
        target_compile_options(quickjs PRIVATE /experimental:c11atomics)
    endif()
endif()

file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-impl" "${BUDO_QUICKJS_IMPL}\n")
file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-source-dir" "${quickjs_SOURCE_DIR}\n")
if(BUDO_QUICKJS_IMPL STREQUAL "ng")
    file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-revision" "${BUDO_QUICKJS_NG_GIT_TAG}\n")
else()
    file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-revision" "${BUDO_QUICKJS_BELLARD_URL_HASH}\n")
endif()
message(STATUS "Using QuickJS implementation: ${BUDO_QUICKJS_IMPL} (${quickjs_SOURCE_DIR})")

# ==================================================
# Lua Configuration
# ==================================================
# Lua 5.4 for Lua runtime support
set(LUA_VERSION "${BUDO_LUA_VERSION}")
set(LUA_DIR "${CMAKE_BINARY_DIR}/lua")

FetchContent_Declare(
    lua
    URL      "${BUDO_LUA_URL}"
    URL_HASH "${BUDO_LUA_URL_HASH}"
)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    message(STATUS "Downloading Lua ${LUA_VERSION}...")
    FetchContent_Populate(lua)
    set(lua_SOURCE_DIR ${lua_SOURCE_DIR})

    set(LUA_CORE_SOURCES
        ${lua_SOURCE_DIR}/src/lapi.c
        ${lua_SOURCE_DIR}/src/lcode.c
        ${lua_SOURCE_DIR}/src/lctype.c
        ${lua_SOURCE_DIR}/src/ldebug.c
        ${lua_SOURCE_DIR}/src/ldo.c
        ${lua_SOURCE_DIR}/src/ldump.c
        ${lua_SOURCE_DIR}/src/lfunc.c
        ${lua_SOURCE_DIR}/src/lgc.c
        ${lua_SOURCE_DIR}/src/llex.c
        ${lua_SOURCE_DIR}/src/lmem.c
        ${lua_SOURCE_DIR}/src/lobject.c
        ${lua_SOURCE_DIR}/src/lopcodes.c
        ${lua_SOURCE_DIR}/src/lparser.c
        ${lua_SOURCE_DIR}/src/lstate.c
        ${lua_SOURCE_DIR}/src/lstring.c
        ${lua_SOURCE_DIR}/src/ltable.c
        ${lua_SOURCE_DIR}/src/ltm.c
        ${lua_SOURCE_DIR}/src/lundump.c
        ${lua_SOURCE_DIR}/src/lvm.c
        ${lua_SOURCE_DIR}/src/lzio.c
        ${lua_SOURCE_DIR}/src/lauxlib.c
        ${lua_SOURCE_DIR}/src/lbaselib.c
        ${lua_SOURCE_DIR}/src/lcorolib.c
        ${lua_SOURCE_DIR}/src/ldblib.c
        ${lua_SOURCE_DIR}/src/liolib.c
        ${lua_SOURCE_DIR}/src/lmathlib.c
        ${lua_SOURCE_DIR}/src/loadlib.c
        ${lua_SOURCE_DIR}/src/loslib.c
        ${lua_SOURCE_DIR}/src/lstrlib.c
        ${lua_SOURCE_DIR}/src/ltablib.c
        ${lua_SOURCE_DIR}/src/lutf8lib.c
        ${lua_SOURCE_DIR}/src/linit.c
    )

    add_library(lua STATIC ${LUA_CORE_SOURCES})
    target_include_directories(lua SYSTEM INTERFACE ${lua_SOURCE_DIR}/src)
    target_include_directories(lua PRIVATE ${lua_SOURCE_DIR}/src)
    if(NOT WIN32)
        target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
        target_link_libraries(lua PRIVATE m)
    endif()
endif()

# ==================================================
# SQLite Configuration
# ==================================================
# SQLite amalgamation for persistence API
set(SQLITE_DIR "${CMAKE_BINARY_DIR}/sqlite")

if(NOT EXISTS "${SQLITE_DIR}/sqlite3.c")
    message(STATUS "Downloading SQLite amalgamation...")
    set(SQLITE_ARCHIVE "${CMAKE_BINARY_DIR}/sqlite-amalgamation.zip")
    if(NOT EXISTS "${SQLITE_ARCHIVE}")
        file(DOWNLOAD
            "${BUDO_SQLITE_URL}"
            "${SQLITE_ARCHIVE}"
            EXPECTED_HASH "${BUDO_SQLITE_URL_HASH}"
            SHOW_PROGRESS
        )
    endif()
    budo_verify_locked_archive("${SQLITE_ARCHIVE}" "${BUDO_SQLITE_URL_HASH}")
    file(MAKE_DIRECTORY "${SQLITE_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${SQLITE_ARCHIVE}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    # Copy the extracted contents into SQLITE_DIR. A rename fails on Windows
    # because SQLITE_DIR already exists (see the Wasmtime download).
    file(GLOB SQLITE_EXTRACTED "${CMAKE_BINARY_DIR}/sqlite-amalgamation-*")
    if(SQLITE_EXTRACTED)
        file(COPY "${SQLITE_EXTRACTED}/" DESTINATION "${SQLITE_DIR}")
        file(REMOVE_RECURSE "${SQLITE_EXTRACTED}")
    endif()
endif()

add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE_DIR})
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
)
if(NOT WIN32)
    target_link_libraries(sqlite3 PRIVATE dl pthread)
endif()

# ==================================================
# Wasmtime Configuration
# ==================================================
# Wasmtime C API for WebAssembly support
set(WASMTIME_VERSION "${BUDO_WASMTIME_VERSION}")
if(WIN32)
    set(WASMTIME_PLATFORM "x86_64-windows")
    set(WASMTIME_EXT "zip")
    set(WASMTIME_URL_HASH "${BUDO_WASMTIME_X86_64_WINDOWS_URL_HASH}")
elseif(APPLE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE WASMTIME_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(WASMTIME_ARCH STREQUAL "arm64")
        set(WASMTIME_PLATFORM "aarch64-macos")
        set(WASMTIME_URL_HASH "${BUDO_WASMTIME_AARCH64_MACOS_URL_HASH}")
    else()
        set(WASMTIME_PLATFORM "x86_64-macos")
        set(WASMTIME_URL_HASH "${BUDO_WASMTIME_X86_64_MACOS_URL_HASH}")
    endif()
    set(WASMTIME_EXT "tar.xz")
else()
    set(WASMTIME_PLATFORM "x86_64-linux")
    set(WASMTIME_EXT "tar.xz")
    set(WASMTIME_URL_HASH "${BUDO_WASMTIME_X86_64_LINUX_URL_HASH}")
endif()

if(NOT WASMTIME_URL_HASH)
    message(FATAL_ERROR "Wasmtime ${WASMTIME_PLATFORM} archive has no trusted SHA-256 in the dependency lock")
endif()

set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.${WASMTIME_EXT}")
set(WASMTIME_DIR "${CMAKE_BINARY_DIR}/wasmtime")

# Check if we need to download Wasmtime
if(NOT EXISTS "${WASMTIME_DIR}/include/wasmtime.h")
    message(STATUS "Downloading Wasmtime C API v${WASMTIME_VERSION}...")
    
    set(WASMTIME_ARCHIVE "${CMAKE_BINARY_DIR}/wasmtime.${WASMTIME_EXT}")
    
    if(NOT EXISTS "${WASMTIME_ARCHIVE}")
        file(DOWNLOAD "${WASMTIME_URL}" "${WASMTIME_ARCHIVE}"
            EXPECTED_HASH "${WASMTIME_URL_HASH}" SHOW_PROGRESS)
    endif()
    budo_verify_locked_archive("${WASMTIME_ARCHIVE}" "${WASMTIME_URL_HASH}")
    
    # Extract the archive
    file(MAKE_DIRECTORY "${WASMTIME_DIR}")
    if(WIN32)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${WASMTIME_ARCHIVE}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    else()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf "${WASMTIME_ARCHIVE}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    endif()
    
    # Copy the extracted contents into WASMTIME_DIR. A rename fails on Windows
    # because WASMTIME_DIR already exists, and freshly extracted files can be
    # held open briefly there (antivirus, indexer).
    file(GLOB WASMTIME_EXTRACTED "${CMAKE_BINARY_DIR}/wasmtime-v${WASMTIME_VERSION}-*-c-api")
    if(WASMTIME_EXTRACTED)
        file(COPY "${WASMTIME_EXTRACTED}/" DESTINATION "${WASMTIME_DIR}")
        file(REMOVE_RECURSE "${WASMTIME_EXTRACTED}")
    endif()
endif()

# Find Wasmtime library (prefer static to avoid dylib/rpath issues on install)
if(WIN32)
    find_library(WASMTIME_LIBRARY NAMES wasmtime.dll.lib wasmtime
        PATHS ${WASMTIME_DIR}/lib
        NO_DEFAULT_PATH
    )
else()
    find_library(WASMTIME_LIBRARY NAMES libwasmtime.a
        PATHS ${WASMTIME_DIR}/lib
        NO_DEFAULT_PATH
    )
endif()

message(STATUS "Wasmtime library: ${WASMTIME_LIBRARY}")
message(STATUS "Wasmtime include: ${WASMTIME_DIR}/include")

if(UNIX AND NOT APPLE)
    # The static Wasmtime archive uses libm directly (Cranelift, Tokio, etc.).
    # Append it so every Wasmtime consumer gets the required link order.
    list(APPEND WASMTIME_LIBRARY m)
endif()

# Find SDL2
find_package(SDL2 REQUIRED)

# Budo includes SDL as <SDL2/SDL.h> (the Homebrew/distribution layout). The
# official Windows development package puts the headers directly in include/,
# so expose them under an SDL2/ directory in the build tree.
if(SDL2_INCLUDE_DIRS)
    list(GET SDL2_INCLUDE_DIRS 0 BUDO_SDL2_HEADER_DIR)
    if(EXISTS "${BUDO_SDL2_HEADER_DIR}/SDL.h" AND NOT EXISTS "${BUDO_SDL2_HEADER_DIR}/SDL2/SDL.h")
        set(BUDO_SDL2_SHIM_DIR "${CMAKE_BINARY_DIR}/sdl2-include")
        file(COPY "${BUDO_SDL2_HEADER_DIR}/" DESTINATION "${BUDO_SDL2_SHIM_DIR}/SDL2"
            FILES_MATCHING PATTERN "*.h")
        list(APPEND SDL2_INCLUDE_DIRS "${BUDO_SDL2_SHIM_DIR}")
        if(TARGET SDL2::SDL2)
            set_property(TARGET SDL2::SDL2 APPEND PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES "${BUDO_SDL2_SHIM_DIR}")
        endif()
        message(STATUS "SDL2 headers exposed as <SDL2/...> from ${BUDO_SDL2_SHIM_DIR}")
    endif()
endif()

# MSVC lacks POSIX pieces Budo's portable code uses (<dirent.h>, <libgen.h>,
# <unistd.h>, PATH_MAX, S_ISDIR/S_ISREG). Directory-scoped on purpose: it only
# reaches targets created from here on, i.e. Budo's own code, not the
# third-party targets above.
if(MSVC)
    set(BUDO_MSVC_COMPAT_DIR "${CMAKE_SOURCE_DIR}/src/compat/msvc")
    include_directories(BEFORE "${BUDO_MSVC_COMPAT_DIR}")
    add_compile_options("/FI${BUDO_MSVC_COMPAT_DIR}/budo_msvc_compat.h")
    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)
endif()

get_target_property(BUDO_SDK_SDL_LIBRARY SDL2::SDL2 IMPORTED_LOCATION)
if(NOT BUDO_SDK_SDL_LIBRARY)
    get_target_property(BUDO_SDK_SDL_LIBRARY SDL2::SDL2 IMPORTED_LOCATION_RELEASE)
endif()
if(NOT BUDO_SDK_SDL_LIBRARY AND SDL2_LIBRARIES)
    list(GET SDL2_LIBRARIES 0 BUDO_SDK_SDL_LIBRARY)
endif()
if(NOT EXISTS "${BUDO_SDK_SDL_LIBRARY}")
    message(FATAL_ERROR "Cannot resolve the SDL2 library required for native SDK production")
endif()
file(REAL_PATH "${BUDO_SDK_SDL_LIBRARY}" BUDO_SDK_SDL_LIBRARY)
get_filename_component(BUDO_SDK_SDL_NAME "${BUDO_SDK_SDL_LIBRARY}" NAME)
if(WIN32)
    set(BUDO_SDK_SDL_RELATIVE_PATH "bin/${BUDO_SDK_SDL_NAME}")
else()
    set(BUDO_SDK_SDL_RELATIVE_PATH "lib/budo/deps/${BUDO_SDK_SDL_NAME}")
endif()
if(APPLE)
    execute_process(COMMAND otool -D "${BUDO_SDK_SDL_LIBRARY}"
        OUTPUT_VARIABLE BUDO_SDK_SDL_OTOOL OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" BUDO_SDK_SDL_OTOOL_LINES "${BUDO_SDK_SDL_OTOOL}")
    list(GET BUDO_SDK_SDL_OTOOL_LINES -1 BUDO_SDK_SDL_INSTALL_NAME)
    set(BUDO_NATIVE_APPLICATION_PLATFORM_SETTINGS
        "  set_property(TARGET \"\${target}\" PROPERTY OSX_DEPLOYMENT_TARGET \"${CMAKE_OSX_DEPLOYMENT_TARGET}\")\n  add_custom_command(TARGET \"\${target}\" POST_BUILD COMMAND install_name_tool -change \"${BUDO_SDK_SDL_INSTALL_NAME}\" \"@loader_path/${BUDO_SDK_SDL_NAME}\" \"$<TARGET_FILE:\${target}>\" VERBATIM)")
endif()

# Find Skia library file (handle different directory structures)
if(WIN32)
    find_file(SKIA_LIBRARY 
        NAMES skia.lib libskia.a
        PATHS 
            ${SKIA_DIR}/lib 
            ${SKIA_DIR}/out/Release
            ${SKIA_DIR}/out/Static
            ${SKIA_DIR}
        NO_DEFAULT_PATH
    )
else()
    find_file(SKIA_LIBRARY 
        NAMES libskia.a
        PATHS 
            ${SKIA_DIR}/lib 
            ${SKIA_DIR}/out/Release
            ${SKIA_DIR}/out/Static
            ${SKIA_DIR}
        NO_DEFAULT_PATH
    )
endif()

if(NOT SKIA_LIBRARY)
    message(WARNING "Could not find Skia library in ${SKIA_DIR}")
    message(STATUS "Expected locations:")
    message(STATUS "  ${SKIA_DIR}/lib/libskia.a")
    message(STATUS "  ${SKIA_DIR}/out/Release/libskia.a")
    set(SKIA_LIBRARY "${SKIA_DIR}/lib/libskia.a")
endif()

message(STATUS "Using Skia library: ${SKIA_LIBRARY}")

# Create Skia imported library
add_library(skia STATIC IMPORTED)
set_target_properties(skia PROPERTIES
    IMPORTED_LOCATION "${SKIA_LIBRARY}"
)
get_filename_component(BUDO_SDK_SKIA_NAME "${SKIA_LIBRARY}" NAME)

# Find Skia include directory
find_path(SKIA_INCLUDE_DIR 
    NAMES core/SkCanvas.h
    PATHS 
        ${SKIA_DIR}/include
        ${SKIA_DIR}
    NO_DEFAULT_PATH
)

if(NOT SKIA_INCLUDE_DIR)
    set(SKIA_INCLUDE_DIR "${SKIA_DIR}/include")
endif()

message(STATUS "Skia include directory: ${SKIA_INCLUDE_DIR}")

# Source files
set(SOURCES
    src/core/entry.c
    src/core/main.c
    src/core/file_watcher.c
    src/core/app_entrypoint.c
    src/core/app_metadata.c
    src/core/json_util.c
    src/core/embedded_resource.c
    src/core/budo_init.c
    src/core/web_export.c
    src/core/web_server.c
    src/core/ts_strip.c
    src/core/capabilities.c
    src/core/js_capabilities_bindings.c
    src/core/lua_capabilities_bindings.c
    src/core/wasm_capabilities_bindings.c
    src/core/managed_basics.c
    src/core/managed_runtime.c
    src/core/managed_subsystems.c
    src/graphics/js_core_bindings.c
    src/graphics/js_canvas_bindings.c
    src/graphics/js_gl_bindings.c
    src/graphics/wasm_canvas_bindings.c
    src/graphics/wasm_gl_bindings.c
    src/graphics/wasm_transform_bindings.c
    src/graphics/lua_canvas_bindings.c
    src/graphics/lua_gl_bindings.c
    src/graphics/lua_transform_bindings.c
    src/graphics/color_util.c
    src/math/math_wrapper.c
    src/math/js_math_bindings.c
    src/math/lua_math_bindings.c
    src/math/wasm_math_bindings.c
    src/audio/audio_wrapper.c
    src/audio/audio_service.c
    src/audio/audio_decoder.c
    src/audio/js_audio_bindings.c
    src/audio/wasm_audio_bindings.c
    src/audio/lua_audio_bindings.c
    src/midi/midi_wrapper.c
    src/midi/midi_service.c
    src/midi/midi_topology.c
    src/midi/js_midi_bindings.c
    src/midi/wasm_midi_bindings.c
    src/midi/lua_midi_bindings.c
    src/midi/rtpmidi.c
    src/sqlite/sqlite_wrapper.c
    src/sqlite/sqlite_service.c
    src/sqlite/js_sqlite_bindings.c
    src/sqlite/wasm_sqlite_bindings.c
    src/sqlite/lua_sqlite_bindings.c
    src/file/file_wrapper.c
    src/file/file_service.c
    src/file/js_file_bindings.c
    src/file/wasm_file_bindings.c
    src/file/lua_file_bindings.c
    src/network/network_wrapper.c
    src/network/network_policy.c
    src/network/network_request_core.c
    src/network/network_desktop_transport.c
    src/network/network_service.c
    src/network/js_network_bindings.c
    src/network/lua_network_bindings.c
    src/network/wasm_network_bindings.c
    src/network/udp_wrapper.c
    src/network/js_udp_bindings.c
    src/network/wasm_udp_bindings.c
    src/network/lua_udp_bindings.c
    src/magneto/magneto_wrapper.c
    src/magneto/magneto_service.c
    src/magneto/js_magneto_bindings.c
    src/magneto/wasm_magneto_bindings.c
    src/magneto/lua_magneto_bindings.c
    src/device/device_wrapper.c
    src/device/device_service.c
    src/device/js_device_bindings.c
    src/device/lua_device_bindings.c
    src/device/wasm_device_bindings.c
    src/wasm_util/wasm_memory.c
)

# Android APK/AAB packaging is supplied by the private Budo Pro feature pack.
# Public builds keep the CLI commands visible, but link an unavailable stub.
set(BUDO_ANDROID_PACK_ROOT "${CMAKE_SOURCE_DIR}/private/android" CACHE PATH
    "Path to the private Budo Android feature pack")
set(BUDO_ANDROID_PACK_RESOLVED_CMAKE
    "${CMAKE_BINARY_DIR}/generated/android-feature-pack.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/scripts/resolve-feature-pack.py"
            --repo "${CMAKE_SOURCE_DIR}"
            --pack-root "${BUDO_ANDROID_PACK_ROOT}"
            --format cmake
            --output "${BUDO_ANDROID_PACK_RESOLVED_CMAKE}"
    RESULT_VARIABLE BUDO_ANDROID_PACK_RESOLVE_RESULT
    OUTPUT_VARIABLE BUDO_ANDROID_PACK_RESOLVE_OUTPUT
    ERROR_VARIABLE BUDO_ANDROID_PACK_RESOLVE_ERROR)
if(NOT BUDO_ANDROID_PACK_RESOLVE_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Android feature-pack resolution failed:\n${BUDO_ANDROID_PACK_RESOLVE_ERROR}")
endif()
include("${BUDO_ANDROID_PACK_RESOLVED_CMAKE}")
if(BUDO_ANDROID_PACK_AVAILABLE)
    file(REAL_PATH "${BUDO_ANDROID_PACK_ROOT}/feature-pack.json"
         BUDO_ANDROID_PACK_REQUESTED_MANIFEST)
    if(NOT BUDO_ANDROID_PACK_MANIFEST STREQUAL
           BUDO_ANDROID_PACK_REQUESTED_MANIFEST)
        message(FATAL_ERROR
            "Android feature-pack resolver returned an unexpected manifest: ${BUDO_ANDROID_PACK_MANIFEST}")
    endif()
endif()

option(BUDO_ENABLE_ANDROID_PACKAGING
       "Enable Budo Pro Android APK/AAB packaging"
       ${BUDO_ANDROID_PACK_AVAILABLE})

if(BUDO_ENABLE_ANDROID_PACKAGING)
    if(NOT BUDO_ANDROID_PACK_AVAILABLE)
        message(FATAL_ERROR
            "BUDO_ENABLE_ANDROID_PACKAGING=ON but no compatible Android feature-pack manifest was found at ${BUDO_ANDROID_PACK_ROOT}/feature-pack.json")
    endif()
    message(STATUS
        "Budo Pro Android packaging ${BUDO_ANDROID_PACK_FEATURE_VERSION} enabled: ${BUDO_ANDROID_PACK_MANIFEST}")
    list(APPEND SOURCES "${BUDO_ANDROID_PACK_PACKAGE_SOURCE}")
else()
    message(STATUS "Budo Pro Android packaging unavailable in this checkout")
    list(APPEND SOURCES src/core/android_package_unavailable.c)
endif()

# ==================================================
# ONNX Runtime (optional — controlled by ENABLE_NEURAL)
# ==================================================
option(ENABLE_NEURAL "Enable ONNX Runtime neural inference API" ON)

if(ENABLE_NEURAL)
    set(ORT_VERSION "1.21.0")
    set(ORT_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")

    if(NOT EXISTS "${ORT_DIR}/include/onnxruntime_c_api.h")
        message(STATUS "")
        message(STATUS "=== ONNX Runtime NOT FOUND ===")
        message(STATUS "Run:  ./scripts/setup-onnx.sh")
        message(STATUS "Or:   cmake -DENABLE_NEURAL=OFF ..")
        message(STATUS "")
        message(WARNING "ONNX Runtime not found; disabling neural API (ENABLE_NEURAL=OFF)")
        set(ENABLE_NEURAL OFF)
    endif()
endif()

if(ENABLE_LLAMACPP)
    list(APPEND SOURCES
        src/llamacpp/llamacpp_wrapper.cpp
        src/llamacpp/llamacpp_service.cpp
        src/llamacpp/js_llamacpp_bindings.cpp
        src/llamacpp/lua_llamacpp_bindings.cpp
    )
endif()

if(ENABLE_NEURAL)
    find_library(ORT_LIBRARY
        NAMES onnxruntime libonnxruntime.so libonnxruntime.dylib
        PATHS "${ORT_DIR}/lib"
        NO_DEFAULT_PATH
    )
    if(NOT ORT_LIBRARY)
        set(ORT_LIBRARY "${ORT_DIR}/lib/libonnxruntime${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()

    message(STATUS "ONNX Runtime library : ${ORT_LIBRARY}")
    message(STATUS "ONNX Runtime include : ${ORT_DIR}/include")

    list(APPEND SOURCES
        src/neural/neural_wrapper.c
        src/neural/neural_service.c
        src/neural/js_neural_bindings.c
        src/neural/lua_neural_bindings.c
        src/neural/wasm_neural_bindings.c
    )
endif()

# ==================================================
# Embedded files for --init
# ==================================================
# Generate C headers that embed budo-llm.md and types/budo.d.ts so the
# binary can write them to disk when invoked with --init.
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

add_custom_command(
    OUTPUT  "${GENERATED_DIR}/embedded_devapi.h"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
            --gzip
            "${CMAKE_SOURCE_DIR}/budo-llm.md"
            "embedded_devapi"
            "${GENERATED_DIR}/embedded_devapi.h"
    DEPENDS "${CMAKE_SOURCE_DIR}/budo-llm.md"
            "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
    COMMENT "Embedding budo-llm.md into embedded_devapi.h"
)

add_custom_command(
    OUTPUT  "${GENERATED_DIR}/embedded_types.h"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
            --gzip
            "${CMAKE_SOURCE_DIR}/types/budo.d.ts"
            "embedded_types"
            "${GENERATED_DIR}/embedded_types.h"
    DEPENDS "${CMAKE_SOURCE_DIR}/types/budo.d.ts"
            "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
    COMMENT "Embedding budo.d.ts into embedded_types.h"
)

add_custom_command(
    OUTPUT  "${GENERATED_DIR}/embedded_native_canvas.h"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
        "${CMAKE_SOURCE_DIR}/examples/12_native_c/main.c"
        "embedded_native_canvas"
        "${GENERATED_DIR}/embedded_native_canvas.h"
    DEPENDS "${CMAKE_SOURCE_DIR}/examples/12_native_c/main.c"
        "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
    COMMENT "Embedding native C canvas starter"
)

add_custom_command(
    OUTPUT  "${GENERATED_DIR}/embedded_native_gpu.h"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
        "${CMAKE_SOURCE_DIR}/examples/13_native_gpu/main.c"
        "embedded_native_gpu"
        "${GENERATED_DIR}/embedded_native_gpu.h"
    DEPENDS "${CMAKE_SOURCE_DIR}/examples/13_native_gpu/main.c"
        "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
    COMMENT "Embedding native C GPU starter"
)

add_custom_target(budo_embedded_headers
    DEPENDS
        "${GENERATED_DIR}/embedded_devapi.h"
        "${GENERATED_DIR}/embedded_types.h"
    "${GENERATED_DIR}/embedded_native_canvas.h"
    "${GENERATED_DIR}/embedded_native_gpu.h"
)

# ==================================================
# Embedded web runtime for --export-web
# ==================================================
# If the pre-built web runtime exists (from `make web-build-runtime`),
# embed it into the binary.  Otherwise, create stub headers with empty
# arrays so the build succeeds (--export-web will print an error at
# runtime instead).

set(WEB_RUNTIME_JS "${CMAKE_SOURCE_DIR}/web/runtime/budo.js")
set(WEB_RUNTIME_WASM "${CMAKE_SOURCE_DIR}/web/runtime/budo.wasm")
set(WEB_TEMPLATE_HTML "${CMAKE_SOURCE_DIR}/web/template.html")

if(EXISTS "${WEB_RUNTIME_JS}" AND EXISTS "${WEB_RUNTIME_WASM}")
    message(STATUS "Web runtime found — --export-web will be available")

    add_custom_command(
        OUTPUT  "${GENERATED_DIR}/embedded_web_js.h"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
                --gzip
                "${WEB_RUNTIME_JS}"
                "embedded_web_js"
                "${GENERATED_DIR}/embedded_web_js.h"
        DEPENDS "${WEB_RUNTIME_JS}"
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
        COMMENT "Embedding web runtime budo.js"
    )

    add_custom_command(
        OUTPUT  "${GENERATED_DIR}/embedded_web_wasm.h"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
                --gzip
                "${WEB_RUNTIME_WASM}"
                "embedded_web_wasm"
                "${GENERATED_DIR}/embedded_web_wasm.h"
        DEPENDS "${WEB_RUNTIME_WASM}"
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
        COMMENT "Embedding web runtime budo.wasm"
    )

    add_custom_command(
        OUTPUT  "${GENERATED_DIR}/embedded_web_template.h"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
                --gzip
                "${WEB_TEMPLATE_HTML}"
                "embedded_web_template"
                "${GENERATED_DIR}/embedded_web_template.h"
        DEPENDS "${WEB_TEMPLATE_HTML}"
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
        COMMENT "Embedding web template.html"
    )

    add_custom_target(budo_web_embedded
        DEPENDS
            "${GENERATED_DIR}/embedded_web_js.h"
            "${GENERATED_DIR}/embedded_web_wasm.h"
            "${GENERATED_DIR}/embedded_web_template.h"
    )
else()
    message(STATUS "Web runtime NOT found — --export-web will be disabled at runtime")
    message(STATUS "  Run 'make web-build-runtime' to enable it, then rebuild.")

    # Create stub headers with empty arrays
    file(WRITE "${GENERATED_DIR}/embedded_web_js.h"
        "/* Stub — web runtime not embedded.  Run make web-build-runtime. */\n"
        "#pragma once\n"
        "#include <stddef.h>\n"
        "static const unsigned char embedded_web_js_data[] = {0x00};\n"
        "static const size_t embedded_web_js_len = 0U;\n"
        "static const size_t embedded_web_js_uncompressed_len = 0U;\n"
        "static const int embedded_web_js_is_gzip = 0;\n"
    )
    file(WRITE "${GENERATED_DIR}/embedded_web_wasm.h"
        "/* Stub — web runtime not embedded.  Run make web-build-runtime. */\n"
        "#pragma once\n"
        "#include <stddef.h>\n"
        "static const unsigned char embedded_web_wasm_data[] = {0x00};\n"
        "static const size_t embedded_web_wasm_len = 0U;\n"
        "static const size_t embedded_web_wasm_uncompressed_len = 0U;\n"
        "static const int embedded_web_wasm_is_gzip = 0;\n"
    )
    file(WRITE "${GENERATED_DIR}/embedded_web_template.h"
        "/* Stub — web template not embedded. */\n"
        "#pragma once\n"
        "#include <stddef.h>\n"
        "static const unsigned char embedded_web_template_data[] = {0x00};\n"
        "static const size_t embedded_web_template_len = 0U;\n"
        "static const size_t embedded_web_template_uncompressed_len = 0U;\n"
        "static const int embedded_web_template_is_gzip = 0;\n"
    )

    add_custom_target(budo_web_embedded)
endif()

# ==================================================
# Android packaging payload for android-apk/android-aab (Budo Pro)
# ==================================================
# The payload is an implementation detail of the private pack. Public builds
# skip this target and link the unavailable stub.

if(BUDO_ENABLE_ANDROID_PACKAGING)
    file(GLOB_RECURSE ANDROID_TEMPLATE_INPUTS CONFIGURE_DEPENDS
        "${BUDO_ANDROID_PACK_TEMPLATE_ROOT}/*"
        "${CMAKE_SOURCE_DIR}/include/*"
        "${CMAKE_SOURCE_DIR}/src/*"
    )
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/build(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/\\.gradle(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/\\.cxx(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/app/build(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/app/\\.gradle(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/app/\\.cxx(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/app/src/main/assets/app(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/app/src/main/cpp/quickjs(/|$)")
    list(FILTER ANDROID_TEMPLATE_INPUTS EXCLUDE REGEX "/android/local\\.properties$")

    set(ANDROID_TEMPLATE_TAR "${GENERATED_DIR}/android-template.tar")

    add_custom_command(
        OUTPUT  "${ANDROID_TEMPLATE_TAR}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/cmake/create_android_template.py"
                "${CMAKE_SOURCE_DIR}"
                "${ANDROID_TEMPLATE_TAR}"
                --android-root "${BUDO_ANDROID_PACK_TEMPLATE_ROOT}"
        DEPENDS ${ANDROID_TEMPLATE_INPUTS}
                "${CMAKE_SOURCE_DIR}/cmake/create_android_template.py"
        COMMENT "Creating Android template tar payload"
    )

    add_custom_command(
        OUTPUT  "${GENERATED_DIR}/embedded_android_template.h"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
                --gzip
                "${ANDROID_TEMPLATE_TAR}"
                "embedded_android_template"
                "${GENERATED_DIR}/embedded_android_template.h"
        DEPENDS "${ANDROID_TEMPLATE_TAR}"
                "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
        COMMENT "Embedding gzipped Android template"
    )

    add_custom_target(budo_android_embedded
        DEPENDS "${GENERATED_DIR}/embedded_android_template.h"
    )
endif()

# Skia SVG module sources (compiled from source since no prebuilt library)
file(GLOB SKIA_SVG_SOURCES ${SKIA_DIR}/modules/svg/src/*.cpp)
file(GLOB SKIA_PATHOPS_SOURCES ${SKIA_DIR}/src/pathops/*.cpp)
set(SKIA_SHAPER_SOURCES
    ${SKIA_DIR}/modules/skshaper/src/SkShaper.cpp
    ${SKIA_DIR}/modules/skshaper/src/SkShaper_primitive.cpp
    ${SKIA_DIR}/modules/skshaper/src/SkShaper_factory.cpp
)

# Create executable
add_library(budo_host_core STATIC
    src/core/api_error.c
    src/core/application_driver.c
    src/core/input.c
    src/core/subsystem_queue.c
    src/core/subsystem_registry.c
    src/core/subsystem_composition.c
)
add_library(budo::host_core ALIAS budo_host_core)
set_target_properties(budo_host_core PROPERTIES EXPORT_NAME host_core)
target_compile_features(budo_host_core PUBLIC c_std_11)
target_include_directories(budo_host_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(budo_host_core PRIVATE Threads::Threads)

add_library(budo_desktop_host STATIC
    src/desktop/desktop_host.c
    src/core/window.c
    src/core/window_canvas_texture.c
    src/core/gl_desktop.c
    src/core/path_util.c
    src/core/gl_shader_dialect.c
    src/graphics/gl_shader_pipeline.c
    src/graphics/gl_render_target.c
    src/graphics/skia_wrapper.cpp
    src/graphics/image_loader.c
    src/graphics/gl_state_guard.c
    src/graphics/gpu_resource_state.c
    ${SKIA_SVG_SOURCES}
    ${SKIA_SHAPER_SOURCES}
    ${SKIA_PATHOPS_SOURCES}
)
add_library(budo::desktop_host ALIAS budo_desktop_host)
set_target_properties(budo_desktop_host PROPERTIES EXPORT_NAME desktop_host)
target_compile_features(budo_desktop_host PUBLIC c_std_11 cxx_std_20)
target_link_libraries(budo_desktop_host PUBLIC budo_host_core)
target_compile_definitions(budo_desktop_host PRIVATE SK_GL SK_GANESH)
target_include_directories(budo_desktop_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${SKIA_DIR}
        ${SKIA_INCLUDE_DIR}
        ${SKIA_INCLUDE_DIR}/core
        ${SKIA_INCLUDE_DIR}/effects
        ${SKIA_INCLUDE_DIR}/gpu
        ${SKIA_INCLUDE_DIR}/gpu/ganesh
        ${SKIA_INCLUDE_DIR}/gpu/ganesh/gl
        ${SKIA_INCLUDE_DIR}/pathops
        ${SKIA_DIR}/modules/svg/include
        ${SKIA_DIR}/modules/skshaper/include
        ${SKIA_DIR}/modules/skresources/include
        ${SDL2_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/third_party/stb
)

add_library(budo_native_host STATIC
    src/native/native_application_driver.c
    src/native/native_gpu.c
    src/native/native_graphics.c
    src/native/native_host.c
)
add_library(budo::native_host ALIAS budo_native_host)
set_target_properties(budo_native_host PROPERTIES EXPORT_NAME native_host)
target_compile_features(budo_native_host PUBLIC c_std_11)
target_include_directories(budo_native_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(budo_native_host PUBLIC budo_host_core)

add_library(budo_native_launcher STATIC
    src/native/native_launcher.c
    src/native/native_desktop.c
)
add_library(budo::native_launcher ALIAS budo_native_launcher)
set_target_properties(budo_native_launcher PROPERTIES EXPORT_NAME native_launcher)
target_compile_features(budo_native_launcher PUBLIC c_std_11)
target_include_directories(budo_native_launcher PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(budo_native_launcher PUBLIC budo_native_host budo_desktop_host)

add_library(budo_native_project_model STATIC
    src/native/native_project.c
    src/core/path_util.c
)

function(budo_escape_c_string input output)
    string(REPLACE "\\" "\\\\" escaped "${input}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    string(REPLACE "\n" "\\n" escaped "${escaped}")
    set(${output} "${escaped}" PARENT_SCOPE)
endfunction()

budo_escape_c_string("${CMAKE_BINARY_DIR}/BudoNativeTargets.cmake" BUDO_CONFIG_NATIVE_TARGETS_FILE)
budo_escape_c_string("${SKIA_LIBRARY}" BUDO_CONFIG_SKIA_LIBRARY)
budo_escape_c_string("${CMAKE_C_COMPILER}" BUDO_CONFIG_C_COMPILER)
budo_escape_c_string("${CMAKE_CXX_COMPILER}" BUDO_CONFIG_CXX_COMPILER)
budo_escape_c_string("${CMAKE_C_COMPILER_ID}" BUDO_CONFIG_C_COMPILER_ID)
budo_escape_c_string("${CMAKE_C_COMPILER_VERSION}" BUDO_CONFIG_C_COMPILER_VERSION)
budo_escape_c_string("${CMAKE_SYSTEM_NAME}" BUDO_CONFIG_SYSTEM_NAME)
budo_escape_c_string("${CMAKE_SYSTEM_PROCESSOR}" BUDO_CONFIG_SYSTEM_PROCESSOR)
budo_escape_c_string("${BUDO_NATIVE_TARGET_TUPLE}" BUDO_CONFIG_TARGET_TUPLE)
budo_escape_c_string("${CMAKE_BUILD_TYPE}" BUDO_CONFIG_BUILD_TYPE)
budo_escape_c_string("${CMAKE_GENERATOR}" BUDO_CONFIG_CMAKE_GENERATOR)
budo_escape_c_string("${CMAKE_COMMAND}" BUDO_CONFIG_CMAKE_COMMAND)
budo_escape_c_string("${BUDO_NATIVE_SDK_RELEASE_METADATA_URL}" BUDO_CONFIG_SDK_RELEASE_METADATA_URL)
budo_escape_c_string("${BUDO_NATIVE_SDK_RELEASE_METADATA_SHA256}" BUDO_CONFIG_SDK_RELEASE_METADATA_SHA256)
configure_file(
    src/native/native_build_config.h.in
    "${GENERATED_DIR}/native/native_build_config.h"
    @ONLY
)

# Build identity for `budo version`. Release builds come from a clean CI
# checkout, so the configure-time commit is the one released.
set(BUDO_BUILD_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE BUDO_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE BUDO_GIT_RESULT)
    if(BUDO_GIT_RESULT EQUAL 0 AND BUDO_GIT_COMMIT)
        set(BUDO_BUILD_COMMIT "${BUDO_GIT_COMMIT}")
    endif()
endif()
if(BUDO_QUICKJS_IMPL STREQUAL "ng")
    string(SUBSTRING "${BUDO_QUICKJS_NG_GIT_TAG}" 0 12 BUDO_QUICKJS_SHORT_REVISION)
    set(BUDO_BUILD_QUICKJS "QuickJS-NG ${BUDO_QUICKJS_SHORT_REVISION}")
else()
    set(BUDO_BUILD_QUICKJS "QuickJS (Bellard)")
endif()
configure_file(
    src/core/build_info.h.in
    "${GENERATED_DIR}/build_info.h"
    @ONLY
)

add_library(budo_native_compile STATIC
    src/native/native_compile.c
    src/native/native_cache.c
    src/native/native_sdk.c
    src/core/fs_util.c
)
add_library(budo::native_compile ALIAS budo_native_compile)
target_compile_features(budo_native_compile PUBLIC c_std_11)
target_include_directories(budo_native_compile PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GENERATED_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(budo_native_compile PUBLIC budo_native_project_model PRIVATE ZLIB::ZLIB)
add_library(budo::native_project_model ALIAS budo_native_project_model)
target_compile_features(budo_native_project_model PUBLIC c_std_11)
target_include_directories(budo_native_project_model
    PUBLIC ${CMAKE_SOURCE_DIR}/src
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)

list(REMOVE_ITEM SOURCES src/core/entry.c src/core/main.c)
add_library(budo_managed_drivers STATIC ${SOURCES})
add_library(budo::managed_drivers ALIAS budo_managed_drivers)
target_link_libraries(budo_managed_drivers PRIVATE quickjs lua sqlite3 ZLIB::ZLIB)
if(ENABLE_LLAMACPP)
    target_compile_definitions(budo_managed_drivers PRIVATE
        BUDO_LLAMACPP
        BUDO_LLAMACPP_REVISION="${BUDO_LLAMACPP_GIT_TAG}")
    target_link_libraries(budo_managed_drivers PRIVATE llama)
endif()
if(BUDO_ENABLE_ANDROID_PACKAGING)
    string(REGEX REPLACE "^SHA256=" "" BUDO_ANDROID_SUPPORT_BUNDLE_SHA256
        "${BUDO_ANDROID_SUPPORT_BUNDLE_URL_HASH}")
    target_compile_definitions(budo_managed_drivers PRIVATE
        BUDO_ANDROID_SUPPORT_BUNDLE_SHA256="${BUDO_ANDROID_SUPPORT_BUNDLE_SHA256}")
    target_link_libraries(budo_managed_drivers PRIVATE budo_native_project_model)
endif()
if(WIN32 OR APPLE)
    if(APPLE)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
        unset(OPENSSL_SSL_LIBRARY CACHE)
        unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    endif()
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(budo_managed_drivers PRIVATE BUDO_NETWORK_OPENSSL)
endif()
if(WIN32)
    target_compile_definitions(budo_managed_drivers PRIVATE
        _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(budo_managed_drivers PRIVATE ws2_32)
endif()
if(WIN32 OR APPLE)
    target_link_libraries(budo_managed_drivers PRIVATE
        OpenSSL::SSL OpenSSL::Crypto)
endif()
add_executable(${PROJECT_NAME}
    src/core/entry.c
    src/core/main.c
    src/core/cli.c
    src/desktop/managed_desktop.c)
if(ENABLE_LLAMACPP)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BUDO_LLAMACPP)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE
    budo_managed_drivers budo_desktop_host budo_native_compile)
if(ENABLE_LLAMACPP AND BUDO_LLAMACPP_DYNAMIC_BACKENDS)
    if(APPLE)
        set_property(TARGET ${PROJECT_NAME} PROPERTY BUILD_RPATH "@loader_path")
    elseif(UNIX)
        set_property(TARGET ${PROJECT_NAME} PROPERTY BUILD_RPATH "\$ORIGIN")
    endif()
    set(BUDO_LLAMACPP_SHARED_TARGETS llama ggml ggml-base ggml-cpu)
    if(ENABLE_LLAMACPP_CUDA)
        list(APPEND BUDO_LLAMACPP_SHARED_TARGETS ggml-cuda)
    endif()
    if(ENABLE_LLAMACPP_VULKAN)
        list(APPEND BUDO_LLAMACPP_SHARED_TARGETS ggml-vulkan)
    endif()
    foreach(shared_target IN LISTS BUDO_LLAMACPP_SHARED_TARGETS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${shared_target}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            VERBATIM)
    endforeach()
endif()

option(BUDO_BUILD_NATIVE_EXAMPLE "Build the native C desktop example" ON)
if(BUDO_BUILD_NATIVE_EXAMPLE)
    add_executable(budo_native_example examples/12_native_c/main.c)
    target_link_libraries(budo_native_example PRIVATE budo_native_launcher)

    add_executable(native_windowless_app tests/native_windowless_app.c)
    target_link_libraries(native_windowless_app PRIVATE budo_native_launcher)
    if(BUILD_TESTING)
        # Runs without a display: no window is ever opened.
        add_test(NAME native_windowless_exit_status
            COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:native_windowless_app> -DEXPECTED=4
                -P ${CMAKE_SOURCE_DIR}/tests/expect_exit_status.cmake)
    endif()

    add_executable(budo_native_gpu_example examples/13_native_gpu/main.c)
    target_link_libraries(budo_native_gpu_example PRIVATE budo_native_launcher)

    add_executable(budo_native_gpu_assets_example
        examples/14_native_gpu_assets/main.c)
    target_link_libraries(budo_native_gpu_assets_example PRIVATE budo_native_launcher)
    add_custom_command(TARGET budo_native_gpu_assets_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/examples/14_native_gpu_assets/shader.vert"
            "$<TARGET_FILE_DIR:budo_native_gpu_assets_example>/shader.vert"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/examples/14_native_gpu_assets/shader.frag"
            "$<TARGET_FILE_DIR:budo_native_gpu_assets_example>/shader.frag"
        VERBATIM)

    add_executable(budo_native_render_targets_example
        examples/15_native_render_targets/main.c)
    target_link_libraries(budo_native_render_targets_example PRIVATE budo_native_launcher)

    add_executable(budo_native_region_passes_example
        examples/17_native_region_passes/main.c)
    target_link_libraries(budo_native_region_passes_example PRIVATE budo_native_launcher)

    add_executable(budo_native_terrain_physics_example
        examples/16_native_terrain_physics/main.c
        examples/16_native_terrain_physics/math3d.c
        examples/16_native_terrain_physics/terrain.c
        examples/16_native_terrain_physics/physics.c
        examples/16_native_terrain_physics/ui_gpu.c
        examples/16_native_terrain_physics/generated/car_models.c)
    target_include_directories(budo_native_terrain_physics_example PRIVATE
        examples/16_native_terrain_physics/include
        examples/16_native_terrain_physics/generated)
    target_link_libraries(budo_native_terrain_physics_example PRIVATE budo_native_launcher)
    foreach(shader terrain.vert terrain.frag car.vert car.frag)
        add_custom_command(TARGET budo_native_terrain_physics_example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/examples/16_native_terrain_physics/${shader}"
                "$<TARGET_FILE_DIR:budo_native_terrain_physics_example>/${shader}"
            VERBATIM)
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME dependency_lock_consistency
        COMMAND ${CMAKE_COMMAND}
            -DBUDO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/validate_dependency_lock.cmake)

    if(UNIX)
        add_test(NAME install_privilege_boundary
            COMMAND bash ${CMAKE_SOURCE_DIR}/tests/install_privilege_boundary_test.sh
                ${CMAKE_SOURCE_DIR})
    endif()

    add_test(NAME web_network_policy_consistency
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/web_network_policy_consistency_test.py
            ${CMAKE_SOURCE_DIR})

    add_test(NAME network_architecture_invariants
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/network_architecture_invariants_test.py
            ${CMAKE_SOURCE_DIR})

    add_test(NAME api_contract_consistency
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/validate-api-contracts.py
            --repo ${CMAKE_SOURCE_DIR}
            --output ${CMAKE_BINARY_DIR}/generated/api-support-matrix.json)

    add_test(NAME api_contract_validator
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/api_contract_validator_test.py
            ${CMAKE_SOURCE_DIR})

    add_test(NAME conformance_matrix
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/conformance_matrix_test.py
            ${CMAKE_SOURCE_DIR})

    add_test(NAME feature_pack_resolver
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/feature_pack_resolver_test.py
            ${CMAKE_SOURCE_DIR})

    if(EXISTS "${CMAKE_SOURCE_DIR}/private/android/feature-pack.json")
        add_test(NAME feature_pack_build_boundary
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/feature_pack_build_boundary_test.py
                ${CMAKE_SOURCE_DIR})
    endif()

    add_test(NAME canvas_binding_ownership
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/canvas_binding_ownership_test.py
            ${CMAKE_SOURCE_DIR})

    set(HEADLESS_RUN_TEST_ARGS "")
    if(BUDO_QUICKJS_IMPL STREQUAL "ng")
        set(HEADLESS_RUN_TEST_ARGS "--quickjs-ng")
    endif()
    # `budo version` / --version / -v: first line is "budo <version>".
    string(REPLACE "." "\\." BUDO_VERSION_STRING_FOR_TESTS "${BUDO_VERSION_STRING}")
    foreach(version_flag IN ITEMS version --version -v)
        add_test(NAME cli_version_${version_flag}
            COMMAND $<TARGET_FILE:${PROJECT_NAME}> ${version_flag})
        set_tests_properties(cli_version_${version_flag} PROPERTIES
            PASS_REGULAR_EXPRESSION "^budo ${BUDO_VERSION_STRING_FOR_TESTS}\ncommit: ")
    endforeach()

    add_test(NAME headless_run
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/headless_run_test.py
            $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_BINARY_DIR}/test-data/headless-run
            ${HEADLESS_RUN_TEST_ARGS})

    add_test(NAME binding_ownership
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/binding_ownership_test.py
            ${CMAKE_SOURCE_DIR})

    if(EXISTS "${CMAKE_SOURCE_DIR}/private/android/android/app/src/main/cpp/android_main.cpp")
        add_test(NAME managed_lifecycle_manifest
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/managed_lifecycle_manifest_test.py
                ${CMAKE_SOURCE_DIR})
    endif()

    add_executable(gl_state_guard_test
        tests/gl_state_guard_test.c
        src/graphics/gl_state_guard.c)
    target_include_directories(gl_state_guard_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(gl_state_guard_test PRIVATE c_std_11)
    add_test(NAME gl_state_guard COMMAND gl_state_guard_test)

    add_executable(gpu_resource_state_test
        tests/gpu_resource_state_test.c
        src/graphics/gpu_resource_state.c)
    target_include_directories(gpu_resource_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(gpu_resource_state_test PRIVATE c_std_11)
    add_test(NAME gpu_resource_state COMMAND gpu_resource_state_test)

    add_executable(gl_shader_pipeline_test
        tests/gl_shader_pipeline_test.c
        src/core/gl_shader_dialect.c
        src/graphics/gl_shader_pipeline.c)
    target_include_directories(gl_shader_pipeline_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(gl_shader_pipeline_test PRIVATE c_std_11)
    add_test(NAME gl_shader_pipeline COMMAND gl_shader_pipeline_test)

    add_executable(gl_render_target_test
        tests/gl_render_target_test.c
        src/graphics/gl_render_target.c)
    target_include_directories(gl_render_target_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(gl_render_target_test PRIVATE c_std_11)
    add_test(NAME gl_render_target COMMAND gl_render_target_test)

    add_executable(network_request_core_test
        tests/network_request_core_test.c
        src/network/network_request_core.c)
    target_include_directories(network_request_core_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(network_request_core_test PRIVATE c_std_11)
    add_test(NAME network_request_core COMMAND network_request_core_test)

    if(EXISTS "${CMAKE_SOURCE_DIR}/private/android/android/app/src/main/cpp/android_window.c")
        add_test(NAME gpu_boundary_invariants
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/gpu_boundary_invariants_test.py
                ${CMAKE_SOURCE_DIR})
    endif()

    add_test(NAME api_contract_artifacts
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/generate-contract-artifacts.py
            --repo ${CMAKE_SOURCE_DIR}
            --check)

    add_test(NAME conformance_vectors
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/conformance_vectors_test.py
            ${CMAKE_SOURCE_DIR})
    set_tests_properties(conformance_vectors PROPERTIES
        DEPENDS api_contract_artifacts)

    add_test(NAME typescript_declarations_consistency
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/generate-types.sh --check)
    set_tests_properties(typescript_declarations_consistency PROPERTIES
        DEPENDS api_contract_artifacts)

    add_executable(basic_runtime_conformance_test
        tests/basic_runtime_conformance_test.c
        src/core/api_error.c
        src/core/capabilities.c
        src/core/subsystem_registry.c
        src/core/js_capabilities_bindings.c
        src/core/lua_capabilities_bindings.c
        src/core/wasm_capabilities_bindings.c
        src/device/device_wrapper.c
        src/device/device_service.c
        src/device/js_device_bindings.c
        src/device/lua_device_bindings.c
        src/device/wasm_device_bindings.c
        src/math/math_wrapper.c
        src/math/js_math_bindings.c
        src/math/lua_math_bindings.c
        src/math/wasm_math_bindings.c)
    target_include_directories(basic_runtime_conformance_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include
        ${SDL2_INCLUDE_DIRS})
    target_compile_features(basic_runtime_conformance_test PRIVATE c_std_11)
    target_compile_definitions(basic_runtime_conformance_test PRIVATE)
    target_link_libraries(basic_runtime_conformance_test PRIVATE
        quickjs
        lua
        ${WASMTIME_LIBRARY}
        ${SDL2_LIBRARIES}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    if(UNIX)
        target_link_libraries(basic_runtime_conformance_test PRIVATE m)
    endif()
    add_test(NAME basic_runtime_conformance
        COMMAND basic_runtime_conformance_test)

    add_executable(sqlite_file_runtime_conformance_test
        tests/sqlite_file_runtime_conformance_test.c
        src/core/api_error.c
        src/core/subsystem_queue.c
        src/file/file_wrapper.c
        src/file/file_service.c
        src/file/js_file_bindings.c
        src/file/lua_file_bindings.c
        src/file/wasm_file_bindings.c
        src/sqlite/sqlite_wrapper.c
        src/sqlite/sqlite_service.c
        src/sqlite/js_sqlite_bindings.c
        src/sqlite/lua_sqlite_bindings.c
        src/sqlite/wasm_sqlite_bindings.c
        src/wasm_util/wasm_memory.c)
    target_include_directories(sqlite_file_runtime_conformance_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(sqlite_file_runtime_conformance_test PRIVATE c_std_11)
    target_compile_definitions(sqlite_file_runtime_conformance_test PRIVATE
        BUDO_FILE_BRIDGE_TEST)
    target_link_libraries(sqlite_file_runtime_conformance_test PRIVATE
        quickjs lua sqlite3
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    set(SQLITE_FILE_CONFORMANCE_ROOT
        "${CMAKE_BINARY_DIR}/test-data/sqlite-file-conformance")
    file(MAKE_DIRECTORY "${SQLITE_FILE_CONFORMANCE_ROOT}")
    add_test(NAME sqlite_file_runtime_conformance
        COMMAND sqlite_file_runtime_conformance_test
            "${SQLITE_FILE_CONFORMANCE_ROOT}")

    add_executable(magneto_audio_runtime_conformance_test
        tests/magneto_audio_runtime_conformance_test.c
        src/core/api_error.c
        tests/audio_mock.c
        tests/magneto_mock.c
        src/audio/audio_service.c
        src/audio/js_audio_bindings.c
        src/audio/lua_audio_bindings.c
        src/audio/wasm_audio_bindings.c
        src/magneto/js_magneto_bindings.c
        src/magneto/magneto_service.c
        src/magneto/lua_magneto_bindings.c
        src/magneto/wasm_magneto_bindings.c)
    target_include_directories(magneto_audio_runtime_conformance_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(magneto_audio_runtime_conformance_test PRIVATE c_std_11)
    target_link_libraries(magneto_audio_runtime_conformance_test PRIVATE
        quickjs lua
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME magneto_audio_runtime_conformance
        COMMAND magneto_audio_runtime_conformance_test)

    add_executable(js_canvas_binding_state_test
        tests/js_canvas_binding_state_test.c
        src/core/ts_strip.c
        src/graphics/color_util.c
        src/graphics/js_canvas_bindings.c
        src/graphics/js_core_bindings.c
        src/graphics/js_gl_bindings.c)
    target_include_directories(js_canvas_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(js_canvas_binding_state_test PRIVATE c_std_11)
    target_compile_definitions(js_canvas_binding_state_test PRIVATE)
    target_link_libraries(js_canvas_binding_state_test PRIVATE
        quickjs budo_desktop_host)
    set(JS_CANVAS_TEST_ROOT_A "${CMAKE_BINARY_DIR}/test-data/js-canvas-a")
    set(JS_CANVAS_TEST_ROOT_B "${CMAKE_BINARY_DIR}/test-data/js-canvas-b")
    file(MAKE_DIRECTORY "${JS_CANVAS_TEST_ROOT_A}" "${JS_CANVAS_TEST_ROOT_B}")
    configure_file("${CMAKE_SOURCE_DIR}/web/fonts/default.ttf"
        "${JS_CANVAS_TEST_ROOT_A}/isolation.ttf" COPYONLY)
    file(REMOVE "${JS_CANVAS_TEST_ROOT_B}/isolation.ttf")
    add_test(NAME js_canvas_binding_state
        COMMAND js_canvas_binding_state_test
            "${JS_CANVAS_TEST_ROOT_A}" "${JS_CANVAS_TEST_ROOT_B}")

    add_executable(js_runtime_load_test
        tests/js_runtime_load_test.c
        src/core/ts_strip.c
        src/graphics/color_util.c
        src/graphics/js_canvas_bindings.c
        src/graphics/js_core_bindings.c
        src/graphics/js_gl_bindings.c)
    target_include_directories(js_runtime_load_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(js_runtime_load_test PRIVATE c_std_11)
    target_link_libraries(js_runtime_load_test PRIVATE
        quickjs budo_desktop_host)
    set(JS_RUNTIME_LOAD_TEST_ROOT "${CMAKE_BINARY_DIR}/test-data/js-runtime-load")
    file(MAKE_DIRECTORY "${JS_RUNTIME_LOAD_TEST_ROOT}")
    add_test(NAME js_runtime_load
        COMMAND js_runtime_load_test "${JS_RUNTIME_LOAD_TEST_ROOT}")

    add_executable(wasm_network_security_test
        tests/wasm_network_security_test.c)
    target_include_directories(wasm_network_security_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(wasm_network_security_test PRIVATE c_std_11)
    add_test(NAME wasm_network_security COMMAND wasm_network_security_test)

    add_executable(managed_network_binding_state_test
        tests/managed_network_binding_state_test.c
        src/core/api_error.c
        tests/network_mock.c
        src/network/network_service.c
        src/network/js_network_bindings.c
        src/network/lua_network_bindings.c)
    target_include_directories(managed_network_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_network_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_network_binding_state_test PRIVATE quickjs lua)
    add_test(NAME managed_network_binding_state
        COMMAND managed_network_binding_state_test)

    add_executable(managed_file_binding_state_test
        tests/managed_file_binding_state_test.c
        src/core/api_error.c
        src/core/subsystem_queue.c
        src/file/file_wrapper.c
        src/file/file_service.c
        src/file/js_file_bindings.c)
    target_include_directories(managed_file_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_file_binding_state_test PRIVATE c_std_11)
    target_compile_definitions(managed_file_binding_state_test PRIVATE
        BUDO_FILE_BRIDGE_TEST)
    target_link_libraries(managed_file_binding_state_test PRIVATE
        quickjs Threads::Threads)
    set(MANAGED_FILE_TEST_ROOT_A "${CMAKE_BINARY_DIR}/test-data/managed-file-a")
    set(MANAGED_FILE_TEST_ROOT_B "${CMAKE_BINARY_DIR}/test-data/managed-file-b")
    set(MANAGED_FILE_TEST_ROOT_C "${CMAKE_BINARY_DIR}/test-data/managed-file-c")
    file(MAKE_DIRECTORY
        "${MANAGED_FILE_TEST_ROOT_A}"
        "${MANAGED_FILE_TEST_ROOT_B}"
        "${MANAGED_FILE_TEST_ROOT_C}")
    add_test(NAME managed_file_binding_state
        COMMAND managed_file_binding_state_test
            "${MANAGED_FILE_TEST_ROOT_A}"
            "${MANAGED_FILE_TEST_ROOT_B}"
            "${MANAGED_FILE_TEST_ROOT_C}")

    add_executable(wasm_file_binding_state_test
        tests/wasm_file_binding_state_test.c
        src/core/api_error.c
        src/file/file_wrapper.c
        src/file/file_service.c
        src/file/wasm_file_bindings.c
        src/wasm_util/wasm_memory.c)
    target_include_directories(wasm_file_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_file_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_file_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    set(WASM_FILE_TEST_ROOT_A "${CMAKE_BINARY_DIR}/test-data/wasm-file-a")
    set(WASM_FILE_TEST_ROOT_B "${CMAKE_BINARY_DIR}/test-data/wasm-file-b")
    file(MAKE_DIRECTORY
        "${WASM_FILE_TEST_ROOT_A}"
        "${WASM_FILE_TEST_ROOT_B}")
    add_test(NAME wasm_file_binding_state
        COMMAND wasm_file_binding_state_test
            "${WASM_FILE_TEST_ROOT_A}"
            "${WASM_FILE_TEST_ROOT_B}")

    add_executable(wasm_network_binding_state_test
        tests/wasm_network_binding_state_test.c
        src/core/api_error.c
        tests/network_mock.c
        src/network/network_service.c
        src/network/wasm_network_bindings.c
        src/wasm_util/wasm_memory.c)
    target_include_directories(wasm_network_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_network_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_network_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME wasm_network_binding_state
        COMMAND wasm_network_binding_state_test)
    add_test(NAME network_runtime_conformance
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/compare_runtime_traces.py
            --runtime managed=$<TARGET_FILE:managed_network_binding_state_test>
            --runtime wasmtime=$<TARGET_FILE:wasm_network_binding_state_test>)

    add_executable(wasm_math_binding_state_test
        tests/wasm_math_binding_state_test.c
        src/core/api_error.c
        src/math/wasm_math_bindings.c
        src/math/math_wrapper.c)
    target_include_directories(wasm_math_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_math_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_math_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    if(UNIX)
        target_link_libraries(wasm_math_binding_state_test PRIVATE m)
    endif()
    add_test(NAME wasm_math_binding_state COMMAND wasm_math_binding_state_test)

    add_executable(wasm_sqlite_binding_state_test
        tests/wasm_sqlite_binding_state_test.c
        src/core/api_error.c
        src/sqlite/wasm_sqlite_bindings.c
        src/sqlite/sqlite_wrapper.c
        src/sqlite/sqlite_service.c
        src/wasm_util/wasm_memory.c)
    target_include_directories(wasm_sqlite_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_sqlite_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_sqlite_binding_state_test PRIVATE
        sqlite3
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    set(WASM_SQLITE_TEST_ROOT_A "${CMAKE_BINARY_DIR}/test-data/wasm-sqlite-a")
    set(WASM_SQLITE_TEST_ROOT_B "${CMAKE_BINARY_DIR}/test-data/wasm-sqlite-b")
    file(MAKE_DIRECTORY "${WASM_SQLITE_TEST_ROOT_A}" "${WASM_SQLITE_TEST_ROOT_B}")
    add_test(NAME wasm_sqlite_binding_state
        COMMAND wasm_sqlite_binding_state_test
            "${WASM_SQLITE_TEST_ROOT_A}" "${WASM_SQLITE_TEST_ROOT_B}")

    add_executable(managed_sqlite_binding_state_test
        tests/managed_sqlite_binding_state_test.c
        src/core/api_error.c
        src/sqlite/js_sqlite_bindings.c
        src/sqlite/lua_sqlite_bindings.c
        src/sqlite/sqlite_wrapper.c
        src/sqlite/sqlite_service.c)
    target_include_directories(managed_sqlite_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_sqlite_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_sqlite_binding_state_test PRIVATE
        quickjs lua sqlite3)
    set(MANAGED_SQLITE_TEST_ROOT_A "${CMAKE_BINARY_DIR}/test-data/managed-sqlite-a")
    set(MANAGED_SQLITE_TEST_ROOT_B "${CMAKE_BINARY_DIR}/test-data/managed-sqlite-b")
    file(MAKE_DIRECTORY "${MANAGED_SQLITE_TEST_ROOT_A}" "${MANAGED_SQLITE_TEST_ROOT_B}")
    add_test(NAME managed_sqlite_binding_state
        COMMAND managed_sqlite_binding_state_test
            "${MANAGED_SQLITE_TEST_ROOT_A}" "${MANAGED_SQLITE_TEST_ROOT_B}")

    add_executable(managed_audio_binding_state_test
        tests/managed_audio_binding_state_test.c
        src/core/api_error.c
        tests/audio_mock.c
        src/audio/audio_service.c
        src/audio/js_audio_bindings.c
        src/audio/lua_audio_bindings.c)
    target_include_directories(managed_audio_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_audio_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_audio_binding_state_test PRIVATE quickjs lua)
    add_test(NAME managed_audio_binding_state
        COMMAND managed_audio_binding_state_test)

    add_executable(managed_midi_binding_state_test
        tests/managed_midi_binding_state_test.c
        src/core/api_error.c
        tests/midi_mock.c
        tests/udp_mock.c
        src/core/subsystem_queue.c
        src/midi/midi_topology.c
        src/midi/midi_service.c
        src/midi/js_midi_bindings.c
        src/midi/lua_midi_bindings.c)
    target_include_directories(managed_midi_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_midi_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_midi_binding_state_test PRIVATE
        quickjs lua Threads::Threads)
    add_test(NAME managed_midi_binding_state
        COMMAND managed_midi_binding_state_test)

    add_executable(wasm_audio_binding_state_test
        tests/wasm_audio_binding_state_test.c
        src/core/api_error.c
        tests/audio_mock.c
        src/audio/audio_service.c
        src/audio/wasm_audio_bindings.c)
    target_include_directories(wasm_audio_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_audio_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_audio_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME wasm_audio_binding_state
        COMMAND wasm_audio_binding_state_test)

    add_executable(wasm_midi_binding_state_test
        tests/wasm_midi_binding_state_test.c
        src/core/api_error.c
        tests/midi_mock.c
        tests/udp_mock.c
        src/midi/midi_service.c
        src/midi/wasm_midi_bindings.c)
    target_include_directories(wasm_midi_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_midi_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_midi_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME wasm_midi_binding_state
        COMMAND wasm_midi_binding_state_test)
    add_test(NAME midi_runtime_conformance
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/compare_runtime_traces.py
            --runtime managed=$<TARGET_FILE:managed_midi_binding_state_test>
            --runtime wasmtime=$<TARGET_FILE:wasm_midi_binding_state_test>)

    add_executable(wasm_canvas_binding_state_test
        tests/wasm_canvas_binding_state_test.c)
    target_include_directories(wasm_canvas_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_canvas_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_canvas_binding_state_test PRIVATE
        budo_managed_drivers
        budo_desktop_host
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    set(WASM_CANVAS_TEST_ROOT "${CMAKE_BINARY_DIR}/test-data/wasm-canvas")
    file(MAKE_DIRECTORY "${WASM_CANVAS_TEST_ROOT}")
    add_test(NAME wasm_canvas_binding_state
        COMMAND wasm_canvas_binding_state_test "${WASM_CANVAS_TEST_ROOT}")

    add_executable(managed_magneto_binding_state_test
        tests/managed_magneto_binding_state_test.c
        src/core/api_error.c
        tests/magneto_mock.c
        src/magneto/magneto_service.c
        src/magneto/js_magneto_bindings.c
        src/magneto/lua_magneto_bindings.c)
    target_include_directories(managed_magneto_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_magneto_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_magneto_binding_state_test PRIVATE quickjs lua)
    add_test(NAME managed_magneto_binding_state
        COMMAND managed_magneto_binding_state_test)

    add_executable(wasm_magneto_binding_state_test
        tests/wasm_magneto_binding_state_test.c
        src/core/api_error.c
        tests/magneto_mock.c
        src/magneto/magneto_service.c
        src/magneto/wasm_magneto_bindings.c)
    target_include_directories(wasm_magneto_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_magneto_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_magneto_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME wasm_magneto_binding_state
        COMMAND wasm_magneto_binding_state_test)

    add_executable(managed_udp_binding_state_test
        tests/managed_udp_binding_state_test.c
        tests/udp_mock.c
        src/network/js_udp_bindings.c
        src/network/lua_udp_bindings.c)
    target_include_directories(managed_udp_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_udp_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_udp_binding_state_test PRIVATE quickjs lua)
    add_test(NAME managed_udp_binding_state
        COMMAND managed_udp_binding_state_test)

    add_executable(wasm_udp_binding_state_test
        tests/wasm_udp_binding_state_test.c
        tests/udp_mock.c
        src/network/wasm_udp_bindings.c)
    target_include_directories(wasm_udp_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_udp_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_udp_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME wasm_udp_binding_state
        COMMAND wasm_udp_binding_state_test)

    add_test(NAME udp_runtime_conformance
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/compare_runtime_traces.py
            --runtime managed=$<TARGET_FILE:managed_udp_binding_state_test>
            --runtime wasmtime=$<TARGET_FILE:wasm_udp_binding_state_test>)

    add_executable(managed_neural_binding_state_test
        tests/managed_neural_binding_state_test.c
        src/core/api_error.c
        tests/neural_mock.c
        src/neural/neural_service.c
        src/neural/js_neural_bindings.c
        src/neural/lua_neural_bindings.c)
    target_include_directories(managed_neural_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(managed_neural_binding_state_test PRIVATE c_std_11)
    target_link_libraries(managed_neural_binding_state_test PRIVATE quickjs lua)
    add_test(NAME managed_neural_binding_state
        COMMAND managed_neural_binding_state_test)

    add_executable(wasm_neural_binding_state_test
        tests/wasm_neural_binding_state_test.c
        src/core/api_error.c
        tests/neural_mock.c
        src/neural/neural_service.c
        src/neural/wasm_neural_bindings.c)
    target_include_directories(wasm_neural_binding_state_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${WASMTIME_DIR}/include)
    target_compile_features(wasm_neural_binding_state_test PRIVATE c_std_11)
    target_link_libraries(wasm_neural_binding_state_test PRIVATE
        ${WASMTIME_LIBRARY}
        ${CMAKE_DL_LIBS}
        Threads::Threads)
    add_test(NAME wasm_neural_binding_state
        COMMAND wasm_neural_binding_state_test)
    add_test(NAME neural_runtime_conformance
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/compare_runtime_traces.py
            --runtime managed=$<TARGET_FILE:managed_neural_binding_state_test>
            --runtime wasmtime=$<TARGET_FILE:wasm_neural_binding_state_test>)

    if(ENABLE_LLAMACPP)
        add_executable(managed_llamacpp_binding_state_test
            tests/managed_llamacpp_binding_state_test.cpp)
        target_include_directories(managed_llamacpp_binding_state_test PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src
            ${quickjs_SOURCE_DIR}
            ${lua_SOURCE_DIR}/src)
        target_compile_features(managed_llamacpp_binding_state_test PRIVATE cxx_std_20)
        target_link_libraries(managed_llamacpp_binding_state_test PRIVATE
            budo_managed_drivers budo_host_core quickjs lua llama Threads::Threads)
        add_test(NAME managed_llamacpp_binding_state
            COMMAND managed_llamacpp_binding_state_test)
    endif()

    add_executable(api_error_test
        tests/api_error_test.c
    )
    target_include_directories(api_error_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(api_error_test PRIVATE c_std_11)
    target_link_libraries(api_error_test PRIVATE budo_host_core)
    add_test(NAME api_error COMMAND api_error_test)

    add_executable(lua_canvas_binding_state_test
        tests/lua_canvas_binding_state_test.c
        src/graphics/lua_canvas_bindings.c
        src/graphics/lua_gl_bindings.c
        src/graphics/lua_transform_bindings.c
        src/graphics/color_util.c
    )
    target_include_directories(lua_canvas_binding_state_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(lua_canvas_binding_state_test PRIVATE c_std_11)
    target_link_libraries(lua_canvas_binding_state_test PRIVATE budo_desktop_host lua)
    set(LUA_CANVAS_TEST_ROOT_A "${CMAKE_BINARY_DIR}/test-data/lua-canvas-a")
    set(LUA_CANVAS_TEST_ROOT_B "${CMAKE_BINARY_DIR}/test-data/lua-canvas-b")
    file(MAKE_DIRECTORY "${LUA_CANVAS_TEST_ROOT_A}" "${LUA_CANVAS_TEST_ROOT_B}")
    configure_file("${CMAKE_SOURCE_DIR}/web/fonts/default.ttf"
        "${LUA_CANVAS_TEST_ROOT_A}/isolation.ttf" COPYONLY)
    file(REMOVE "${LUA_CANVAS_TEST_ROOT_B}/isolation.ttf")
    add_test(NAME lua_canvas_binding_state
        COMMAND lua_canvas_binding_state_test
            "${LUA_CANVAS_TEST_ROOT_A}" "${LUA_CANVAS_TEST_ROOT_B}")

    add_test(NAME canvas_host_runtime_conformance
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/compare_runtime_traces.py
            --runtime javascript=$<TARGET_FILE:js_canvas_binding_state_test>
            --arg javascript=${JS_CANVAS_TEST_ROOT_A}
            --arg javascript=${JS_CANVAS_TEST_ROOT_B}
            --runtime lua=$<TARGET_FILE:lua_canvas_binding_state_test>
            --arg lua=${LUA_CANVAS_TEST_ROOT_A}
            --arg lua=${LUA_CANVAS_TEST_ROOT_B}
            --runtime wasmtime=$<TARGET_FILE:wasm_canvas_binding_state_test>
            --arg wasmtime=${WASM_CANVAS_TEST_ROOT})
    # These tests write and remove scripts in the same test-data folders.
    set_tests_properties(js_canvas_binding_state lua_canvas_binding_state
        wasm_canvas_binding_state canvas_host_runtime_conformance
        PROPERTIES RESOURCE_LOCK canvas_test_data)

    add_executable(application_driver_test
        tests/application_driver_test.c
    )
    target_include_directories(application_driver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(application_driver_test PRIVATE budo_host_core)
    add_test(NAME application_driver_lifecycle COMMAND application_driver_test)

    add_executable(subsystem_registry_test
        tests/subsystem_registry_test.c
    )
    target_include_directories(subsystem_registry_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(subsystem_registry_test PRIVATE budo_host_core)
    add_test(NAME subsystem_registry_lifecycle COMMAND subsystem_registry_test)

    add_executable(subsystem_composition_test
        tests/subsystem_composition_test.c)
    target_include_directories(subsystem_composition_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(subsystem_composition_test PRIVATE budo_host_core)
    add_test(NAME subsystem_declarative_composition COMMAND subsystem_composition_test)
    add_test(NAME subsystem_composition_invariants
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/subsystem_composition_invariants_test.py
            ${CMAKE_SOURCE_DIR})
    add_test(NAME canvas_adapter_split
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/canvas_adapter_split_test.py
            ${CMAKE_SOURCE_DIR})
    add_test(NAME window_graphics_consolidation
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/window_graphics_consolidation_test.py
            ${CMAKE_SOURCE_DIR})

    add_executable(subsystem_queue_test
        tests/subsystem_queue_test.c
    )
    target_include_directories(subsystem_queue_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(subsystem_queue_test PRIVATE c_std_11)
    target_link_libraries(subsystem_queue_test PRIVATE budo_host_core Threads::Threads)
    add_test(NAME subsystem_queue COMMAND subsystem_queue_test)

    add_executable(input_text_test
        tests/input_text_test.c
        src/core/input.c
    )
    target_include_directories(input_text_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(input_text_test PRIVATE c_std_11)
    add_test(NAME input_text COMMAND input_text_test)

    add_executable(midi_topology_test
        tests/midi_topology_test.c
        src/midi/midi_topology.c
    )
    target_include_directories(midi_topology_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(midi_topology_test PRIVATE c_std_11)
    add_test(NAME midi_topology_fingerprint COMMAND midi_topology_test)

    if(UNIX OR WIN32)
        add_executable(file_sandbox_test
            src/file/file_wrapper.c
        )
        if(WIN32)
            target_sources(file_sandbox_test PRIVATE tests/file_sandbox_windows_test.c)
            target_compile_definitions(file_sandbox_test PRIVATE
                _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
            target_compile_options(file_sandbox_test PRIVATE /UNDEBUG)
        else()
            target_sources(file_sandbox_test PRIVATE tests/file_sandbox_test.c)
        endif()
        target_include_directories(file_sandbox_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_features(file_sandbox_test PRIVATE c_std_11)
        add_test(NAME file_sandbox_security COMMAND file_sandbox_test)
    endif()

    if(WIN32)
        add_executable(network_windows_security_test
            tests/network_windows_security_test.c
            src/network/network_policy.c
            src/network/network_request_core.c
            src/network/network_desktop_transport.c
            src/network/network_wrapper.c)
        target_include_directories(network_windows_security_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_features(network_windows_security_test PRIVATE c_std_11)
        target_compile_definitions(network_windows_security_test PRIVATE
            _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_options(network_windows_security_test PRIVATE /UNDEBUG)
        find_package(OpenSSL REQUIRED)
        target_link_libraries(network_windows_security_test PRIVATE
            OpenSSL::SSL OpenSSL::Crypto ws2_32)
        add_test(NAME network_windows_security_contract COMMAND network_windows_security_test)
    endif()

    if(UNIX)
        add_executable(file_binding_error_test
            tests/file_binding_error_test.c
            src/core/api_error.c
            src/core/subsystem_queue.c
            src/file/file_wrapper.c
            src/file/file_service.c
            src/file/js_file_bindings.c
            src/file/lua_file_bindings.c
        )
        target_include_directories(file_binding_error_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_features(file_binding_error_test PRIVATE c_std_11)
        target_link_libraries(file_binding_error_test PRIVATE
            quickjs lua Threads::Threads)
        add_test(NAME file_binding_error_contract COMMAND file_binding_error_test)

        add_executable(network_async_test
            tests/network_async_test.c
            src/network/network_policy.c
            src/network/network_request_core.c
            src/network/network_desktop_transport.c
            src/network/network_wrapper.c
            src/core/json_util.c
        )
        target_include_directories(network_async_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_features(network_async_test PRIVATE c_std_11)
        target_link_libraries(network_async_test PRIVATE pthread)
        if(APPLE)
            target_link_libraries(network_async_test PRIVATE
                "-framework Security"
                "-framework CoreFoundation")
        else()
            find_package(OpenSSL REQUIRED)
            target_link_libraries(network_async_test PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        endif()
        add_test(NAME network_async_lifecycle COMMAND network_async_test)

        add_executable(runtime_consistency_test
            tests/runtime_consistency_test.c
            src/core/app_entrypoint.c
            src/core/app_metadata.c
            src/core/json_util.c
            src/network/udp_wrapper.c
        )
        target_include_directories(runtime_consistency_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_features(runtime_consistency_test PRIVATE c_std_11)
        add_test(NAME runtime_consistency COMMAND runtime_consistency_test)

        add_executable(web_capability_test tests/web_capability_test.c)
        target_include_directories(web_capability_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_features(web_capability_test PRIVATE c_std_11)
        target_compile_definitions(web_capability_test PRIVATE BUDO_WEB=1)
        add_test(NAME web_capability_truth COMMAND web_capability_test)
    endif()

    add_executable(native_application_driver_test
        tests/native_application_driver_test.c
    )
    target_include_directories(native_application_driver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(native_application_driver_test PRIVATE budo_native_host)
    add_test(NAME native_application_lifecycle COMMAND native_application_driver_test)

    add_executable(native_example_test
        tests/native_example_test.c
        examples/12_native_c/main.c
    )
    target_include_directories(native_example_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(native_example_test PRIVATE budo_native_host budo_desktop_host)
    add_test(NAME native_application_example COMMAND native_example_test)

    add_executable(native_graphics_test tests/native_graphics_test.c)
    target_include_directories(native_graphics_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(native_graphics_test PRIVATE budo_native_host budo_desktop_host)
    add_test(NAME native_graphics_contract COMMAND native_graphics_test)

    add_executable(native_gpu_contract_test
        tests/native_gpu_contract_test.c
        src/native/native_gpu.c
        src/native/native_host.c
    )
    target_include_directories(native_gpu_contract_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(native_gpu_contract_test PRIVATE budo_host_core)
    add_test(NAME native_gpu_contract COMMAND native_gpu_contract_test)

    add_executable(native_public_headers_c tests/public_headers_c_test.c)
    target_include_directories(native_public_headers_c PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME native_public_headers_c COMMAND native_public_headers_c)

    add_executable(native_public_headers_cpp tests/public_headers_cpp_test.cpp)
    target_include_directories(native_public_headers_cpp PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME native_public_headers_cpp COMMAND native_public_headers_cpp)

    add_executable(native_project_test tests/native_project_test.c)
    target_link_libraries(native_project_test PRIVATE budo_native_project_model)

    add_executable(native_cache_test
        tests/native_cache_test.c
        src/native/native_cache.c)
    target_include_directories(native_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME native_cache_hash COMMAND native_cache_test)

    add_executable(native_sdk_security_test tests/native_sdk_security_test.c)
    target_include_directories(native_sdk_security_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${GENERATED_DIR})
    target_link_libraries(native_sdk_security_test PRIVATE budo_native_compile ZLIB::ZLIB)
    add_test(NAME native_sdk_security COMMAND native_sdk_security_test)

    set(NATIVE_PROJECT_FIXTURE_DIR "${CMAKE_SOURCE_DIR}/tests/native_project_fixture")
    set(NATIVE_PROJECT_INVALID_DIR "${CMAKE_SOURCE_DIR}/tests/native_project_invalid")
    set(NATIVE_PROJECT_GENERATED_DIR "${CMAKE_BINARY_DIR}/native-project-generated")
    set(NATIVE_PROJECT_BUILD_DIR "${NATIVE_PROJECT_GENERATED_DIR}/build")
    add_test(NAME native_project_model
        COMMAND native_project_test
            "${NATIVE_PROJECT_FIXTURE_DIR}"
            "${NATIVE_PROJECT_INVALID_DIR}"
            "${NATIVE_PROJECT_GENERATED_DIR}"
            "${CMAKE_BINARY_DIR}/BudoNativeTargets.cmake")
    if(BUDO_ENABLE_ANDROID_PACKAGING)
        add_test(NAME android_packaging_security
            COMMAND ${CMAKE_COMMAND}
                "-DBUDO_EXECUTABLE=$<TARGET_FILE:budo>"
                "-DSTAGING_ROOT=${CMAKE_BINARY_DIR}/android-package"
                "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                "-DBUDO_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DPRIVATE_ANDROID_ROOT=${CMAKE_SOURCE_DIR}/private/android"
                -P "${CMAKE_SOURCE_DIR}/tests/verify_android_packaging_security.cmake")
        set_tests_properties(android_packaging_security PROPERTIES
            RESOURCE_LOCK android_package_staging)
        add_test(NAME native_android_staging
            COMMAND ${CMAKE_COMMAND}
                "-DBUDO_EXECUTABLE=$<TARGET_FILE:budo>"
                "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/12_native_c"
                "-DSTAGING_ROOT=${CMAKE_BINARY_DIR}/android-package"
                -P "${CMAKE_SOURCE_DIR}/tests/verify_android_native_staging.cmake")
        set_tests_properties(native_android_staging PROPERTIES
            RESOURCE_LOCK android_package_staging)
        add_test(NAME native_android_gpu_assets_staging
            COMMAND ${CMAKE_COMMAND}
                "-DBUDO_EXECUTABLE=$<TARGET_FILE:budo>"
                "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/14_native_gpu_assets"
                "-DSTAGING_ROOT=${CMAKE_BINARY_DIR}/android-package"
                "-DREQUIRED_ASSETS=shader.vert;shader.frag"
                -P "${CMAKE_SOURCE_DIR}/tests/verify_android_native_staging.cmake")
        set_tests_properties(native_android_gpu_assets_staging PROPERTIES
            RESOURCE_LOCK android_package_staging)
        add_test(NAME native_android_render_targets_staging
            COMMAND ${CMAKE_COMMAND}
                "-DBUDO_EXECUTABLE=$<TARGET_FILE:budo>"
                "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/15_native_render_targets"
                "-DSTAGING_ROOT=${CMAKE_BINARY_DIR}/android-package"
                -P "${CMAKE_SOURCE_DIR}/tests/verify_android_native_staging.cmake")
        set_tests_properties(native_android_render_targets_staging PROPERTIES
            RESOURCE_LOCK android_package_staging)
        add_test(NAME native_android_region_passes_staging
            COMMAND ${CMAKE_COMMAND}
                "-DBUDO_EXECUTABLE=$<TARGET_FILE:budo>"
                "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/17_native_region_passes"
                "-DSTAGING_ROOT=${CMAKE_BINARY_DIR}/android-package"
                -P "${CMAKE_SOURCE_DIR}/tests/verify_android_native_staging.cmake")
        set_tests_properties(native_android_region_passes_staging PROPERTIES
            RESOURCE_LOCK android_package_staging)
        add_test(NAME native_android_terrain_physics_staging
            COMMAND ${CMAKE_COMMAND}
                "-DBUDO_EXECUTABLE=$<TARGET_FILE:budo>"
                "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/16_native_terrain_physics"
                "-DSTAGING_ROOT=${CMAKE_BINARY_DIR}/android-package"
                "-DREQUIRED_ASSETS=terrain.vert;terrain.frag;car.vert;car.frag;assets/kenney_car_kit/LICENSE.txt"
                -P "${CMAKE_SOURCE_DIR}/tests/verify_android_native_staging.cmake")
        set_tests_properties(native_android_terrain_physics_staging PROPERTIES
            RESOURCE_LOCK android_package_staging)
    endif()
    add_test(NAME native_project_generated_configure
        COMMAND ${CMAKE_COMMAND}
            -S "${NATIVE_PROJECT_GENERATED_DIR}"
            -B "${NATIVE_PROJECT_BUILD_DIR}"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DBUDO_NATIVE_SKIA_LIBRARY=${SKIA_LIBRARY}")
    set_tests_properties(native_project_generated_configure PROPERTIES
        DEPENDS native_project_model)
    add_test(NAME native_project_generated_build
        COMMAND ${CMAKE_COMMAND} --build "${NATIVE_PROJECT_BUILD_DIR}"
                --config $<CONFIG>)
    set_tests_properties(native_project_generated_build PROPERTIES
        DEPENDS native_project_generated_configure)
    add_test(NAME native_project_generated_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_PROJECT_BUILD_DIR}"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_generated.cmake")
    set_tests_properties(native_project_generated_verify PROPERTIES
        DEPENDS native_project_generated_build)

    set(NATIVE_COMPILE_CLI_DIR "${CMAKE_BINARY_DIR}/native-compile-cli")
    add_test(NAME native_compile_cli
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${CMAKE_SOURCE_DIR}/examples/12_native_c"
            --build-dir "${NATIVE_COMPILE_CLI_DIR}")
    add_test(NAME native_compile_cli_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_COMPILE_CLI_DIR}"
            "-DOUTPUT_NAME=Budo_Native_Example"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_compile.cmake")
    set_tests_properties(native_compile_cli_verify PROPERTIES
        DEPENDS native_compile_cli)
    set(NATIVE_GPU_ASSETS_COMPILE_DIR
        "${CMAKE_BINARY_DIR}/native-compile-gpu-assets")
    add_test(NAME native_compile_gpu_assets
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${CMAKE_SOURCE_DIR}/examples/14_native_gpu_assets"
            --build-dir "${NATIVE_GPU_ASSETS_COMPILE_DIR}")
    set_tests_properties(native_compile_gpu_assets PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-test")
    add_test(NAME native_compile_gpu_assets_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_GPU_ASSETS_COMPILE_DIR}"
            "-DOUTPUT_NAME=Budo_Native_GPU_Assets_Example"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            "-DREQUIRED_ASSETS=shader.vert;shader.frag"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_compile.cmake")
    set_tests_properties(native_compile_gpu_assets_verify PROPERTIES
        DEPENDS native_compile_gpu_assets)
    set(NATIVE_RENDER_TARGETS_COMPILE_DIR
        "${CMAKE_BINARY_DIR}/native-compile-render-targets")
    add_test(NAME native_compile_render_targets
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${CMAKE_SOURCE_DIR}/examples/15_native_render_targets"
            --build-dir "${NATIVE_RENDER_TARGETS_COMPILE_DIR}")
    set_tests_properties(native_compile_render_targets PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-test")
    add_test(NAME native_compile_render_targets_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_RENDER_TARGETS_COMPILE_DIR}"
            "-DOUTPUT_NAME=Budo_Native_Render_Targets_Example"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_compile.cmake")
    set_tests_properties(native_compile_render_targets_verify PROPERTIES
        DEPENDS native_compile_render_targets)
    set(NATIVE_REGION_PASSES_COMPILE_DIR
        "${CMAKE_BINARY_DIR}/native-compile-region-passes")
    add_test(NAME native_compile_region_passes
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${CMAKE_SOURCE_DIR}/examples/17_native_region_passes"
            --build-dir "${NATIVE_REGION_PASSES_COMPILE_DIR}")
    set_tests_properties(native_compile_region_passes PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-test")
    add_test(NAME native_compile_region_passes_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_REGION_PASSES_COMPILE_DIR}"
            "-DOUTPUT_NAME=Budo_Native_Region_Passes_Example"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_compile.cmake")
    set_tests_properties(native_compile_region_passes_verify PROPERTIES
        DEPENDS native_compile_region_passes)
    set(NATIVE_TERRAIN_PHYSICS_COMPILE_DIR
        "${CMAKE_BINARY_DIR}/native-compile-terrain-physics")
    add_test(NAME native_compile_terrain_physics
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${CMAKE_SOURCE_DIR}/examples/16_native_terrain_physics"
            --build-dir "${NATIVE_TERRAIN_PHYSICS_COMPILE_DIR}")
    set_tests_properties(native_compile_terrain_physics PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-test")
    add_test(NAME native_compile_terrain_physics_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_TERRAIN_PHYSICS_COMPILE_DIR}"
            "-DOUTPUT_NAME=Budo_Native_Terrain_Physics"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            "-DREQUIRED_ASSETS=terrain.vert;terrain.frag;car.vert;car.frag;assets/kenney_car_kit/LICENSE.txt"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_compile.cmake")
    set_tests_properties(native_compile_terrain_physics_verify PROPERTIES
        DEPENDS native_compile_terrain_physics)
    set(NATIVE_COMPILE_CACHE_HIT_DIR "${CMAKE_BINARY_DIR}/native-compile-cache-hit")
    add_test(NAME native_compile_cache_hit
        COMMAND ${CMAKE_COMMAND}
            "-DBUDO=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/12_native_c"
            "-DFIRST_BUILD_DIR=${CMAKE_BINARY_DIR}/native-compile-cache-seed"
            "-DSECOND_BUILD_DIR=${NATIVE_COMPILE_CACHE_HIT_DIR}"
            "-DCACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-hit-test"
            -P "${CMAKE_SOURCE_DIR}/tests/run_native_cache_hit.cmake")
    set_tests_properties(native_compile_cache_hit PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-hit-test")
    set_tests_properties(native_compile_cli PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-cache-test")
    add_test(NAME native_compile_cache_hit_verify
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_DIR=${NATIVE_COMPILE_CACHE_HIT_DIR}"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_cache_hit.cmake")
    set_tests_properties(native_compile_cache_hit_verify PROPERTIES
        DEPENDS native_compile_cache_hit)
    add_test(NAME native_compile_watch_rejected
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${CMAKE_SOURCE_DIR}/examples/12_native_c" --watch)
    set_tests_properties(native_compile_watch_rejected PROPERTIES
        WILL_FAIL TRUE)
    set(NATIVE_INIT_CANVAS_DIR "${CMAKE_BINARY_DIR}/native-init-canvas")
    set(NATIVE_INIT_GPU_DIR "${CMAKE_BINARY_DIR}/native-init-gpu")
    add_test(NAME native_init_canvas
        COMMAND ${CMAKE_COMMAND}
            "-DBUDO=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DTEST_ROOT=${NATIVE_INIT_CANVAS_DIR}"
            "-DTEMPLATE=canvas"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_init.cmake")
    add_test(NAME native_init_gpu
        COMMAND ${CMAKE_COMMAND}
            "-DBUDO=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DTEST_ROOT=${NATIVE_INIT_GPU_DIR}"
            "-DTEMPLATE=gpu"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_init.cmake")
    add_test(NAME native_init_invalid_options
        COMMAND ${CMAKE_COMMAND}
            "-DBUDO=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DTEST_ROOT=${CMAKE_BINARY_DIR}/native-init-invalid"
            -P "${CMAKE_SOURCE_DIR}/tests/verify_native_init_invalid.cmake")
    add_test(NAME native_init_canvas_compile
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
            "${NATIVE_INIT_CANVAS_DIR}" --debug
            --build-dir "${CMAKE_BINARY_DIR}/native-init-canvas-build")
    set_tests_properties(native_init_canvas_compile PROPERTIES
        DEPENDS native_init_canvas
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-init-cache")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        add_test(NAME native_compile_sanitizer
            COMMAND $<TARGET_FILE:${PROJECT_NAME}> compile
                "${CMAKE_SOURCE_DIR}/examples/12_native_c"
                --debug --sanitize address,undefined
                --build-dir "${CMAKE_BINARY_DIR}/native-compile-sanitizer")
        set_tests_properties(native_compile_sanitizer PROPERTIES
            ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-sanitizer-cache")
    endif()
    add_test(NAME native_sdk_relocation
        COMMAND ${CMAKE_COMMAND}
            "-DCMAKE_COMMAND_PATH=${CMAKE_COMMAND}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DBUDO=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/12_native_c"
            "-DSDK_STAGE=${CMAKE_BINARY_DIR}/native-sdk-test-stage"
            "-DSDK_RELOCATED=${CMAKE_BINARY_DIR}/native-sdk-test-relocated"
            "-DAPP_BUILD_DIR=${CMAKE_BINARY_DIR}/native-sdk-test-app"
            "-DCONFIGURATION=$<CONFIG>"
            "-DPYTHON=${Python3_EXECUTABLE}"
            "-DPACKAGER=${CMAKE_SOURCE_DIR}/scripts/package-native-sdk.py"
            "-DTARGET_TUPLE=${BUDO_NATIVE_TARGET_TUPLE}"
            -P "${CMAKE_SOURCE_DIR}/tests/run_native_sdk_relocation.cmake")
    set_tests_properties(native_sdk_relocation PROPERTIES
        ENVIRONMENT "BUDO_NATIVE_CACHE_DIR=${CMAKE_BINARY_DIR}/native-sdk-test-cache")
    add_test(NAME native_sdk_resolution
        COMMAND ${CMAKE_COMMAND}
            "-DCMAKE_COMMAND_PATH=${CMAKE_COMMAND}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DBUDO=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DPROJECT_DIR=${CMAKE_SOURCE_DIR}/examples/12_native_c"
            "-DSDK_STAGE=${CMAKE_BINARY_DIR}/native-sdk-resolution-stage"
            "-DTEST_ROOT=${CMAKE_BINARY_DIR}/native-sdk-resolution-test"
            "-DCONFIGURATION=$<CONFIG>"
            "-DPYTHON=${Python3_EXECUTABLE}"
            "-DPACKAGER=${CMAKE_SOURCE_DIR}/scripts/package-native-sdk.py"
            "-DTARGET_TUPLE=${BUDO_NATIVE_TARGET_TUPLE}"
            -P "${CMAKE_SOURCE_DIR}/tests/run_native_sdk_resolution.cmake")

    # Tests check results with assert(), often wrapping calls with side
    # effects, so assertions must stay enabled in Release (-DNDEBUG) builds.
    if(MSVC)
        set(BUDO_TEST_KEEP_ASSERTS /UNDEBUG)
    else()
        set(BUDO_TEST_KEEP_ASSERTS -UNDEBUG)
    endif()
    get_property(BUDO_DIRECTORY_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(BUDO_TARGET IN LISTS BUDO_DIRECTORY_TARGETS)
        get_target_property(BUDO_TARGET_TYPE ${BUDO_TARGET} TYPE)
        if(BUDO_TARGET MATCHES "_test$" AND BUDO_TARGET_TYPE STREQUAL "EXECUTABLE")
            target_compile_options(${BUDO_TARGET} PRIVATE ${BUDO_TEST_KEEP_ASSERTS})
        endif()
    endforeach()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${SKIA_DIR}
    ${SKIA_INCLUDE_DIR}
    ${SKIA_INCLUDE_DIR}/core
    ${SKIA_INCLUDE_DIR}/effects
    ${SKIA_INCLUDE_DIR}/gpu
    ${SKIA_INCLUDE_DIR}/gpu/ganesh
    ${SKIA_INCLUDE_DIR}/gpu/ganesh/gl
    ${SKIA_INCLUDE_DIR}/pathops
    ${SKIA_DIR}/modules/svg/include
    ${SKIA_DIR}/modules/skshaper/include
    ${SKIA_DIR}/modules/skresources/include
    ${SDL2_INCLUDE_DIRS}
    ${WASMTIME_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
    ${miniaudio_SOURCE_DIR}
    ${GENERATED_DIR}
)
target_include_directories(budo_managed_drivers PRIVATE
    ${SKIA_DIR}
    ${SKIA_INCLUDE_DIR}
    ${SKIA_INCLUDE_DIR}/core
    ${SKIA_INCLUDE_DIR}/effects
    ${SKIA_INCLUDE_DIR}/gpu
    ${SKIA_INCLUDE_DIR}/gpu/ganesh
    ${SKIA_INCLUDE_DIR}/gpu/ganesh/gl
    ${SKIA_INCLUDE_DIR}/pathops
    ${SKIA_DIR}/modules/svg/include
    ${SKIA_DIR}/modules/skshaper/include
    ${SKIA_DIR}/modules/skresources/include
    ${SDL2_INCLUDE_DIRS}
    ${WASMTIME_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb
    ${miniaudio_SOURCE_DIR}
    ${GENERATED_DIR}
)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h")
    message(FATAL_ERROR
        "third_party/stb/stb_image.h not found.\n"
        "Run ./scripts/setup-stb.sh to fetch it.")
endif()

add_dependencies(${PROJECT_NAME} budo_embedded_headers budo_web_embedded)
add_dependencies(budo_managed_drivers budo_embedded_headers budo_web_embedded)
if(BUDO_ENABLE_ANDROID_PACKAGING)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${BUDO_ANDROID_PACK_SOURCE_INCLUDE}")
    target_include_directories(budo_managed_drivers PRIVATE
        "${BUDO_ANDROID_PACK_SOURCE_INCLUDE}")
    add_dependencies(${PROJECT_NAME} budo_android_embedded)
    add_dependencies(budo_managed_drivers budo_android_embedded)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BUDO_ANDROID_PACKAGING)
    target_compile_definitions(budo_managed_drivers PRIVATE BUDO_ANDROID_PACKAGING)
endif()

# Desktop links the full Skia → advanced graphics surface available.
# See src/core/capabilities.c.
target_compile_definitions(${PROJECT_NAME} PRIVATE)
target_compile_definitions(budo_managed_drivers PRIVATE)

# Enable Skia's Ganesh GL backend for direct GPU rendering on desktop.
target_compile_definitions(${PROJECT_NAME} PRIVATE SK_GL SK_GANESH)
target_compile_definitions(budo_managed_drivers PRIVATE SK_GL SK_GANESH)

if(ENABLE_NEURAL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BUDO_NEURAL)
    target_compile_definitions(budo_managed_drivers PRIVATE BUDO_NEURAL)
    target_include_directories(${PROJECT_NAME} PRIVATE "${ORT_DIR}/include")
    target_include_directories(budo_managed_drivers PRIVATE "${ORT_DIR}/include")
    # Do NOT link against ORT directly — neural_wrapper.c loads it at runtime
    # via dlopen so the binary starts even without the dylib installed.
    # ORT_RUNTIME_PATH lets development builds find the dylib in-tree without
    # requiring it to be on the system library search path.
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        ORT_RUNTIME_PATH="${ORT_DIR}/lib/libonnxruntime${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )
    target_compile_definitions(budo_managed_drivers PRIVATE
        ORT_RUNTIME_PATH="${ORT_DIR}/lib/libonnxruntime${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )
endif()

# Find Skia module libraries
find_file(SKIA_SKRESOURCES_LIBRARY
    NAMES libskresources.a skresources.lib
    PATHS
        ${SKIA_DIR}/lib
        ${SKIA_DIR}/out/Release
        ${SKIA_DIR}/out/Static
    NO_DEFAULT_PATH
)
find_file(SKIA_EXPAT_LIBRARY
    NAMES libexpat.a expat.lib
    PATHS
        ${SKIA_DIR}/lib
        ${SKIA_DIR}/out/Release
        ${SKIA_DIR}/out/Static
    NO_DEFAULT_PATH
)

# Link libraries
# Note: skresources must come before skia (static library link order matters)
if(SKIA_SKRESOURCES_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PRIVATE "${SKIA_SKRESOURCES_LIBRARY}")
    target_link_libraries(budo_desktop_host PUBLIC
        "$<BUILD_INTERFACE:${SKIA_SKRESOURCES_LIBRARY}>"
        "$<INSTALL_INTERFACE:budo_sdk_skresources>")
    get_filename_component(BUDO_SDK_SKRESOURCES_NAME "${SKIA_SKRESOURCES_LIBRARY}" NAME)
    set(BUDO_SDK_SKRESOURCES_TARGET "if(NOT TARGET budo_sdk_skresources)\n  add_library(budo_sdk_skresources STATIC IMPORTED)\n  set_target_properties(budo_sdk_skresources PROPERTIES IMPORTED_LOCATION \"\${_BUDO_NATIVE_SDK_ROOT}/lib/budo/deps/${BUDO_SDK_SKRESOURCES_NAME}\")\nendif()")
endif()
if(SKIA_EXPAT_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PRIVATE "${SKIA_EXPAT_LIBRARY}")
    target_link_libraries(budo_desktop_host PUBLIC
        "$<BUILD_INTERFACE:${SKIA_EXPAT_LIBRARY}>"
        "$<INSTALL_INTERFACE:budo_sdk_expat>")
    get_filename_component(BUDO_SDK_EXPAT_NAME "${SKIA_EXPAT_LIBRARY}" NAME)
    set(BUDO_SDK_EXPAT_TARGET "if(NOT TARGET budo_sdk_expat)\n  add_library(budo_sdk_expat STATIC IMPORTED)\n  set_target_properties(budo_sdk_expat PROPERTIES IMPORTED_LOCATION \"\${_BUDO_NATIVE_SDK_ROOT}/lib/budo/deps/${BUDO_SDK_EXPAT_NAME}\")\nendif()")
endif()

# SDL2_LIBRARIES may include SDL2::SDL2main, which the relocatable SDK does not
# recreate; Budo supplies its own main(), so installed consumers need SDL2 only.
target_link_libraries(budo_desktop_host PUBLIC
    skia
    "$<BUILD_INTERFACE:${SDL2_LIBRARIES}>"
    "$<INSTALL_INTERFACE:SDL2::SDL2>"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    skia
    quickjs
    lua
    sqlite3
    ${WASMTIME_LIBRARY}
    ${SDL2_LIBRARIES}
    ZLIB::ZLIB
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(budo_desktop_host PUBLIC opengl32 user32 gdi32)
    target_compile_definitions(budo_desktop_host PRIVATE
        _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        opengl32
        user32
        gdi32
        ws2_32
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        # 8 MB main-thread stack like Linux/macOS (MSVC defaults to 1 MB, the
        # same as QuickJS's JS stack limit, so deep recursion would crash the
        # process instead of raising a JS RangeError). QuickJS-NG's own
        # executables use the same value.
        target_link_options(${PROJECT_NAME} PRIVATE /STACK:8388608)
    endif()
elseif(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(CORETEXT_FRAMEWORK CoreText)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(METAL_FRAMEWORK Metal)
    find_library(COREMIDI_FRAMEWORK CoreMIDI)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(SECURITY_FRAMEWORK Security)
    target_link_libraries(budo_desktop_host PUBLIC
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework CoreFoundation"
        "-framework Metal"
        "-framework OpenGL"
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${CORETEXT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${COREMIDI_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${SECURITY_FRAMEWORK}
        "-framework OpenGL"
    )
    if(TARGET wasm_canvas_binding_state_test)
        target_link_libraries(wasm_canvas_binding_state_test PRIVATE
            ${COREMIDI_FRAMEWORK}
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${SECURITY_FRAMEWORK})
    endif()
else()
    find_package(OpenGL REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(Fontconfig REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(ALSA)
    target_link_libraries(budo_desktop_host PUBLIC
        OpenGL::GL
        Freetype::Freetype
        Fontconfig::Fontconfig
        dl
        pthread
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        OpenGL::GL
        Freetype::Freetype
        Fontconfig::Fontconfig
        OpenSSL::SSL
        OpenSSL::Crypto
        dl
        pthread
    )
    if(ALSA_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${ALSA_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${ALSA_INCLUDE_DIRS})
    endif()
    if(TARGET wasm_canvas_binding_state_test)
        target_link_libraries(wasm_canvas_binding_state_test PRIVATE
            OpenSSL::SSL OpenSSL::Crypto)
        if(ALSA_FOUND)
            target_link_libraries(wasm_canvas_binding_state_test PRIVATE
                ${ALSA_LIBRARIES})
        endif()
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${Freetype_INCLUDE_DIRS}
    )
endif()

# Relocatable native SDK package metadata.
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/BudoNativeConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/BudoNativeConfig.cmake"
    INSTALL_DESTINATION lib/cmake/BudoNative
    NO_SET_AND_CHECK_MACRO)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/BudoNativeConfigVersion.cmake"
    VERSION "${BUDO_VERSION_STRING}"
    COMPATIBILITY ExactVersion)

# Install rules
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(TARGETS
    budo_host_core
    budo_desktop_host
    budo_native_host
    budo_native_launcher
    EXPORT BudoNativeTargets
    ARCHIVE DESTINATION lib COMPONENT NativeSDK
    LIBRARY DESTINATION lib COMPONENT NativeSDK
    RUNTIME DESTINATION bin COMPONENT NativeSDK
)
install(DIRECTORY include/budo DESTINATION include COMPONENT NativeSDK)
install(EXPORT BudoNativeTargets
    FILE BudoNativeTargets.cmake
    NAMESPACE budo::
    DESTINATION lib/cmake/BudoNative
    COMPONENT NativeSDK
)
install(FILES
    "${CMAKE_BINARY_DIR}/BudoNativeConfig.cmake"
    "${CMAKE_BINARY_DIR}/BudoNativeConfigVersion.cmake"
    DESTINATION lib/cmake/BudoNative
    COMPONENT NativeSDK)
install(FILES "${SKIA_LIBRARY}" DESTINATION lib/budo/deps COMPONENT NativeSDK)
if(SKIA_SKRESOURCES_LIBRARY)
    install(FILES "${SKIA_SKRESOURCES_LIBRARY}" DESTINATION lib/budo/deps COMPONENT NativeSDK)
endif()
if(SKIA_EXPAT_LIBRARY)
    install(FILES "${SKIA_EXPAT_LIBRARY}" DESTINATION lib/budo/deps COMPONENT NativeSDK)
endif()
if(WIN32)
    install(FILES "${BUDO_SDK_SDL_LIBRARY}" DESTINATION bin COMPONENT NativeSDK)
else()
    install(FILES "${BUDO_SDK_SDL_LIBRARY}" DESTINATION lib/budo/deps COMPONENT NativeSDK)
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION share/budo COMPONENT NativeSDK)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/skia/LICENSE"
    DESTINATION share/budo/licenses/skia COMPONENT NativeSDK)
install(FILES "${CMAKE_SOURCE_DIR}/licenses/native-sdk/sdl2/LICENSE.txt"
    DESTINATION share/budo/licenses/sdl2 COMPONENT NativeSDK)
install(FILES "${CMAKE_SOURCE_DIR}/licenses/native-sdk/stb/LICENSE.txt"
    DESTINATION share/budo/licenses/stb COMPONENT NativeSDK)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/skia/third_party/externals/expat/COPYING")
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/skia/third_party/externals/expat/COPYING"
        DESTINATION share/budo/licenses/expat COMPONENT NativeSDK)
endif()
export(EXPORT BudoNativeTargets
    FILE "${CMAKE_BINARY_DIR}/BudoNativeTargets.cmake"
    NAMESPACE budo::
)

if(ENABLE_NEURAL)
    # Resolve symlink so both the versioned dylib and the unversioned symlink are installed.
    # The binary links against the versioned name (e.g. libonnxruntime.1.21.0.dylib) via @rpath,
    # so we must install that real file in addition to the convenience symlink.
    file(REAL_PATH "${ORT_LIBRARY}" ORT_REAL_LIBRARY)
    install(FILES "${ORT_REAL_LIBRARY}" TYPE LIB)
    install(FILES "${ORT_LIBRARY}" TYPE LIB)
endif()

# Symlink examples into the build directory so the binary can find them
# without copying gigabytes of data on every configure.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/examples")
    file(CREATE_LINK
        "${CMAKE_SOURCE_DIR}/examples"
        "${CMAKE_BINARY_DIR}/examples"
        SYMBOLIC
    )
endif()

message(STATUS "===========================================")
message(STATUS "Build configuration:")
message(STATUS "  Platform: ${SKIA_PLATFORM}")
message(STATUS "  Skia: ${SKIA_VERSION}")
message(STATUS "  Wasmtime: ${WASMTIME_VERSION}")
message(STATUS "  SDL2: ${SDL2_VERSION}")
message(STATUS "===========================================")
