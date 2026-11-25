execute_process(COMMAND "${BUDO}" run --record
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "--record requires a value")
    message(FATAL_ERROR "Missing recording filename was not rejected: ${error}")
endif()
execute_process(COMMAND "${BUDO}" run --record invalid.json
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "ending in .mp4")
    message(FATAL_ERROR "Invalid recording extension was not rejected: ${error}")
endif()

file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/app.json" "{\"icon\":\"icon.svg\"}\n")
file(WRITE "${WORK}/icon.svg" [=[
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16">
<rect width="16" height="16" fill="#ff0000"/>
</svg>
]=])
file(WRITE "${WORK}/main.js" [=[
sys.canvas.clear('#ff0000');
let frames = 0;
function frame() {
    sys.canvas.clear('#ff0000');
    if (++frames === 4) sys.exit(0);
    sys.animation.requestFrame(frame);
}
sys.animation.requestFrame(frame);
]=])
execute_process(COMMAND "${BUDO}" run "${WORK}"
    --width 96 --height 64 --record "${WORK}/custom name.mp4"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(error MATCHES "Failed to create window")
    message("SKIP: No graphics window available")
    return()
endif()
if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/custom name.mp4" OR
   NOT EXISTS "${WORK}/custom name.json")
    message(FATAL_ERROR "CLI recording failed (${result}): ${output}\n${error}")
endif()
file(READ "${WORK}/custom name.json" json)
string(JSON fit GET "${json}" fit)
string(JSON width GET "${json}" width)
if(NOT fit STREQUAL "contain" OR width LESS 96)
    message(FATAL_ERROR "Invalid recording JSON: ${json}")
endif()

file(WRITE "${WORK}/main.js" "console.log('no graphics');\n")
execute_process(COMMAND "${BUDO}" run "${WORK}/main.js" --record "${WORK}/headless.mp4"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(result EQUAL 0 OR NOT error MATCHES "did not open a recordable window" OR
   EXISTS "${WORK}/headless.mp4")
    message(FATAL_ERROR "Headless recording did not fail explicitly: ${output}\n${error}")
endif()
file(REMOVE "${WORK}/main.js" "${WORK}/app.json" "${WORK}/icon.svg"
    "${WORK}/custom name.mp4" "${WORK}/custom name.json")
