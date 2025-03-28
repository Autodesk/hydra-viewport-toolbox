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

# Enable only the typical build types.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;MinSizeRel;RelWithDebInfo")
if(NOT "${CMAKE_BUILD_TYPE}" IN_LIST CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "CONFIG=${CMAKE_BUILD_TYPE} is unsupported. Supported values are: ${CMAKE_CONFIGURATION_TYPES}.")
endif()

# Declare the project.
project(HydraViewportToolbox
    VERSION     0.25.03.0
    DESCRIPTION "Utilities to support graphics viewports using OpenUSD Hydra"
    LANGUAGES   CXX
)

set(_VERSION "${HydraViewportToolbox_VERSION}")

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

# OpenUSD install path global variable, which is required.
# NOTE: This refers to an install of OpenUSD, not the source.
set(OPENUSD_INSTALL_PATH CACHE FILEPATH "Path to OpenUSD install")
if(NOT OPENUSD_INSTALL_PATH OR NOT EXISTS ${OPENUSD_INSTALL_PATH})
    message(FATAL_ERROR "OPENUSD_INSTALL_PATH not specified or does not exist: ${OPENUSD_INSTALL_PATH}.")
endif()

# Enable the use of an Autodesk fork of OpenUSD.
# NOTE: This is necessary when using OpenUSD from https://github.com/autodesk-forks/USD.
option(ENABLE_ADSK_OPENUSD "Enable the use of an Autodesk fork of OpenUSD" OFF)

# Enable/Disable warnings as errors.
option(ENABLE_WARNINGS_AS_ERRORS "Enable all the warnings as errors" ON)

# Use draw order.
option(ENABLE_DRAWORDER "Use DrawOrder" ON)

##### End User Settings #####

# Find the Python3 include directory, which may be required by OpenUSD.
# NOTE: Without this, the OpenUSD headers will be unable to find Python headers like pyconfig.h.
find_package(Python3 REQUIRED COMPONENTS Development)
if (Python3_FOUND)
    message(STATUS "Found Python3 version ${Python3_VERSION}.")
    set(_PYTHON3_INCLUDE_DIR "${Python3_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Python3 not found.")
endif()

#---------------------------------------------------------------------------------------------------

# This section lists all the third-party dependencies.

# Find OpenGL i.e., mandatory for OpenUSD.
find_package(OpenGL REQUIRED)
if (NOT OpenGL_FOUND)
    message(FATAL_ERROR "OpenGL was not found.")
endif()

# Find OpenUSD, and populate variables needed for building, e.g. PXR_INCLUDE_DIRS.
set(pxr_DIR ${OPENUSD_INSTALL_PATH})
find_package(pxr REQUIRED NO_CMAKE_FIND_ROOT_PATH)
if (NOT pxr_FOUND)
    message(FATAL_ERROR "OpenUSD was not found.")
else()
    message(STATUS "Found OpenUSD version ${PXR_VERSION}.")
endif()

#---------------------------------------------------------------------------------------------------

# This section only adds settings applicable to all the projects.

