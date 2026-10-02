# Usage: cmake -DAPP=<executable> -DEXPECTED=<status> -P expect_exit_status.cmake
execute_process(COMMAND "${APP}" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
if(NOT status STREQUAL "${EXPECTED}")
    message(FATAL_ERROR "${APP} exited with '${status}', expected ${EXPECTED}\n${out}${err}")
endif()
message(STATUS "${APP} exited with ${status}: ${out}")
