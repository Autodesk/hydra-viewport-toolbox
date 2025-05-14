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

# Add the path to the CMake helpers and import them.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(helpers)

# Variables that need to be set prior to the project definition.
if (APPLE)
    set_if_not_defined(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" "OSX architecture")
    set_if_not_defined(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" "Minimum OSX deployment version")
    set_if_not_defined(CMAKE_SKIP_BUILD_RPATH TRUE "")
    set_if_not_defined(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE "")
    set_if_not_defined(CMAKE_INSTALL_RPATH "@loader_path/" "")
    set_if_not_defined(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE "")
    set_if_not_defined(CMAKE_MACOSX_RPATH ON "")
endif()

# Features to enable in vcpkg prior to the project definition.
option(ENABLE_TESTS "Enable tests" OFF)
if(ENABLE_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
# Enable vcpkg USD feature only if no local USD path is provided
if(NOT DEFINED OPENUSD_INSTALL_PATH)
  list(APPEND VCPKG_MANIFEST_FEATURES "usd-build")
endif()

# VCPKG Bootstrap prior to the project definition.
if(VCPKG_MANIFEST_FEATURES)
    set(VCPKG_DIR "${CMAKE_CURRENT_LIST_DIR}/vcpkg")    
    if(NOT EXISTS "${VCPKG_DIR}")
        message(STATUS "vcpkg directory not found but vcpkg features were requested. Fetching submodules.")
        execute_process(COMMAND git submodule update --init --recursive
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
    endif()
    # vcpkg executable is present if boostrap was already executed
    if(WIN32)
        if(NOT EXISTS "${VCPKG_DIR}/vcpkg.exe")
            message(STATUS "Bootstrapping vcpkg (Windows)...")
            execute_process(COMMAND cmd /c bootstrap-vcpkg.bat
                WORKING_DIRECTORY "${VCPKG_DIR}")
        endif()
    else()
        if(NOT EXISTS "${VCPKG_DIR}/vcpkg")
            message(STATUS "Bootstrapping vcpkg (Unix)...")
            execute_process(COMMAND ./bootstrap-vcpkg.sh
                WORKING_DIRECTORY "${VCPKG_DIR}")
        endif()
    endif()
    # Define the toolchain if it was not already defined in the command line
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()
    # Overlay ports are there to change the default behavior of vcpkg on standard ports or add new ports.
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/overlay-ports")
else()
    message(STATUS "Not using vcpkg for hydra-viewport-toolbox because no dependencies requested...")
endif()

# Declare the project.
project(HydraViewportToolbox
    VERSION     0.25.03.0
    DESCRIPTION "Utilities to support graphics viewports using OpenUSD Hydra"
    LANGUAGES   CXX
)
set(_VERSION "${HydraViewportToolbox_VERSION}")

# Enable only the typical build types.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;MinSizeRel;RelWithDebInfo")
if (NOT "${CMAKE_BUILD_TYPE}" IN_LIST CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "CONFIG=${CMAKE_BUILD_TYPE} is unsupported. Supported values are: ${CMAKE_CONFIGURATION_TYPES}.")
endif()

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

# By default, generate all XCode schemes.
set(CMAKE_XCODE_GENERATE_SCHEME  ON)

# By default, set the symbol visibility to hidden (to avoid exporting private symbols by mistake).
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# By default, set the output directory for all the sub-projects.
set_if_not_defined(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin" "Default output directory for binaries")
set_if_not_defined(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib" "Default output directory for libraries")

#---------------------------------------------------------------------------------------------------

# This section only adds cmake options.

# An option to enable the use of precompiled headers.
# TODO: Implement precompiled headers.
option(ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers" ON)

# Optional: Path to a local OpenUSD install (CMake config, not source tree).
# NOTE: This refers to an install of OpenUSD, not the source.
set(OPENUSD_INSTALL_PATH CACHE PATH "Path to a local OpenUSD install (optional)")

if (OPENUSD_INSTALL_PATH AND EXISTS "${OPENUSD_INSTALL_PATH}")
    set(pxr_DIR "${OPENUSD_INSTALL_PATH}")
    message(STATUS "Using local OpenUSD install at: ${OPENUSD_INSTALL_PATH}")
    # Local OpenUSD installs typically only build one configuration.
    #set(CMAKE_CONFIGURATION_TYPES "${CMAKE_BUILD_TYPE}")
else()
    # Note that under normal verbosity, cmake will ignore the printing of this message
    message(STATUS "No valid OPENUSD_INSTALL_PATH provided, using OpenUSD from vcpkg.")
endif()

# Enable the use of pending changes to OpenUSD from Autodesk.
# NOTE: These are not currently available publicly, but are expected to be available in a future
# version of OpenUSD.
option(ENABLE_ADSK_OPENUSD_PENDING "Enable the use of pending changes to OpenUSD from Autodesk" OFF)

# Enable/Disable warnings as errors.
option(ENABLE_WARNINGS_AS_ERRORS "Enable all the warnings as errors" ON)

# Use draw order.
option(ENABLE_DRAW_ORDER "Use draw order" ON)

#---------------------------------------------------------------------------------------------------

# This section lists all the third-party dependencies common to all sub-projects.

# Find OpenGL, which is mandatory for OpenUSD on Windows and macOS.
if (WIN32 OR (APPLE AND NOT IOS))
    find_package(OpenGL REQUIRED)
    if (NOT OpenGL_FOUND)
        message(FATAL_ERROR "OpenGL was not found.")
    endif()
endif()

# Find OpenUSD if not defined already, and populate variables needed for building (e.g. 
# PXR_INCLUDE_DIRS) and target names (e.g. hdx, usdImaging) that can be used for assigning
# dependencies. See the PXR_LIBRARIES variable for the full list of libraries.
# NOTE: The special variable <package>_DIR is used by find_package(), so in this case we must
# specify "pxr_DIR" because OpenUSD uses "pxr" as the package name.
if (NOT pxr_FOUND)
    find_package(pxr REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    
    # TEMP: Make sure at this point both variable point to the same location. (ADSK or PIXAR install)
    set(OPENUSD_INSTALL_PATH "${pxr_DIR}")
endif()

#---------------------------------------------------------------------------------------------------

# This section only adds settings applicable to all the projects.

# Set the HVT include (public API) and source (private API) directories, for use by sub-projects.
set(_HVT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(_HVT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(_HVT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# Allowing tbb to work in debug mode in different setups
add_compile_definitions(
	$<$<CONFIG:Debug>:TBB_USE_DEBUG>
)

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

    # The min() and max() macro definitions from windef.h (Windows) can conflict with std::min()
    # and std::max(). For example, this happens when using OpenUSD with TBB instead of OneTBB. To
    # prevent the conflict, we can specify the NOMINMAX symbol since we don't use the macros.
    add_compile_definitions(NOMINMAX)

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

# TODO: Drop ENABLE_ADSK_OPENUSD_PENDING support i.e., only support origin/dev.
if (ENABLE_ADSK_OPENUSD_PENDING)
    add_compile_definitions(ADSK_OPENUSD_PENDING)
endif()

# TODO: Drop DRAW_ORDER support
# Add a DRAW_ORDER definition if the draw order feature is enabled.
if (ENABLE_DRAW_ORDER)
    add_compile_definitions(DRAW_ORDER)
endif()

#---------------------------------------------------------------------------------------------------

add_subdirectory("source")

if (ENABLE_TESTS AND PROJECT_IS_TOP_LEVEL)
    # Initialize CTest before adding test.
    enable_testing()

    add_subdirectory("test")
endif()

#---------------------------------------------------------------------------------------------------

message(STATUS "-------------------------------------------")
message(STATUS "Hydra Viewport Toolbox build configuration:")
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
message(STATUS "  OpenUSD build path       : ${pxr_DIR}")
message(STATUS "  OpenUSD version          : ${pxr_VERSION}")
message(STATUS "  Autodesk OpenUSD pending : ${ENABLE_ADSK_OPENUSD_PENDING}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Draw order               : ${ENABLE_DRAW_ORDER}")
message(STATUS "  Tests                    : ${ENABLE_TESTS}")
message(STATUS "-------------------------------------------")
