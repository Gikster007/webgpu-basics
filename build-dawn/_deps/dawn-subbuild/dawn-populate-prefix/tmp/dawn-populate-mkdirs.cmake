# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-src"
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-build"
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix"
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix/tmp"
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix/src/dawn-populate-stamp"
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix/src"
  "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix/src/dawn-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix/src/dawn-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Projects/C++/basic-webgpu-app/build-dawn/_deps/dawn-subbuild/dawn-populate-prefix/src/dawn-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
