# ============================================================================
# Budo — Emscripten (Web) Build
# ============================================================================
#
# This is a standalone CMake configuration for the web target.
# It lives under web/ so the project-local emcmake can find it via -S web/.
#
# Build commands (handled by the Makefile):
#   source scripts/emscripten-env.sh
#   emcmake cmake -B build-web -S web -DBUDO_ROOT=$PWD -DCMAKE_BUILD_TYPE=Release
#   emmake  cmake --build build-web
#
# Or simply:
#   make web-build
#
# The output is build-web/budo.html + .js + .wasm (+ optional .data).
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(budo C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BudoDependencyLock.cmake")
budo_report_dependency_lock("web")

# BUDO_ROOT points back to the repo root (set by Makefile or derived here)
if(NOT BUDO_ROOT)
    get_filename_component(BUDO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
endif()

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "This CMake file is intended for Emscripten builds only.\n"
        "Use:  source scripts/emscripten-env.sh && emcmake cmake -B build-web -DBUDO_ROOT=<repo> ...\n"
        "See WEB-PLATFORM-IMPLEMENTATION.md for details.")
endif()

add_definitions(-DBUDO_WEB)

# ── Python (for embed_file.py) ───────────────────────────────────────────
find_package(Python3 COMPONENTS Interpreter QUIET)

# ── Embed default font for Skia (the empty font manager has no system fonts) ─
set(WEB_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WEB_GENERATED_DIR}")

set(DEFAULT_FONT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/fonts/default.ttf")
if(Python3_FOUND AND EXISTS "${DEFAULT_FONT_PATH}")
    add_custom_command(
        OUTPUT  "${WEB_GENERATED_DIR}/embedded_default_font.h"
        COMMAND ${Python3_EXECUTABLE}
                "${BUDO_ROOT}/cmake/embed_file.py"
                "${DEFAULT_FONT_PATH}"
                "embedded_default_font"
                "${WEB_GENERATED_DIR}/embedded_default_font.h"
        DEPENDS "${DEFAULT_FONT_PATH}"
                "${BUDO_ROOT}/cmake/embed_file.py"
        COMMENT "Embedding default font into web build"
    )
    add_custom_target(web_generated_files DEPENDS
        "${WEB_GENERATED_DIR}/embedded_default_font.h"
    )
else()
    message(WARNING "Python3 or default font not found — web build will have no default font")
    add_custom_target(web_generated_files)
endif()

# ============================================================================
# QuickJS (QuickJS-NG by default, optional Bellard with BUDO_QUICKJS_IMPL=bellard)
# ============================================================================
include(FetchContent)

# Decode-only audio file support (WAV, MP3, Ogg Vorbis, FLAC via miniaudio).
set(miniaudio_SOURCE_DIR "${BUDO_ROOT}/build/_deps/miniaudio-src")
if(NOT EXISTS "${miniaudio_SOURCE_DIR}/miniaudio.h")
    FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY ${BUDO_MINIAUDIO_GIT_REPOSITORY}
        GIT_TAG        ${BUDO_MINIAUDIO_GIT_TAG}
    )
    message(STATUS "Downloading miniaudio...")
    FetchContent_Populate(miniaudio)
endif()

set(BUDO_QUICKJS_IMPL "${BUDO_QUICKJS_IMPL_DEFAULT}" CACHE STRING "QuickJS implementation: bellard or ng")
set_property(CACHE BUDO_QUICKJS_IMPL PROPERTY STRINGS bellard ng)

if(BUDO_QUICKJS_IMPL STREQUAL "ng")
    set(quickjs_SOURCE_DIR "${BUDO_ROOT}/build/_deps/quickjs_ng-src")
    if(NOT EXISTS "${quickjs_SOURCE_DIR}/quickjs.c")
        FetchContent_Declare(
            quickjs_ng
            GIT_REPOSITORY ${BUDO_QUICKJS_NG_GIT_REPOSITORY}
            GIT_TAG        ${BUDO_QUICKJS_NG_GIT_TAG}
        )
        message(STATUS "Downloading QuickJS-NG (${BUDO_QUICKJS_NG_GIT_TAG})...")
        FetchContent_Populate(quickjs_ng)
        set(quickjs_SOURCE_DIR ${quickjs_ng_SOURCE_DIR})
    endif()
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

    # Remove 'version' file that conflicts with C++ <version> header
    file(REMOVE ${quickjs_SOURCE_DIR}/version)

    set(QUICKJS_SOURCES
        ${quickjs_SOURCE_DIR}/quickjs.c
        ${quickjs_SOURCE_DIR}/libregexp.c
        ${quickjs_SOURCE_DIR}/libunicode.c
        ${quickjs_SOURCE_DIR}/cutils.c
        ${quickjs_SOURCE_DIR}/quickjs-libc.c
    )
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
    _GNU_SOURCE
    EMSCRIPTEN
)
# Suppress warnings in third-party code
target_compile_options(quickjs PRIVATE -w)

