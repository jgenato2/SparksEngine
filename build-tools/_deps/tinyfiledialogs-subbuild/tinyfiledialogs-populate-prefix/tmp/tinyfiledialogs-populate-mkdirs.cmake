# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-src")
  file(MAKE_DIRECTORY "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-src")
endif()
file(MAKE_DIRECTORY
  "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-build"
  "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix"
  "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix/tmp"
  "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix/src/tinyfiledialogs-populate-stamp"
  "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix/src"
  "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix/src/tinyfiledialogs-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix/src/tinyfiledialogs-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Mac/SourceCode/SparksEngine/build-tools/_deps/tinyfiledialogs-subbuild/tinyfiledialogs-populate-prefix/src/tinyfiledialogs-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
