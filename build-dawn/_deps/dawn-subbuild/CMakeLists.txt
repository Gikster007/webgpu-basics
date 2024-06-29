# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.28.3-msvc11)

# Reject any attempt to use a toolchain file. We must not use one because
# we could be downloading it here. If the CMAKE_TOOLCHAIN_FILE environment
# variable is set, the cache variable will have been initialized from it.
unset(CMAKE_TOOLCHAIN_FILE CACHE)
unset(ENV{CMAKE_TOOLCHAIN_FILE})

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(dawn-populate NONE)



include(ExternalProject)
ExternalProject_Add(dawn-populate
                     "UPDATE_DISCONNECTED" "False" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "DOWNLOAD_COMMAND" "cd" "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-src" "&&" "git" "init" "&&" "git" "fetch" "--depth=1" "https://dawn.googlesource.com/dawn" "chromium/6536" "&&" "git" "reset" "--hard" "FETCH_HEAD" "PATCH_COMMAND" "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" "-E" "copy" "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/webgpu-backend-dawn-src/cmake/../patch/tools/fetch_dawn_dependencies.py" "tools"
                    SOURCE_DIR          "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-src"
                    BINARY_DIR          "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