file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-impl" "${BUDO_QUICKJS_IMPL}\n")
file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-source-dir" "${quickjs_SOURCE_DIR}\n")
if(BUDO_QUICKJS_IMPL STREQUAL "ng")
    file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-revision" "${BUDO_QUICKJS_NG_GIT_TAG}\n")
else()
    file(WRITE "${CMAKE_BINARY_DIR}/.quickjs-revision" "${BUDO_QUICKJS_BELLARD_URL_HASH}\n")
endif()
message(STATUS "Using QuickJS implementation: ${BUDO_QUICKJS_IMPL} (${quickjs_SOURCE_DIR})")

# ============================================================================
# Lua 5.4 (fetched from lua.org — same as desktop build)
# ============================================================================
set(LUA_VERSION "${BUDO_LUA_VERSION}")

FetchContent_Declare(
    lua
    URL      "${BUDO_LUA_URL}"
    URL_HASH "${BUDO_LUA_URL_HASH}"
)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    message(STATUS "Downloading Lua ${LUA_VERSION}...")
    FetchContent_Populate(lua)

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
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
    # Suppress warnings in third-party code
    target_compile_options(lua PRIVATE -w)
endif()

# ============================================================================
# SQLite (fetched from sqlite.org — same amalgamation as desktop build)
# ============================================================================
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
    # Move extracted contents to SQLITE_DIR
    file(GLOB SQLITE_EXTRACTED "${CMAKE_BINARY_DIR}/sqlite-amalgamation-*")
    if(SQLITE_EXTRACTED)
        file(RENAME "${SQLITE_EXTRACTED}" "${SQLITE_DIR}")
    endif()
endif()

add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE_DIR})
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_LOAD_EXTENSION
)
# Suppress warnings in third-party code
target_compile_options(sqlite3 PRIVATE -w)

# ============================================================================
# Skia WASM (pre-built via scripts/setup-skia-wasm.sh)
# ============================================================================
set(SKIA_WASM_DIR "${BUDO_ROOT}/third_party/skia-wasm" CACHE PATH "Path to Skia WASM checkout")
set(SKIA_WASM_OUT "${SKIA_WASM_DIR}/out/wasm")

# Skia's WASM build produces .wasm.a files
if(NOT EXISTS "${SKIA_WASM_OUT}/libskia.wasm.a")
    message(FATAL_ERROR
        "Skia WASM library not found at ${SKIA_WASM_OUT}/libskia.wasm.a\n"
        "Please run:  ./scripts/setup-skia-wasm.sh\n"
        "Then retry the web build.")
endif()

add_library(skia STATIC IMPORTED)
set_target_properties(skia PROPERTIES
    IMPORTED_LOCATION "${SKIA_WASM_OUT}/libskia.wasm.a"
)

# Skia SVG module library
if(EXISTS "${SKIA_WASM_OUT}/libsvg.wasm.a")
    add_library(skia_svg STATIC IMPORTED)
    set_target_properties(skia_svg PROPERTIES
        IMPORTED_LOCATION "${SKIA_WASM_OUT}/libsvg.wasm.a"
    )
    set(SKIA_SVG_LIB skia_svg)
else()
    set(SKIA_SVG_LIB "")
    message(WARNING "Skia SVG module not found — SVG support will be limited")
endif()

# Collect all Skia dependency libraries (freetype, png, zlib, etc.)
set(SKIA_EXTRA_LIBS "")
foreach(SKIA_MOD_LIB skresources skcms skshaper freetype2 png zlib expat wuffs jpeg webp webp_sse41 icu skunicode_core skunicode_icu jpeg12 jpeg16)
    if(EXISTS "${SKIA_WASM_OUT}/lib${SKIA_MOD_LIB}.wasm.a")
        add_library(skia_${SKIA_MOD_LIB} STATIC IMPORTED)
        set_target_properties(skia_${SKIA_MOD_LIB} PROPERTIES
            IMPORTED_LOCATION "${SKIA_WASM_OUT}/lib${SKIA_MOD_LIB}.wasm.a"
        )
        list(APPEND SKIA_EXTRA_LIBS skia_${SKIA_MOD_LIB})
    endif()
endforeach()

