if(NOT DEFINED CMAKE_COMMAND_PATH OR NOT DEFINED BUILD_DIR OR NOT DEFINED BUDO OR
   NOT DEFINED PROJECT_DIR OR NOT DEFINED SDK_STAGE OR NOT DEFINED TEST_ROOT OR
   NOT DEFINED CONFIGURATION OR NOT DEFINED PYTHON OR NOT DEFINED PACKAGER OR
   NOT DEFINED TARGET_TUPLE)
    message(FATAL_ERROR "Missing native SDK resolution test argument")
endif()

file(REMOVE_RECURSE "${SDK_STAGE}" "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(ARCHIVE "${TEST_ROOT}/budo-native-sdk-${TARGET_TUPLE}.tar.gz")
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" --install "${BUILD_DIR}" --prefix "${SDK_STAGE}"
            --component NativeSDK --config "${CONFIGURATION}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native SDK staging failed: ${result}")
endif()
execute_process(
    COMMAND "${PYTHON}" "${PACKAGER}" --stage "${SDK_STAGE}"
            --output "${ARCHIVE}" --version 0.4.3 --tuple "${TARGET_TUPLE}"
            --compiler host-compatible --baseline host-test
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Native SDK packaging failed: ${result}")
endif()
set(RELEASE "${ARCHIVE}.release.json")
file(SHA256 "${RELEASE}" RELEASE_SHA)
set(SDK_CACHE "${TEST_ROOT}/sdk-cache")
set(BUILD_CACHE "${TEST_ROOT}/build-cache")
set(FIRST_BUILD "${TEST_ROOT}/online-build")
set(SECOND_BUILD "${TEST_ROOT}/offline-build")
set(COMMON_ENV
    "BUDO_NATIVE_SDK_DISABLE_LOCAL=1"
    "BUDO_NATIVE_SDK_RELEASE_METADATA_URL=file://${RELEASE}"
    "BUDO_NATIVE_SDK_RELEASE_METADATA_SHA256=${RELEASE_SHA}"
    "BUDO_NATIVE_SDK_CACHE_DIR=${SDK_CACHE}"
    "BUDO_NATIVE_CACHE_DIR=${BUILD_CACHE}")

execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" compile "${PROJECT_DIR}" --build-dir "${FIRST_BUILD}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Automatic downloaded SDK compilation failed: ${result}")
endif()
file(READ "${FIRST_BUILD}/budo-native-build.json" provenance)
if(NOT provenance MATCHES "\"sdk_source\": \"downloaded\"" OR
   NOT provenance MATCHES "\"sdk_manifest_sha256\": \"[0-9a-f]+\"" OR
   NOT provenance MATCHES "\"release_metadata_sha256\": \"${RELEASE_SHA}\"")
    message(FATAL_ERROR "Downloaded SDK provenance is incomplete")
endif()

# The immutable sources disappear: offline resolution must use only the pinned
# metadata copy and verified content-addressed archive already in the SDK cache.
file(REMOVE "${RELEASE}" "${ARCHIVE}")
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" compile "${PROJECT_DIR}" --offline --build-dir "${SECOND_BUILD}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Offline cached SDK compilation failed: ${result}")
endif()

file(GLOB SDK_ENTRIES "${SDK_CACHE}/sha256/*")
list(LENGTH SDK_ENTRIES entry_count)
if(NOT entry_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one content-addressed SDK entry")
endif()
list(GET SDK_ENTRIES 0 SDK_ENTRY)
set(SDK_DIR "${SDK_ENTRY}/sdk")
set(CORRUPT_FILE "${SDK_DIR}/include/budo/budo.h")
file(APPEND "${CORRUPT_FILE}" "\ncorruption\n")
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" compile "${PROJECT_DIR}" --offline
            --build-dir "${TEST_ROOT}/recovered-build"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Offline SDK corruption recovery from cached archive failed: ${result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" cache native clean --all
    RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT EXISTS "${SDK_ENTRY}/complete")
    message(FATAL_ERROR "Native build-cache clean incorrectly touched SDK storage")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" cache sdk inspect --json
    OUTPUT_VARIABLE inspect RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT inspect MATCHES "\"namespace\":\"native-sdks\"" OR
   NOT inspect MATCHES "\"count\":1")
    message(FATAL_ERROR "SDK cache inspection diagnostics are incomplete: ${inspect}")
endif()

# With both installed contents and cached archive corrupted, offline mode must
# fail closed rather than use either artifact.
file(APPEND "${CORRUPT_FILE}" "again\n")
file(APPEND "${SDK_ENTRY}/archive.tar.gz" "bad\n")
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" compile "${PROJECT_DIR}" --offline
            --build-dir "${TEST_ROOT}/must-fail"
    RESULT_VARIABLE result)
if(result EQUAL 0)
    message(FATAL_ERROR "Offline resolution accepted corrupt SDK/archive")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env ${COMMON_ENV}
            "${BUDO}" cache sdk clean --all
    RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR EXISTS "${SDK_ENTRY}")
    message(FATAL_ERROR "SDK-only cache cleanup failed")
endif()
