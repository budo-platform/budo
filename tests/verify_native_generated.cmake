if(NOT EXISTS "${BUILD_DIR}/compile_commands.json")
    message(FATAL_ERROR "Generated native build did not emit compile_commands.json")
endif()

if(NOT EXISTS "${BUILD_DIR}/bin/Native_Project_Fixture${EXECUTABLE_SUFFIX}")
    message(FATAL_ERROR "Generated native executable was not built")
endif()

if(NOT EXISTS "${BUILD_DIR}/bin/assets/message.txt")
    message(FATAL_ERROR "Generated native build did not stage declared assets")
endif()