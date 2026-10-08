if(NOT DEFINED BUDO_EXECUTABLE OR NOT DEFINED PROJECT_DIR OR NOT DEFINED BUILD_CACHE)
    message(FATAL_ERROR "BUDO_EXECUTABLE, PROJECT_DIR, and BUILD_CACHE are required")
endif()

# The packager keeps one build directory per app in a cache (the user cache by
# default). Point it at a test-owned directory; clear_android_build_cache()
# before a run makes staged_android_app_root() find the one it produced.
set(ENV{BUDO_ANDROID_BUILD_CACHE_DIR} "${BUILD_CACHE}")
get_filename_component(android_build_cache_parent "${BUILD_CACHE}" DIRECTORY)
file(MAKE_DIRECTORY "${android_build_cache_parent}")

function(clear_android_build_cache)
    file(REMOVE_RECURSE "${BUILD_CACHE}")
endfunction()

function(staged_android_app_root out)
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${BUILD_CACHE}" "${BUILD_CACHE}/*")
    set(found "")
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${BUILD_CACHE}/${entry}" AND NOT entry MATCHES "^\\.")
            list(APPEND found "${BUILD_CACHE}/${entry}")
        endif()
    endforeach()
    list(LENGTH found count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected one Android build directory in ${BUILD_CACHE}, found: ${found}")
    endif()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

clear_android_build_cache()

execute_process(
    COMMAND "${BUDO_EXECUTABLE}" android-apk "${PROJECT_DIR}" --no-build
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native Android staging failed (${result}):\n${output}\n${error}")
endif()
staged_android_app_root(STAGING_ROOT)

set(native_root "${STAGING_ROOT}/android/app/src/main/cpp/budo_native_app")
set(asset_root "${STAGING_ROOT}/android/app/src/main/assets/app")
foreach(required
        "${native_root}/main.c"
        "${native_root}/budo-native-sources.cmake"
        "${asset_root}/budo_asset_manifest.txt")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Expected staged file is missing: ${required}")
    endif()
endforeach()
foreach(forbidden
        "${asset_root}/main.c"
        "${asset_root}/budo-native.json")
    if(EXISTS "${forbidden}")
        message(FATAL_ERROR "Native build input leaked into Android assets: ${forbidden}")
    endif()
endforeach()
if(DEFINED REQUIRED_ASSETS)
    file(READ "${asset_root}/budo_asset_manifest.txt" asset_manifest)
    foreach(asset IN LISTS REQUIRED_ASSETS)
        if(NOT EXISTS "${asset_root}/${asset}")
            message(FATAL_ERROR "Expected staged Android asset is missing: ${asset}")
        endif()
        string(FIND "${asset_manifest}" "${asset}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Android asset manifest lacks: ${asset}")
        endif()
    endforeach()
endif()
file(READ "${native_root}/budo-native-sources.cmake" manifest)
foreach(expected
        "set(BUDO_HAS_NATIVE_APP ON)"
        "BUDO_NATIVE_APP_C_STANDARD 11"
        "\${CMAKE_CURRENT_LIST_DIR}/main.c")
    string(FIND "${manifest}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Generated Android native manifest lacks: ${expected}")
    endif()
endforeach()

set(android_cmake "${STAGING_ROOT}/android/app/src/main/cpp/CMakeLists.txt")
set(android_main "${STAGING_ROOT}/android/app/src/main/cpp/android_main.cpp")
file(GLOB_RECURSE android_activity
    "${STAGING_ROOT}/android/app/src/main/java/*/BudoActivity.java")
file(GLOB_RECURSE android_view
    "${STAGING_ROOT}/android/app/src/main/java/*/BudoView.java")
list(LENGTH android_activity android_activity_count)
list(LENGTH android_view android_view_count)
if(NOT android_activity_count EQUAL 1 OR NOT android_view_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one staged BudoActivity and BudoView")
endif()
foreach(required_file "${android_cmake}" "${android_main}" "${android_activity}" "${android_view}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Expected Android lifecycle source is missing: ${required_file}")
    endif()
endforeach()

file(READ "${android_cmake}" android_cmake_content)
file(READ "${android_main}" android_main_content)
file(READ "${android_activity}" android_activity_content)
file(READ "${android_view}" android_view_content)
foreach(check
        "android_cmake_content;native/native_application_driver.c"
    "android_cmake_content;native/native_gpu.c"
        "android_cmake_content;BUDO_NATIVE_APPLICATION=1"
        "android_main_content;init_native_runtime()"
        "android_main_content;budo_get_application()"
    "android_main_content;budo_native_host_set_graphics_window(&g_state.nativeHost, g_state.window)"
        "android_main_content;nativeSurfaceDestroyed"
        "android_activity_content;pauseWithNativeLifecycle"
        "android_activity_content;destroyWithNativeLifecycle"
        "android_view_content;runOnRenderThreadAndWait"
        "android_view_content;nativeSurfaceDestroyed()")
    list(GET check 0 content_variable)
    list(GET check 1 expected)
    string(FIND "${${content_variable}}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Android lifecycle integration lacks: ${expected}")
    endif()
endforeach()
