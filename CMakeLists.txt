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

add_subdirectory(glfw3webgpu)

if (EMSCRIPTEN)
    # Emscripten has built-in support for GLFW but requires the `-sUSE_GLFW=3` link option:
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

target_link_libraries(webgpu-basics PRIVATE webgpu glfw glfw3webgpu)

# Glob all source files
file(GLOB_RECURSE SOURCES
	"src/*.cpp"
)

# Add source files to the program
target_sources(webgpu-basics PRIVATE
	${SOURCES}
)

# Pre-compiled header
target_precompile_headers(webgpu-basics PRIVATE "src/precomp.h")

# We add an option to enable different settings when developing the app than
# when distributing it.
option(DEV_MODE "Set up development helper settings" ON)

if(DEV_MODE)
    # In dev mode, we load resources from the source tree, so that when we
    # dynamically edit resources (like shaders), these are correctly
    # versionned.
    target_compile_definitions(webgpu-basics PRIVATE
        RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
else()
    # In release mode, we just load resources relatively to wherever the
    # executable is launched from, so that the binary is portable
    target_compile_definitions(webgpu-basics PRIVATE
        RESOURCE_DIR="./resources"
    )
endif()

# Catch more warnings
if (MSVC)
    target_compile_options(webgpu-basics PRIVATE /W4)
else()
    target_compile_options(webgpu-basics PRIVATE -Wall -Wextra -pedantic)
endif()

if (EMSCRIPTEN)
    # Generate a full web page rather than a simple WebAssembly module
    set_target_properties(webgpu-basics PROPERTIES SUFFIX ".html")
    # Enable the use of emscripten_sleep()
    target_link_options(webgpu-basics PRIVATE -sASYNCIFY)
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET webgpu-basics PROPERTY CXX_STANDARD 20)
endif()

target_copy_webgpu_binaries(webgpu-basics)