# CMakeList.txt : CMake project for basic-webgpu-app, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Include FetchContent
include(FetchContent)

# Set the WEBGPU_BACKEND CMake cache variable
set(WEBGPU_BACKEND "dawn" CACHE STRING "Choose the backend for WebGPU-distribution")

# Fetch the WebGPU-distribution library
FetchContent_Declare(
  webgpu
  GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
  GIT_TAG        main
)
FetchContent_MakeAvailable(webgpu)

project ("basic-webgpu-app")

# Add source to this project's executable.
add_executable (basic-webgpu-app "src/main.cpp")

# Add target properties
set_target_properties(basic-webgpu-app PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

# Catch more warnings
if (MSVC)
    target_compile_options(basic-webgpu-app PRIVATE /W4)
else()
    target_compile_options(basic-webgpu-app PRIVATE -Wall -Wextra -pedantic)
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET basic-webgpu-app PROPERTY CXX_STANDARD 20)
endif()

# Dynamically link backend
target_copy_webgpu_binaries(basic-webgpu-app)