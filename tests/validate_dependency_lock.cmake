if(NOT DEFINED BUDO_SOURCE_DIR)
    message(FATAL_ERROR "BUDO_SOURCE_DIR is required")
endif()

set(lock_file "${BUDO_SOURCE_DIR}/cmake/BudoDependencyLock.cmake")
if(NOT EXISTS "${lock_file}")
    message(FATAL_ERROR "Shared dependency lock is missing: ${lock_file}")
endif()

file(READ "${BUDO_SOURCE_DIR}/CMakeLists.txt" root_cmake)
file(READ "${BUDO_SOURCE_DIR}/web/CMakeLists.txt" web_cmake)
file(READ "${BUDO_SOURCE_DIR}/Makefile" makefile_text)
file(READ "${BUDO_SOURCE_DIR}/scripts/build-web.sh" build_web_script)
file(READ "${BUDO_SOURCE_DIR}/scripts/_common.sh" common_script)
file(READ "${BUDO_SOURCE_DIR}/scripts/bundle-linux-appimage.sh" appimage_script)
file(READ "${BUDO_SOURCE_DIR}/scripts/windows-provision.ps1" windows_provision)
file(READ "${BUDO_SOURCE_DIR}/scripts/windows-build.ps1" windows_build)
file(READ "${BUDO_SOURCE_DIR}/private/android/android/app/src/main/cpp/CMakeLists.txt"
    android_cmake)