# ============================================================================
# Web sources
# ============================================================================
set(WEB_SOURCES
    ${BUDO_ROOT}/src/web/web_main.c
    ${BUDO_ROOT}/src/web/web_window.c
    ${BUDO_ROOT}/src/core/window_canvas_texture.c
    ${BUDO_ROOT}/src/web/web_input.c
    ${BUDO_ROOT}/src/web/web_audio.c
    ${BUDO_ROOT}/src/web/web_midi.c
    ${BUDO_ROOT}/src/web/web_network.c
    ${BUDO_ROOT}/src/web/web_magneto.c
    ${BUDO_ROOT}/src/web/web_device.c
    ${BUDO_ROOT}/src/device/device_service.c
    ${BUDO_ROOT}/src/core/input.c
    ${BUDO_ROOT}/src/core/path_util.c
    ${BUDO_ROOT}/src/core/app_entrypoint.c
    ${BUDO_ROOT}/src/core/app_metadata.c
    ${BUDO_ROOT}/src/core/json_util.c
    ${BUDO_ROOT}/src/core/ts_strip.c
    ${BUDO_ROOT}/src/core/gl_shader_dialect.c
    ${BUDO_ROOT}/src/graphics/gl_shader_pipeline.c
    ${BUDO_ROOT}/src/graphics/gl_render_target.c
    ${BUDO_ROOT}/src/core/capabilities.c
    ${BUDO_ROOT}/src/core/api_error.c
    ${BUDO_ROOT}/src/core/js_capabilities_bindings.c
    ${BUDO_ROOT}/src/core/lua_capabilities_bindings.c
    ${BUDO_ROOT}/src/core/managed_basics.c
    ${BUDO_ROOT}/src/core/managed_runtime.c
    ${BUDO_ROOT}/src/core/managed_subsystems.c
    ${BUDO_ROOT}/src/core/subsystem_queue.c
    ${BUDO_ROOT}/src/core/subsystem_registry.c
    ${BUDO_ROOT}/src/core/subsystem_composition.c
    ${BUDO_ROOT}/src/graphics/skia_wrapper.cpp
    ${BUDO_ROOT}/src/graphics/js_canvas_bindings.c
    ${BUDO_ROOT}/src/graphics/js_core_bindings.c
    ${BUDO_ROOT}/src/graphics/js_gl_bindings.c
    ${BUDO_ROOT}/src/graphics/lua_canvas_bindings.c
    ${BUDO_ROOT}/src/graphics/lua_gl_bindings.c
    ${BUDO_ROOT}/src/graphics/lua_transform_bindings.c
    ${BUDO_ROOT}/src/graphics/image_loader.c
    ${BUDO_ROOT}/src/graphics/gl_state_guard.c
    ${BUDO_ROOT}/src/graphics/gpu_resource_state.c
    ${BUDO_ROOT}/src/graphics/color_util.c
    ${BUDO_ROOT}/src/math/math_wrapper.c
    ${BUDO_ROOT}/src/math/js_math_bindings.c
    ${BUDO_ROOT}/src/math/lua_math_bindings.c
    ${BUDO_ROOT}/src/audio/audio_service.c
    ${BUDO_ROOT}/src/audio/js_audio_bindings.c
    ${BUDO_ROOT}/src/audio/lua_audio_bindings.c
    ${BUDO_ROOT}/src/audio/audio_decoder.c
    ${BUDO_ROOT}/src/midi/js_midi_bindings.c
    ${BUDO_ROOT}/src/midi/lua_midi_bindings.c
    ${BUDO_ROOT}/src/midi/midi_service.c
    ${BUDO_ROOT}/src/midi/midi_topology.c
    ${BUDO_ROOT}/src/network/network_wrapper.c
    ${BUDO_ROOT}/src/network/network_policy.c
    ${BUDO_ROOT}/src/network/network_request_core.c
    ${BUDO_ROOT}/src/network/network_service.c
    ${BUDO_ROOT}/src/network/js_network_bindings.c
    ${BUDO_ROOT}/src/network/lua_network_bindings.c
    ${BUDO_ROOT}/src/web/web_sqlite.c
    ${BUDO_ROOT}/src/sqlite/sqlite_wrapper.c
    ${BUDO_ROOT}/src/sqlite/sqlite_service.c
    ${BUDO_ROOT}/src/sqlite/js_sqlite_bindings.c
    ${BUDO_ROOT}/src/sqlite/lua_sqlite_bindings.c
    ${BUDO_ROOT}/src/file/file_wrapper.c
    ${BUDO_ROOT}/src/file/file_service.c
    ${BUDO_ROOT}/src/file/js_file_bindings.c
    ${BUDO_ROOT}/src/file/lua_file_bindings.c
    ${BUDO_ROOT}/src/magneto/js_magneto_bindings.c
    ${BUDO_ROOT}/src/magneto/lua_magneto_bindings.c
    ${BUDO_ROOT}/src/magneto/magneto_service.c
    ${BUDO_ROOT}/src/device/js_device_bindings.c
    ${BUDO_ROOT}/src/device/lua_device_bindings.c
)

