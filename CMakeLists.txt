# Copyright 2025 Autodesk, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# 3.16 required for precompiled headers.
# 3.31 latest tested version.
cmake_minimum_required(VERSION 3.16...3.31)

# Find all the cmake helpers.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Import the cmake utility functions
include(helpers)

# Variables to set prior the projet definition.
if(APPLE)
    set_if_not_defined(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" "OSX architecture")
    set_if_not_defined(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" "Minimum OSX deployment version")
    set_if_not_defined(CMAKE_SKIP_BUILD_RPATH TRUE "")
    set_if_not_defined(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE "")
    set_if_not_defined(CMAKE_INSTALL_RPATH "@loader_path/" "")
    set_if_not_defined(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE "")
    set_if_not_defined(CMAKE_MACOSX_RPATH ON "")
endif()

# Declare the project.
SET(_VERSION "25.03")
project("HydraViewportToolbox"
    VERSION "${_VERSION}"
    DESCRIPTION "Utilities to support graphics viewports using OpenUSD Hydra"
    LANGUAGES CXX
)

# Declare C++17 support.
set(CMAKE_CXX_STANDARD 17)
# Disable fallback to other C++ version if standard is not supported.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Disable any compiler specific C++ extensions.
set(CMAKE_CXX_EXTENSIONS OFF)

# Required by Linux build.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

##### IDE Settings #####

# Enable code IDE to use folders, this will enable source_group(TREE,...)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# By default, generate all schemes.
set(CMAKE_XCODE_GENERATE_SCHEME  ON)

##### Begin User Settings #####

# An option to enable the use of precompiled headers.
# TODO: Implement precompiled headers.
option(ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers" ON)

# An option to enable tests.
option(ENABLE_TESTS "Enable tests" ON)

# OpenUSD build path global variable, which is required.
# NOTE: This refers to a build of OpenUSD, not the source.
set(OPENUSD_BUILD_PATH CACHE FILEPATH "Path to OpenUSD build")
if(NOT OPENUSD_BUILD_PATH)
    message(FATAL_ERROR "OPENUSD_BUILD_PATH not specified.")
endif()
set(_OPENUSD_INCLUDE_DIR "${OPENUSD_BUILD_PATH}/include")

# Enable the use of an Autodesk fork of OpenUSD.
# NOTE: This is necessary when using OpenUSD from https://github.com/autodesk-forks/USD.
option(ENABLE_ADSK_OPENUSD "Enable the use of an Autodesk fork of OpenUSD" OFF)

##### End User Settings #####

# Find the Python3 include directory, which may be required by OpenUSD.
# NOTE: Without this, the OpenUSD headers will be unable to find Python headers like pyconfig.h.
find_package(Python3 REQUIRED COMPONENTS Development)
if (Python3_FOUND)
    message(STATUS "Found Python3 version ${Python3_VERSION}")
    set(_PYTHON3_INCLUDE_DIR "${Python3_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Python3 not found.")
endif()

# Set the HVT include directory, for use by sub-projects.
set(_HVT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

# Enable warnings and warnings as errors for all sub-projects.
if (MSVC)
    # TODO: Set this to /W4 or /Wall when warnings have been resolved, i.e. fixed or suppressed.
    add_compile_options(/W1 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "[C|c]lang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Add an HVT_BUILD definition to indicate that the project is being built rather than consumed. Also
# add HVT_SHARED if the project is being built (or was built) with shared libraries.
add_compile_definitions(
    HVT_BUILD
    $<$<BOOL:${BUILD_SHARED_LIBS}>:HVT_SHARED>
)

# Add the main library for the project, which will be built as a static library by default.
# TODO: This currently can't be built as a shared library because it will require linking to TBB,
# so we need to find TBB first when building as a shared library.
set(_TARGET "${PROJECT_NAME}")
add_library(${_TARGET} "source/main.cpp")
set_target_properties(${_TARGET} PROPERTIES
    OUTPUT_NAME "hvt"
)

#---------------------------------------------------------------------------------------------------

# Add sub-projects and build a list of libraries.
add_subdirectory("source/engine")
LIST(APPEND _LIBRARIES ${_ENGINE_TARGET})
add_subdirectory("source/event")
LIST(APPEND _LIBRARIES ${_EVENT_TARGET})
add_subdirectory("source/geometry")
LIST(APPEND _LIBRARIES ${_GEOMETRY_TARGET})
add_subdirectory("source/material")
LIST(APPEND _LIBRARIES ${_MATERIAL_TARGET})
add_subdirectory("source/sceneIndex")
LIST(APPEND _LIBRARIES ${_SCENE_INDEX_TARGET})
add_subdirectory("source/tasks")
LIST(APPEND _LIBRARIES ${_TASKS_TARGET})
if (ENABLE_TESTS)
    add_subdirectory("test")
    LIST(APPEND _LIBRARIES ${_TEST_TARGET})
endif()

# Link to the libraries from the sub-projects.
target_link_libraries(${_TARGET} PUBLIC ${_LIBRARIES})

#---------------------------------------------------------------------------------------------------

# Prepare for creating relocatable packages.
include(CMakePackageConfigHelpers)

# Create package configuration file to ensure that the resulting package is relocatable,
# by avoiding hardcoded paths in the installed configuration file.
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}Config.cmake.in
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
    NO_SET_AND_CHECK_MACRO
)

# Create a package version file to provide better compatibility support.
write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_NAME}_VERSION}
    COMPATIBILITY SameMajorVersion
)

#---------------------------------------------------------------------------------------------------

# Install the binaries.
install(TARGETS ${_TARGET}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    )

# Install the public header files.
install(DIRECTORY ${_HVT_INCLUDE_DIR}/hvt DESTINATION include)

# Install configuration and version files for relocatable package.
install(FILES
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION lib/cmake/${${PROJECT_NAME}}
)

