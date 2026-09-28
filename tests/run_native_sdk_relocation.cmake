if(NOT DEFINED CMAKE_COMMAND_PATH OR NOT DEFINED BUILD_DIR OR NOT DEFINED BUDO OR
   NOT DEFINED PROJECT_DIR OR NOT DEFINED SDK_STAGE OR NOT DEFINED SDK_RELOCATED OR
    NOT DEFINED APP_BUILD_DIR OR NOT DEFINED CONFIGURATION OR NOT DEFINED PYTHON OR
    NOT DEFINED PACKAGER OR NOT DEFINED TARGET_TUPLE)
    message(FATAL_ERROR "Missing native SDK relocation test argument")
endif()

file(REMOVE_RECURSE "${SDK_STAGE}" "${SDK_RELOCATED}" "${APP_BUILD_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" --install "${BUILD_DIR}" --prefix "${SDK_STAGE}"
            --component NativeSDK --config "${CONFIGURATION}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native SDK staging failed: ${result}")
endif()
execute_process(
    COMMAND "${PYTHON}" "${PACKAGER}" --stage "${SDK_STAGE}"
            --output "${SDK_STAGE}.tar.gz" --version 0.4.3
            --tuple "${TARGET_TUPLE}" --compiler host-compatible
            --baseline host-test
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native SDK manifest/package generation failed: ${result}")
endif()
file(COPY "${SDK_STAGE}/" DESTINATION "${SDK_RELOCATED}")
file(READ "${SDK_RELOCATED}/lib/cmake/BudoNative/BudoNativeConfig.cmake" config)
if(config MATCHES "${BUILD_DIR}" OR config MATCHES "${CMAKE_SOURCE_DIR}")
    message(FATAL_ERROR "Relocated SDK CMake package contains a checkout path")
endif()
execute_process(
    COMMAND "${BUDO}" compile "${PROJECT_DIR}" --sdk "${SDK_RELOCATED}"
            --build-dir "${APP_BUILD_DIR}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Compilation through relocated native SDK failed: ${result}")
endif()
file(READ "${APP_BUILD_DIR}/budo-native-build.json" provenance)
if(NOT provenance MATCHES "\"sdk_source\": \"explicit\"")
    message(FATAL_ERROR "Relocated SDK provenance does not record explicit selection")
endif()
if(NOT provenance MATCHES "\"target_tuple\"")
    message(FATAL_ERROR "Relocated SDK provenance has no target tuple")
endif()