add_executable(budo ${WEB_SOURCES})

add_dependencies(budo web_generated_files)

target_include_directories(budo PRIVATE
    ${BUDO_ROOT}/src
    ${BUDO_ROOT}/third_party/stb
    ${miniaudio_SOURCE_DIR}
    ${SKIA_WASM_DIR}
    ${SKIA_WASM_DIR}/include
    ${SKIA_WASM_DIR}/modules/svg/include
    ${WEB_GENERATED_DIR}
)

target_link_libraries(budo PRIVATE quickjs lua sqlite3 skia ${SKIA_SVG_LIB} ${SKIA_EXTRA_LIBS})

# ── Advanced graphics surface ────────────────────────────────────────────
# Web links full Skia-WASM (same skia_wrapper.cpp the desktop and Android
# builds compile).
target_compile_definitions(budo PRIVATE)

# ============================================================================
# Emscripten link flags
# ============================================================================
# PROJECT_ASSET_DIR can be set at build time to preload a project folder:
#   emcmake cmake -DPROJECT_ASSET_DIR=examples/hello_world ...
set(PROJECT_ASSET_DIR "" CACHE PATH "Project directory to preload into the WASM virtual filesystem")

set(PRELOAD_PROJECT_ASSET_DIR "")
if(PROJECT_ASSET_DIR)
    if(NOT IS_ABSOLUTE "${PROJECT_ASSET_DIR}")
        set(PROJECT_ASSET_DIR "${BUDO_ROOT}/${PROJECT_ASSET_DIR}")
    endif()
    if(NOT EXISTS "${PROJECT_ASSET_DIR}")
        message(WARNING "PROJECT_ASSET_DIR does not exist: ${PROJECT_ASSET_DIR}")
    endif()

    set(PROJECT_ASSET_STAGE_DIR "${CMAKE_BINARY_DIR}/generated/project-assets")
    file(REMOVE_RECURSE "${PROJECT_ASSET_STAGE_DIR}")
    file(MAKE_DIRECTORY "${PROJECT_ASSET_STAGE_DIR}")
    file(COPY "${PROJECT_ASSET_DIR}/" DESTINATION "${PROJECT_ASSET_STAGE_DIR}")

    if(EXISTS "${PROJECT_ASSET_STAGE_DIR}/main.wat" AND NOT EXISTS "${PROJECT_ASSET_STAGE_DIR}/main.wasm")
        get_filename_component(EMSCRIPTEN_COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(EMSCRIPTEN_UPSTREAM_DIR "${EMSCRIPTEN_COMPILER_DIR}" DIRECTORY)
        find_program(WASM_AS_EXECUTABLE wasm-as
            HINTS
                "${EMSCRIPTEN_UPSTREAM_DIR}/bin"
                "${BUDO_ROOT}/third_party/emsdk/upstream/bin"
        )
        if(NOT WASM_AS_EXECUTABLE)
            message(FATAL_ERROR "main.wat requires wasm-as to stage a browser-runnable main.wasm")
        endif()
        execute_process(
            COMMAND "${WASM_AS_EXECUTABLE}" "${PROJECT_ASSET_STAGE_DIR}/main.wat" -o "${PROJECT_ASSET_STAGE_DIR}/main.wasm"
            RESULT_VARIABLE WAT_COMPILE_RESULT
            ERROR_VARIABLE WAT_COMPILE_ERROR
        )
        if(NOT WAT_COMPILE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to compile main.wat for web runtime:\n${WAT_COMPILE_ERROR}")
        endif()
        message(STATUS "Compiled main.wat to staged main.wasm for web runtime")
    endif()

    set(PRELOAD_PROJECT_ASSET_DIR "${PROJECT_ASSET_STAGE_DIR}")
endif()

set(EM_LINK_FLAGS
    "-sWASM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"stringToNewUTF8\"]"
    "-sFORCE_FILESYSTEM=1"
    "-lidbfs.js"
    "-sEXPORTED_FUNCTIONS=[\"_main\",\"_malloc\",\"_free\",\"_js_file_picker_complete\",\"_js_file_save_complete\",\"_web_midi_on_message\",\"_web_midi_on_sysex\",\"_web_network_on_success\",\"_web_network_on_error\",\"_web_sqlite_on_ready\",\"_budo_web_wasm_mark_ready\",\"_budo_web_wasm_app_exit\",\"_budo_web_wasm_request_graphics\",\"_budo_web_wasm_canvas_clear\",\"_budo_web_wasm_canvas_draw_rect\",\"_budo_web_wasm_canvas_draw_round_rect\",\"_budo_web_wasm_canvas_draw_circle\",\"_budo_web_wasm_canvas_draw_line\",\"_budo_web_wasm_canvas_draw_text\",\"_budo_web_wasm_canvas_set_fill_color\",\"_budo_web_wasm_canvas_set_stroke_color\",\"_budo_web_wasm_canvas_set_stroke_width\",\"_budo_web_wasm_canvas_set_anti_alias\",\"_budo_web_wasm_canvas_set_alpha\",\"_budo_web_wasm_transform_save\",\"_budo_web_wasm_transform_restore\",\"_budo_web_wasm_transform_translate\",\"_budo_web_wasm_transform_rotate\",\"_budo_web_wasm_transform_scale\",\"_budo_web_wasm_path_create\",\"_budo_web_wasm_path_move_to\",\"_budo_web_wasm_path_line_to\",\"_budo_web_wasm_path_close\",\"_budo_web_wasm_canvas_draw_path\",\"_budo_web_wasm_window_get_width\",\"_budo_web_wasm_window_get_height\",\"_budo_web_wasm_input_get_mouse_x\",\"_budo_web_wasm_input_get_mouse_y\",\"_budo_web_wasm_input_get_mouse_button\",\"_budo_web_wasm_gl_create_program\",\"_budo_web_wasm_gl_draw_fullscreen\",\"_budo_web_wasm_gl_set_uniform_1f\",\"_budo_web_wasm_gl_set_uniform_2f\",\"_budo_web_wasm_gl_set_uniform_matrix4fv\",\"_budo_web_wasm_gl_create_buffer\",\"_budo_web_wasm_gl_buffer_data\",\"_budo_web_wasm_gl_create_vertex_layout\",\"_budo_web_wasm_gl_set_attribute\",\"_budo_web_wasm_gl_set_index_buffer\",\"_budo_web_wasm_gl_draw_mesh\",\"_budo_web_wasm_network_fetch\",\"_budo_web_wasm_network_fetch_status\",\"_budo_web_wasm_network_fetch_status_code\",\"_budo_web_wasm_network_fetch_response_body_len\",\"_budo_web_wasm_network_fetch_response_body\",\"_budo_web_wasm_network_fetch_release\",\"_budo_web_wasm_network_shutdown\"]"
    "-sUSE_WEBGL2=1"
    "-sFULL_ES3=1"
    "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2"
    # Emscripten's default 64 KB stack is too small for runtime start-up
    # (e.g. the 16 KB NetworkPolicy parsed while composing subsystems), and an
    # overflow silently corrupts the static data below the stack.
    "-sSTACK_SIZE=1048576"
    "--shell-file" "${BUDO_ROOT}/web/shell.html"
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Abort with a clear error instead of corrupting memory on overflow.
    list(APPEND EM_LINK_FLAGS "-sSTACK_OVERFLOW_CHECK=2")
endif()

if(PRELOAD_PROJECT_ASSET_DIR)
    # Preload the project directory at / in the virtual FS
    list(APPEND EM_LINK_FLAGS "--preload-file" "${PRELOAD_PROJECT_ASSET_DIR}@/")
    message(STATUS "Preloading assets from: ${PROJECT_ASSET_DIR}")
endif()

# Join list into a space-separated string for LINK_FLAGS
list(JOIN EM_LINK_FLAGS " " EM_LINK_FLAGS_STR)

set_target_properties(budo PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "${EM_LINK_FLAGS_STR}"
)

# ============================================================================
# Status
# ============================================================================
message(STATUS "===========================================")
message(STATUS "Budo Web Build (Skia GPU)")
message(STATUS "  Emscripten:  ${EMSCRIPTEN_VERSION}")
message(STATUS "  QuickJS:     ${BUDO_QUICKJS_IMPL}")
message(STATUS "  Lua:         ${LUA_VERSION}")
message(STATUS "  Skia:        ${SKIA_WASM_DIR}")
if(PROJECT_ASSET_DIR)
    message(STATUS "  Assets:      ${PROJECT_ASSET_DIR}")
else()
    message(STATUS "  Assets:      (none — set -DPROJECT_ASSET_DIR=...)")
endif()
message(STATUS "===========================================")
