function(budo_read_version output file pattern)
    if(EXISTS "${file}")
        file(STRINGS "${file}" version_line REGEX "${pattern}" LIMIT_COUNT 1)
        if(version_line MATCHES "${pattern}")
            set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(budo_read_triplet output file major minor patch)
    foreach(part IN ITEMS major minor patch)
        budo_read_version(${part}_version "${file}" "^#define ${${part}}[ \t]+([0-9]+)")
        if(NOT DEFINED ${part}_version)
            return()
        endif()
    endforeach()
    set(${output} "${major_version}.${minor_version}.${patch_version}" PARENT_SCOPE)
endfunction()

function(budo_source_revision output directory)
    # Do not let Git walk up into Budo's own checkout for unpacked sources.
    if(GIT_FOUND AND EXISTS "${directory}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${directory}" OUTPUT_VARIABLE revision
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE result)
        if(result EQUAL 0 AND revision MATCHES "^[0-9a-f]+$")
            set(${output} "${revision}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(budo_add_version_report target)
    if(BUDO_QUICKJS_IMPL STREQUAL "ng")
        set(quickjs_revision "${BUDO_QUICKJS_NG_GIT_TAG} (lock)")
        budo_source_revision(quickjs_revision "${quickjs_SOURCE_DIR}")
        budo_read_triplet(quickjs_version "${quickjs_SOURCE_DIR}/quickjs.h"
            QJS_VERSION_MAJOR QJS_VERSION_MINOR QJS_VERSION_PATCH)
        set(javascript "QuickJS-NG (revision ${quickjs_revision})")
        if(DEFINED quickjs_version)
            set(javascript "QuickJS-NG ${quickjs_version} (revision ${quickjs_revision})")
        endif()
    else()
        set(javascript "QuickJS (Bellard) ${BUDO_QUICKJS_BELLARD_VERSION}")
    endif()
    foreach(dep IN ITEMS miniaudio ogg opus opusfile llamacpp)
        string(TOUPPER "${dep}" upper)
        set(${dep}_revision "${BUDO_${upper}_GIT_TAG} (lock)")
        FetchContent_GetProperties(${dep} SOURCE_DIR ${dep}_source)
        budo_source_revision(${dep}_revision "${${dep}_source}")
    endforeach()
    budo_read_triplet(miniaudio_version "${miniaudio_source}/miniaudio.h"
        MA_VERSION_MAJOR MA_VERSION_MINOR MA_VERSION_REVISION)
    budo_read_version(ogg_version "${ogg_source}/configure.ac"
        "^AC_INIT\\(\\[libogg\\],\\[([0-9.]+)\\]")
    foreach(dep IN ITEMS miniaudio ogg)
        if(DEFINED ${dep}_version)
            set(${dep}_build "${${dep}_version} (revision ${${dep}_revision})")
        else()
            set(${dep}_build "revision ${${dep}_revision}")
        endif()
    endforeach()

    budo_read_version(sqlite "${SQLITE_DIR}/sqlite3.h"
        "^#define SQLITE_VERSION[ \t]+\"([^\"]+)\"")
    budo_read_version(wasmtime "${WASMTIME_DIR}/include/wasmtime.h"
        "^#define WASMTIME_VERSION[ \t]+\"([^\"]+)\"")
    budo_read_version(stb "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h"
        "^/\\* stb_image - v([0-9.]+)")
    budo_read_version(skia_milestone "${SKIA_INCLUDE_DIR}/core/SkMilestone.h"
        "^#define SK_MILESTONE[ \t]+([0-9]+)")
    set(skia "version unavailable")
    if(skia_milestone)
        set(skia "m${skia_milestone}")
    endif()
    budo_source_revision(skia_revision "${SKIA_DIR}")
    if(skia_revision)
        string(APPEND skia " (source revision ${skia_revision})")
    endif()

    set(expat "not linked separately")
    if(SKIA_EXPAT_LIBRARY)
        set(expat "version unavailable (from Skia)")
        budo_read_version(expat "${SKIA_DIR}/third_party/expat/include/expat_config/expat_config.h"
            "^#define PACKAGE_VERSION[ \t]+\"([^\"]+)\"")
    endif()
    set(onnx "not built")
    if(ENABLE_NEURAL)
        set(onnx "${ORT_VERSION}")
        budo_read_version(onnx "${ORT_DIR}/VERSION_NUMBER" "^([0-9][0-9A-Za-z.+-]+)")
        string(APPEND onnx " (runtime loaded on demand)")
    endif()
    set(llama "not built")
    if(ENABLE_LLAMACPP)
        set(llama "revision ${llamacpp_revision} (llama.cpp and ggml)")
    endif()
    set(alsa "not linked")
    if(ALSA_FOUND)
        set(alsa "${ALSA_VERSION_STRING}")
    endif()
    if(OPENSSL_VERSION AND NOT OpenSSL_VERSION)
        set(OpenSSL_VERSION "${OPENSSL_VERSION}")
    endif()
    if(ZLIB_VERSION_STRING AND NOT ZLIB_VERSION)
        set(ZLIB_VERSION "${ZLIB_VERSION_STRING}")
    endif()
    foreach(value IN ITEMS sqlite wasmtime stb alsa SDL2_VERSION ZLIB_VERSION
            OpenSSL_VERSION FREETYPE_VERSION_STRING Fontconfig_VERSION Vulkan_VERSION)
        if(NOT ${value})
            set(${value} "version unavailable")
        endif()
    endforeach()

    string(CONCAT report
        "\nRuntime engines:\n"
        "  JavaScript: ${javascript}\n"
        "  Lua: ${LUA_VERSION}\n"
        "  Wasmtime: ${wasmtime} (${WASMTIME_PLATFORM})\n"
        "\nDependencies (build-time):\n"
        "  SQLite: ${sqlite}\n"
        "  Skia: ${skia}\n"
        "  Expat: ${expat}\n"
        "  SDL2: ${SDL2_VERSION} (${BUDO_SDK_SDL_IMPORT_TYPE})\n"
        "  zlib: ${ZLIB_VERSION}\n"
        "  OpenSSL: ${OpenSSL_VERSION}\n"
        "  stb_image: ${stb}\n"
        "  miniaudio: ${miniaudio_build}\n"
        "  Ogg: ${ogg_build}\n"
        "  Opus: revision ${opus_revision}\n"
        "  opusfile: revision ${opusfile_revision}\n"
        "  llama.cpp: ${llama}\n"
        "  ONNX Runtime: ${onnx}\n"
        "  OpenGL: platform library (driver version requires a graphics context)\n"
        "  Threads: platform library\n")
    if(WIN32)
        string(APPEND report "  Windows SDK: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION} (User32, GDI32, Winsock)\n")
    elseif(APPLE)
        string(APPEND report
            "  macOS frameworks: Cocoa, IOKit, CoreVideo, CoreGraphics, CoreText,\n"
            "    CoreFoundation, Metal, CoreMIDI, CoreAudio, AudioToolbox, Security\n"
            "  macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}\n")
    else()
        string(APPEND report
            "  FreeType: ${FREETYPE_VERSION_STRING}\n"
            "  Fontconfig: ${Fontconfig_VERSION}\n"
            "  ALSA: ${alsa}\n")
    endif()
    if(ENABLE_LLAMACPP_CUDA)
        string(APPEND report "  CUDA compiler: ${CMAKE_CUDA_COMPILER_VERSION}\n")
    endif()
    if(ENABLE_LLAMACPP_VULKAN)
        string(APPEND report "  Vulkan: ${Vulkan_VERSION}\n")
    endif()

    string(APPEND report
        "\nBuild:\n"
        "  Configuration: $<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,unspecified>\n"
        "  CMake: ${CMAKE_VERSION} (${CMAKE_GENERATOR})\n"
        "  C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} (C${CMAKE_C_STANDARD})\n"
        "  C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (C++${CMAKE_CXX_STANDARD})\n"
        "  Python (build tool): ${Python3_VERSION}\n"
        "  Dependency lock: v${BUDO_DEPENDENCY_LOCK_VERSION}\n")
    foreach(flag IN ITEMS ENABLE_NEURAL ENABLE_LLAMACPP ENABLE_LLAMACPP_CUDA
            ENABLE_LLAMACPP_VULKAN BUDO_ENABLE_ANDROID_PACKAGING
            BUDO_EMBED_WEB_RUNTIME BUDO_WINDOWS_STANDALONE USE_SYSTEM_SKIA)
        string(APPEND report "  ${flag}: $<IF:$<BOOL:${${flag}}>,ON,OFF>\n")
    endforeach()
    if(ENABLE_LLAMACPP)
        foreach(flag IN ITEMS GGML_CPU GGML_NATIVE GGML_METAL GGML_OPENMP GGML_BACKEND_DL)
            string(APPEND report "  ${flag}: $<IF:$<BOOL:${${flag}}>,ON,OFF>\n")
        endforeach()
    endif()
    if(BUDO_ENABLE_ANDROID_PACKAGING)
        string(APPEND report
            "  Android support bundle: ${BUDO_ANDROID_SUPPORT_BUNDLE_TAG}\n"
            "  Android support bundle default SHA-256: ${BUDO_ANDROID_SUPPORT_BUNDLE_SHA256}\n"
            "  Android support bundle default URL: ${BUDO_ANDROID_SUPPORT_BASE_URL}/budo-android-support-${BUDO_ANDROID_SUPPORT_BUNDLE_SHA256}.tar.gz\n")
    endif()

    set(configs ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    list(REMOVE_DUPLICATES configs)
    if(NOT configs)
        set(configs "unspecified")
    endif()
    set(version_dir "${GENERATED_DIR}/version")
    # Give CMake a concrete generated output for each configuration: it may
    # evaluate target sources without a configuration during generation.
    foreach(config IN LISTS configs)
        string(TOUPPER "${config}" upper)
        set(config_report "${report}")
        foreach(flag IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
            string(STRIP "${${flag}} ${${flag}_${upper}}" flags)
            if(flags STREQUAL "")
                set(flags "(none)")
            endif()
            # CMake treats the contents of these flag variables as literal text.
            string(REPLACE "$<" "$<1:$><" flags "${flags}")
            string(APPEND config_report "  ${flag}: ${flags}\n")
        endforeach()
        string(APPEND config_report
            "  Budo link options: $<JOIN:$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},LINK_OPTIONS>>, >\n")
        set(config_dir "${version_dir}/${config}")
        set(config_selected "$<STREQUAL:${config},$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,unspecified>>")
        file(GENERATE OUTPUT "${config_dir}/version.txt" CONTENT "${config_report}"
            CONDITION "${config_selected}")
        add_custom_command(OUTPUT "${config_dir}/version_report.h"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
                "${config_dir}/version.txt" budo_version_report "${config_dir}/version_report.h"
            DEPENDS "${config_dir}/version.txt" "${CMAKE_SOURCE_DIR}/cmake/embed_file.py"
            VERBATIM)
        target_sources(${target} PRIVATE "$<${config_selected}:${config_dir}/version_report.h>")
    endforeach()

    set(report_dir "${version_dir}/$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,unspecified>")
    target_include_directories(${target} PRIVATE "${report_dir}")
endfunction()
