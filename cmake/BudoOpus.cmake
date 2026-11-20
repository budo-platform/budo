include(FetchContent)

function(budo_add_opus_decode_backend)
    set(_budo_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(_budo_build_testing "${BUILD_TESTING}")
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(INSTALL_DOCS OFF)
    set(INSTALL_PKG_CONFIG_MODULE OFF)
    set(INSTALL_CMAKE_PACKAGE_MODULE OFF)
    set(OPUS_BUILD_TESTING OFF)
    set(OPUS_BUILD_PROGRAMS OFF)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

    foreach(dependency IN ITEMS ogg opus opusfile)
        string(TOUPPER "${dependency}" dependency_upper)
        if(BUDO_CODEC_DEPENDENCY_SOURCE_ROOT
           AND EXISTS "${BUDO_CODEC_DEPENDENCY_SOURCE_ROOT}/${dependency}-src/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_${dependency_upper}
                "${BUDO_CODEC_DEPENDENCY_SOURCE_ROOT}/${dependency}-src")
        endif()
    endforeach()

    FetchContent_Declare(
        ogg
        GIT_REPOSITORY ${BUDO_OGG_GIT_REPOSITORY}
        GIT_TAG        ${BUDO_OGG_GIT_TAG}
        GIT_SHALLOW    FALSE
    )
    FetchContent_Declare(
        opus
        GIT_REPOSITORY ${BUDO_OPUS_GIT_REPOSITORY}
        GIT_TAG        ${BUDO_OPUS_GIT_TAG}
        GIT_SHALLOW    FALSE
    )
    FetchContent_Declare(
        opusfile
        GIT_REPOSITORY ${BUDO_OPUSFILE_GIT_REPOSITORY}
        GIT_TAG        ${BUDO_OPUSFILE_GIT_TAG}
        GIT_SHALLOW    FALSE
        SOURCE_SUBDIR  budo-sources-only
    )
    cmake_policy(PUSH)
    cmake_policy(SET CMP0077 NEW)
    FetchContent_MakeAvailable(ogg opus opusfile)
    cmake_policy(POP)

    add_library(budo_opusfile STATIC
        "${opusfile_SOURCE_DIR}/src/info.c"
        "${opusfile_SOURCE_DIR}/src/internal.c"
        "${opusfile_SOURCE_DIR}/src/opusfile.c"
        "${opusfile_SOURCE_DIR}/src/stream.c")
    target_include_directories(budo_opusfile
        PUBLIC "${opusfile_SOURCE_DIR}/include"
        PRIVATE "${opusfile_SOURCE_DIR}/src")
    target_compile_definitions(budo_opusfile PRIVATE OP_DISABLE_HTTP)
    target_link_libraries(budo_opusfile PUBLIC Ogg::ogg Opus::opus)
    set_target_properties(budo_opusfile PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set_target_properties(ogg opus PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set_target_properties(opus PROPERTIES C_VISIBILITY_PRESET hidden)

    set(BUILD_SHARED_LIBS "${_budo_build_shared_libs}")
    set(BUILD_TESTING "${_budo_build_testing}")
endfunction()