# Set the HVT include directory, for use by sub-projects.
set(_HVT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

# Enable warnings and warnings as errors for all sub-projects.
if (MSVC)

    # TODO: Set this to /W4 or /Wall when warnings have been resolved, i.e. fixed or suppressed.
    add_compile_options(/W1)

    if (ENABLE_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()

    # Tell compiler to store the debugging information in a PDB for the OBJ file.
    add_compile_options(/Zi)

    # Tell linker to include symbol data.
    if (BUILD_SHARED_LIBS)
        add_link_options(/DEBUG:FULL /OPT:REF /OPT:ICF)
    endif()

elseif(CMAKE_CXX_COMPILER_ID MATCHES "[C|c]lang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")

    # By default, enables all warnings.
    add_compile_options(-Wall -Wextra -Wpedantic)

    if (ENABLE_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()

else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}.")
endif()

# Add an HVT_BUILD definition to indicate that the project is being built rather than consumed. Also
# add HVT_SHARED if the project is being built (or was built) with shared libraries.
add_compile_definitions(
    HVT_BUILD
    $<$<BOOL:${BUILD_SHARED_LIBS}>:HVT_SHARED>
)

if (ENABLE_ADSK_OPENUSD)
    add_compile_definitions(ADSK_OPENUSD)
endif()

if (ENABLE_DRAWORDER)
    add_compile_definitions(DRAW_ORDER)
endif()

#---------------------------------------------------------------------------------------------------

# When using adsk/dev change the namespace to avoid conflict with official releases
# e.g., release built from origin/dev for example.
if (ENABLE_ADSK_OPENUSD)
    set(_NS_ADSK "_adsk")
endif()

# Create the versioned namespace, which includes a prefix, version, and optional suffix, 
# e.g. "hvt_v0_25_03_adsk".
set(_SRCFILE ${CMAKE_CURRENT_SOURCE_DIR}/source/namespace.h.in)
set(_DSTFILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hvt/namespace.h)
configure_file(${_SRCFILE} ${_DSTFILE})

#---------------------------------------------------------------------------------------------------

# Add the main library for the project, which will be built as a static library by default.
# TODO: This currently can't be built as a shared library because it will require linking to TBB,
# so we need to find TBB first when building as a shared library.
set(_TARGET "${PROJECT_NAME}")
set(_MODULE_NAME "hvt")
add_library(${_TARGET} "source/main.cpp")

set_target_properties(${_TARGET}
    PROPERTIES
        OUTPUT_NAME ${_MODULE_NAME}
)

if(MSVC)
    # Set PDB file name & location.
    set_target_properties(${_TARGET}
        PROPERTIES
            COMPILE_PDB_NAME ${_MODULE_NAME}
            COMPILE_PDB_OUTPUT_DIR "${CMAKE_BINARY_DIR}"
    )
endif()

#---------------------------------------------------------------------------------------------------

# This section lists all the existing projects.

# That's a private library.
add_subdirectory("source/utils")
LIST(APPEND _PRIVATE_LIBRARIES ${_UTILS_TARGET})

# That's a private library.
add_subdirectory("source/shadow")
LIST(APPEND _PRIVATE_LIBRARIES ${_SHADOW_TARGET})

add_subdirectory("source/dataSource")
LIST(APPEND _PUBLIC_LIBRARIES ${_DATA_SOURCE_TARGET})

add_subdirectory("source/sceneIndex")
LIST(APPEND _PUBLIC_LIBRARIES ${_SCENE_INDEX_TARGET})

add_subdirectory("source/tasks")
LIST(APPEND _PUBLIC_LIBRARIES ${_TASKS_TARGET})

add_subdirectory("source/engine")
LIST(APPEND _PUBLIC_LIBRARIES ${_ENGINE_TARGET})

add_subdirectory("source/event")
LIST(APPEND _PUBLIC_LIBRARIES ${_EVENT_TARGET})

add_subdirectory("source/geometry")
LIST(APPEND _PUBLIC_LIBRARIES ${_GEOMETRY_TARGET})

add_subdirectory("source/material")
LIST(APPEND _PUBLIC_LIBRARIES ${_MATERIAL_TARGET})

# Link to the libraries from the sub-projects.
target_link_libraries(${_TARGET} 
    PUBLIC 
        ${_PUBLIC_LIBRARIES}
        ${PXR_LIBRARIES}
    PRIVATE
        ${_PRIVATE_LIBRARIES}
)

if (ENABLE_TESTS)
    add_subdirectory("test")
endif()

#---------------------------------------------------------------------------------------------------

# Prepare for creating relocatable packages.
include(CMakePackageConfigHelpers)

# Create package configuration file to ensure that the resulting package is relocatable,
# by avoiding hardcoded paths in the installed configuration file.
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/source/${_MODULE_NAME}Config.cmake.in
    ${PROJECT_BINARY_DIR}/${_MODULE_NAME}Config.cmake
    INSTALL_DESTINATION lib/cmake/${_MODULE_NAME}
    NO_SET_AND_CHECK_MACRO
)

# Create a package version file to provide better compatibility support.
write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/${_MODULE_NAME}ConfigVersion.cmake
    VERSION ${${PROJECT_NAME}_VERSION}
    COMPATIBILITY SameMajorVersion
)

#---------------------------------------------------------------------------------------------------

# Install the binaries.
install(TARGETS ${_TARGET}
    EXPORT ${_MODULE_NAME}Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    )

# Install the public header files.
install(DIRECTORY ${_HVT_INCLUDE_DIR}/hvt DESTINATION include)

# Install the PDB file.
if (MSVC AND BUILD_SHARED_LIBS)
    install(FILES $<TARGET_PDB_FILE:${_TARGET}>
        DESTINATION bin
        CONFIGURATIONS RelWithDebInfo Debug
    )
endif()

# Install configuration and version files for relocatable package.
install(FILES
    ${PROJECT_BINARY_DIR}/${_MODULE_NAME}Config.cmake
    ${PROJECT_BINARY_DIR}/${_MODULE_NAME}ConfigVersion.cmake
    DESTINATION lib/cmake/${${_MODULE_NAME}}
)

if (BUILD_SHARED_LIBS)
    # Install exported targets for relocatable package.
    install(EXPORT ${_MODULE_NAME}Targets
        DESTINATION lib/cmake/${_MODULE_NAME}
        FILE ${_MODULE_NAME}Targets.cmake
        NAMESPACE ${_MODULE_NAME}::
        EXPORT_LINK_INTERFACE_LIBRARIES
    )
endif()

#---------------------------------------------------------------------------------------------------

message(STATUS "--------------------")
message(STATUS "Build configuration:")
message(STATUS "  Platform                 : ${CMAKE_SYSTEM_NAME} [${CMAKE_SYSTEM}]")
if (APPLE)
    EXECUTE_PROCESS( COMMAND sysctl -n machdep.cpu.brand_string OUTPUT_VARIABLE COMPUTED_ARCHITECTURE )
    message(STATUS "  Machine architecture     : ${COMPUTED_ARCHITECTURE}")
endif()
message(STATUS "  CMake                    : ${CMAKE_VERSION}")
message(STATUS "  CMake generator          : ${CMAKE_GENERATOR}")
message(STATUS "  Compiler                 : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Compiler version         : ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
if (APPLE)
    message(STATUS "  Build architecture       : ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "  Build deployment target  : ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
message(STATUS "  Build type               : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build types              : ${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "  Warnings as errors       : ${ENABLE_WARNINGS_AS_ERRORS}")
message(STATUS "")
message(STATUS "  OpenUSD build path       : ${OPENUSD_INSTALL_PATH}")
message(STATUS "  OpenUSD version          : ${PXR_VERSION}")
message(STATUS "  Autodesk OpenUSD enabled : ${ENABLE_ADSK_OPENUSD}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Draw order               : ${ENABLE_DRAWORDER}")
message(STATUS "")