string(FIND "${root_cmake}"
    [=[include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BudoDependencyLock.cmake")]=]
    root_lock_include)
string(FIND "${web_cmake}"
    [=[include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BudoDependencyLock.cmake")]=]
    web_lock_include)

string(FIND "${android_cmake}" [=[include("${BUDO_DEPENDENCY_LOCK}")]=]
    android_lock_include)

if(root_lock_include EQUAL -1 OR web_lock_include EQUAL -1 OR android_lock_include EQUAL -1)
    message(FATAL_ERROR "Root, web, and Android CMake configurations must consume the shared dependency lock")
endif()

include("${lock_file}")
if(NOT BUDO_DEPENDENCY_LOCK_VERSION EQUAL 1)
    message(FATAL_ERROR "Unexpected dependency lock version")
endif()
budo_report_dependency_lock("consistency-test")

foreach(contents_var root_cmake web_cmake android_cmake)
    string(FIND "${${contents_var}}"
        [=[set(BUDO_QUICKJS_IMPL "${BUDO_QUICKJS_IMPL_DEFAULT}" CACHE STRING]=]
        default_use)
    if(default_use EQUAL -1)
        message(FATAL_ERROR "${contents_var} does not derive its QuickJS default from the lock")
    endif()
endforeach()

string(FIND "${makefile_text}" "QUICKJS_IMPL ?= $(LOCKED_QUICKJS_IMPL)" make_default)
string(FIND "${makefile_text}"
    [=[@$(MAKE) $(BUDO_ANDROID_EVERYTHING_TARGETS)]=] android_bundle_release_step)
string(FIND "${makefile_text}" [=[@$(MAKE) rebuild]=] native_release_rebuild_step)
if(android_bundle_release_step EQUAL -1 OR native_release_rebuild_step EQUAL -1 OR
   NOT android_bundle_release_step LESS native_release_rebuild_step)
    message(FATAL_ERROR
        "The everything pipeline must pin the Android support bundle before rebuilding release binaries")
endif()
string(FIND "${build_web_script}"
    [=[budo_dependency_lock_value BUDO_QUICKJS_IMPL_DEFAULT]=] script_default)
string(FIND "${common_script}"
    [=[budo_dependency_lock_value BUDO_EMSDK_VERSION]=] emsdk_lock_use)
string(FIND "${appimage_script}"
    [=[budo_dependency_lock_value BUDO_APPIMAGETOOL_VERSION]=] appimagetool_lock_use)
string(FIND "${appimage_script}" [=[EXPECTED_APPIMAGETOOL_SHA]=] appimagetool_hash_use)
string(FIND "${windows_provision}" [=[BUDO_SDL2_WINDOWS_URL_HASH]=] windows_sdl_lock_use)
string(FIND "${windows_provision}" [=[Get-FileHash -Algorithm SHA256]=] windows_sdl_hash_use)
string(FIND "${windows_build}" [=[OPENSSL_ROOT_DIR]=] windows_openssl_use)
file(READ "${BUDO_SOURCE_DIR}/scripts/setup-onnx.sh" onnx_script)
file(READ "${BUDO_SOURCE_DIR}/private/android/scripts/setup-onnx-android.sh" android_onnx_script)
string(FIND "${onnx_script}" [=[budo_dependency_lock_value BUDO_ONNXRUNTIME_VERSION]=]
    onnx_lock_use)
string(FIND "${android_onnx_script}" [=[budo_dependency_lock_value BUDO_ONNXRUNTIME_VERSION]=]
    android_onnx_lock_use)
string(FIND "${onnx_script}" [=[budo_dependency_lock_value BUDO_ONNXRUNTIME_OSX_ARM64_URL_HASH]=]
    onnx_hash_use)
string(FIND "${android_onnx_script}" [=[budo_dependency_lock_value BUDO_ONNXRUNTIME_ANDROID_AAR_URL_HASH]=]
    android_onnx_hash_use)
if(make_default EQUAL -1 OR script_default EQUAL -1 OR emsdk_lock_use EQUAL -1 OR
    appimagetool_lock_use EQUAL -1 OR appimagetool_hash_use EQUAL -1 OR
    windows_sdl_lock_use EQUAL -1 OR windows_sdl_hash_use EQUAL -1 OR
    windows_openssl_use EQUAL -1 OR
   onnx_lock_use EQUAL -1 OR android_onnx_lock_use EQUAL -1 OR
   onnx_hash_use EQUAL -1 OR android_onnx_hash_use EQUAL -1)
    message(FATAL_ERROR "Makefile and setup/build scripts must derive defaults from the lock")
endif()

foreach(hash_var IN ITEMS
        BUDO_ONNXRUNTIME_OSX_ARM64_URL_HASH
        BUDO_ONNXRUNTIME_OSX_X86_64_URL_HASH
        BUDO_ONNXRUNTIME_LINUX_X64_URL_HASH
        BUDO_ONNXRUNTIME_WINDOWS_X64_URL_HASH
        BUDO_ONNXRUNTIME_ANDROID_AAR_URL_HASH
        BUDO_ANDROID_SUPPORT_BUNDLE_URL_HASH
        BUDO_APPIMAGETOOL_X86_64_URL_HASH
        BUDO_APPIMAGETOOL_AARCH64_URL_HASH
        BUDO_SDL2_WINDOWS_URL_HASH)
    string(LENGTH "${${hash_var}}" hash_length)
    if(NOT hash_length EQUAL 71 OR NOT "${${hash_var}}" MATCHES "^SHA256=[0-9a-f]+$")
        message(FATAL_ERROR "${hash_var} is not a locked SHA-256 digest")
    endif()
endforeach()

set(corrupt_archive "${CMAKE_CURRENT_BINARY_DIR}/dependency-lock-corrupt.bin")
set(corrupt_check "${CMAKE_CURRENT_BINARY_DIR}/dependency-lock-corrupt-check.cmake")
file(WRITE "${corrupt_archive}" "corrupted dependency archive\n")
file(WRITE "${corrupt_check}"
    "include(\"${lock_file}\")\n"
    "budo_verify_locked_archive(\"${corrupt_archive}\" \"SHA256=0000000000000000000000000000000000000000000000000000000000000000\")\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${corrupt_check}"
    RESULT_VARIABLE corrupt_result OUTPUT_VARIABLE corrupt_output ERROR_VARIABLE corrupt_error)
file(REMOVE "${corrupt_archive}" "${corrupt_check}")
if(corrupt_result EQUAL 0 OR NOT "${corrupt_output}${corrupt_error}" MATCHES "hash mismatch")
    message(FATAL_ERROR "Locked archive verification did not reject corrupted content")
endif()

foreach(contents_var root_cmake web_cmake android_cmake)
    if("${${contents_var}}" MATCHES "GIT_TAG[ \t]+(master|main|0\\.11\\.25)")
        message(FATAL_ERROR "${contents_var} contains a mutable or non-commit Git dependency")
    endif()
endforeach()

foreach(contents_var root_cmake web_cmake)
    foreach(hash_var
            BUDO_QUICKJS_BELLARD_URL_HASH
            BUDO_LUA_URL_HASH
            BUDO_SQLITE_URL_HASH)
        string(FIND "${${contents_var}}" "${hash_var}" hash_use)
        if(hash_use EQUAL -1)
            message(FATAL_ERROR "${contents_var} does not use ${hash_var}")
        endif()
    endforeach()
endforeach()

message(STATUS "Dependency defaults agree: QuickJS=${BUDO_QUICKJS_IMPL_DEFAULT}@${BUDO_QUICKJS_NG_GIT_TAG}")