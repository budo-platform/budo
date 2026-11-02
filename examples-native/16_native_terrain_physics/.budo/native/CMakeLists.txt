cmake_minimum_required(VERSION 3.20)
project(budo_generated_native C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUDO_NATIVE_SANITIZERS "" CACHE STRING "Application sanitizers: address, undefined, or address,undefined")
if(BUDO_NATIVE_SANITIZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Native application sanitizers require Clang or GCC; selected compiler is ${CMAKE_C_COMPILER_ID}. Choose a supported compiler or omit --sanitize.")
  endif()
  if(NOT BUDO_NATIVE_SANITIZERS MATCHES "^(address|undefined|address,undefined)$")
    message(FATAL_ERROR "Unsupported BUDO_NATIVE_SANITIZERS value: ${BUDO_NATIVE_SANITIZERS}")
  endif()
endif()

set(BUDO_NATIVE_PROJECT_ROOT [==[/mnt/data/arnaud/repos/perso/hatchway/examples/16_native_terrain_physics]==])
set(BUDO_NATIVE_SDK_INPUT [==[/mnt/data/arnaud/repos/perso/hatchway/build/BudoNativeTargets.cmake]==] CACHE PATH "Budo native SDK package directory or development target manifest")
if(IS_DIRECTORY "${BUDO_NATIVE_SDK_INPUT}")
  find_package(BudoNative CONFIG REQUIRED PATHS "${BUDO_NATIVE_SDK_INPUT}" NO_DEFAULT_PATH)
else()
set(BUDO_NATIVE_TARGETS_FILE "${BUDO_NATIVE_SDK_INPUT}")
if(NOT EXISTS "${BUDO_NATIVE_TARGETS_FILE}")
  message(FATAL_ERROR "BUDO_NATIVE_TARGETS_FILE does not exist: ${BUDO_NATIVE_TARGETS_FILE}")
endif()
find_package(Threads REQUIRED)
find_package(SDL2 REQUIRED)
if(UNIX AND NOT APPLE)
  find_package(OpenGL REQUIRED)
  find_package(Freetype REQUIRED)
  find_package(Fontconfig REQUIRED)
endif()
set(BUDO_NATIVE_SKIA_LIBRARY "" CACHE FILEPATH "Local SDK Skia library (required by checkout exports)")
if(BUDO_NATIVE_SKIA_LIBRARY AND NOT TARGET skia)
  add_library(skia STATIC IMPORTED)
  set_target_properties(skia PROPERTIES IMPORTED_LOCATION "${BUDO_NATIVE_SKIA_LIBRARY}")
endif()
include("${BUDO_NATIVE_TARGETS_FILE}")
endif()

add_executable(budo_native_app)
target_sources(budo_native_app PRIVATE
  "${BUDO_NATIVE_PROJECT_ROOT}/main.c"
  "${BUDO_NATIVE_PROJECT_ROOT}/math3d.c"
  "${BUDO_NATIVE_PROJECT_ROOT}/terrain.c"
  "${BUDO_NATIVE_PROJECT_ROOT}/physics.c"
  "${BUDO_NATIVE_PROJECT_ROOT}/ui_gpu.c"
  "${BUDO_NATIVE_PROJECT_ROOT}/generated/car_models.c"
)
set_property(TARGET budo_native_app PROPERTY C_STANDARD 11)
set_property(TARGET budo_native_app PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET budo_native_app PROPERTY C_EXTENSIONS OFF)
set_target_properties(budo_native_app PROPERTIES
  OUTPUT_NAME [==[Budo_Native_Terrain_Physics]==]
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(BUDO_CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_property(TARGET budo_native_app PROPERTY "RUNTIME_OUTPUT_DIRECTORY_${BUDO_CONFIG}" "${CMAKE_BINARY_DIR}/bin")
endforeach()
target_include_directories(budo_native_app PRIVATE
  "${BUDO_NATIVE_PROJECT_ROOT}/include"
  "${BUDO_NATIVE_PROJECT_ROOT}/generated"
)
target_link_libraries(budo_native_app PRIVATE budo::native_launcher)
if(BUDO_NATIVE_SANITIZERS)
  string(REPLACE "," ";" BUDO_SANITIZER_LIST "${BUDO_NATIVE_SANITIZERS}")
  list(JOIN BUDO_SANITIZER_LIST "," BUDO_SANITIZER_FLAGS)
  target_compile_options(budo_native_app PRIVATE "-fsanitize=${BUDO_SANITIZER_FLAGS}" -fno-omit-frame-pointer)
  target_link_options(budo_native_app PRIVATE "-fsanitize=${BUDO_SANITIZER_FLAGS}")
endif()
if(COMMAND budo_native_configure_application)
  budo_native_configure_application(budo_native_app)
endif()
add_custom_command(TARGET budo_native_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:budo_native_app>/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUDO_NATIVE_PROJECT_ROOT}/terrain.vert" "$<TARGET_FILE_DIR:budo_native_app>/terrain.vert"
)
add_custom_command(TARGET budo_native_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:budo_native_app>/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUDO_NATIVE_PROJECT_ROOT}/terrain.frag" "$<TARGET_FILE_DIR:budo_native_app>/terrain.frag"
)
add_custom_command(TARGET budo_native_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:budo_native_app>/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUDO_NATIVE_PROJECT_ROOT}/car.vert" "$<TARGET_FILE_DIR:budo_native_app>/car.vert"
)
add_custom_command(TARGET budo_native_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:budo_native_app>/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUDO_NATIVE_PROJECT_ROOT}/car.frag" "$<TARGET_FILE_DIR:budo_native_app>/car.frag"
)
add_custom_command(TARGET budo_native_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:budo_native_app>/assets/kenney_car_kit"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BUDO_NATIVE_PROJECT_ROOT}/assets/kenney_car_kit/LICENSE.txt" "$<TARGET_FILE_DIR:budo_native_app>/assets/kenney_car_kit/LICENSE.txt"
)
