file(REMOVE_RECURSE "${FIRST_BUILD_DIR}" "${SECOND_BUILD_DIR}" "${CACHE_DIR}")
execute_process(
    COMMAND "${BUDO}" compile "${PROJECT_DIR}" --build-dir "${FIRST_BUILD_DIR}"
    RESULT_VARIABLE first_result)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "Initial native cache build failed: ${first_result}")
endif()
execute_process(
    COMMAND "${BUDO}" compile "${PROJECT_DIR}" --build-dir "${SECOND_BUILD_DIR}"
    RESULT_VARIABLE second_result)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "Native cache-hit build failed: ${second_result}")
endif()
