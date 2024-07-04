cmake_minimum_required(VERSION 3.8)

project("webgpu-basics")

include(FetchContent)

FetchContent_Declare(
  webgpu
  GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
  GIT_TAG        main
)
FetchContent_MakeAvailable(webgpu)

# Add source to this project's executable.
add_executable(webgpu-basics "src/main.cpp")

if (NOT EMSCRIPTEN)
    add_subdirectory(glfw)
else()
    # Emscripten has built-in support for GLFW but requires the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

target_link_libraries(webgpu-basics PRIVATE webgpu glfw)

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET webgpu-basics PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
target_copy_webgpu_binaries(webgpu-basics)