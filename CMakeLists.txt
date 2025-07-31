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

# 3.26 to match OpenUSD 25.05.
# 3.31 latest tested version.
cmake_minimum_required(VERSION 3.26...3.31)

# An option to enable tests.
option(ENABLE_TESTS "Enable tests" ON)

# An option to tell vcpkg to only fetch release and not debug dependencies.
option(HVT_RELEASE_ONLY_VCPKG_DEPS "Use a release-optimized vcpkg base triplet" OFF)

# Add the path to the CMake helpers and import them.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Try to import from the environment variable only if OPENUSD_INSTALL_PATH isn’t already set
if(NOT DEFINED OPENUSD_INSTALL_PATH AND DEFINED ENV{OPENUSD_INSTALL_PATH})
    message(STATUS "Using OpenUSD install path from environment variable: $ENV{OPENUSD_INSTALL_PATH}")
    set(OPENUSD_INSTALL_PATH "$ENV{OPENUSD_INSTALL_PATH}" CACHE PATH "Path to local USD install")
endif()

# Includes various helpers.
include(Helpers)

# Setups and runs vcpkg.
include(VcpkgSetup)

# Detect whether this (HVT) is the top-level project.
# NOTE: We can't use PROJECT_IS_TOP_LEVEL here because the project is not yet defined.
string(COMPARE EQUAL "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}" _HVT_PROJECT_IS_TOP_LEVEL)

# Variables that need to be set prior to the project definition.
if (_HVT_PROJECT_IS_TOP_LEVEL AND UNIX)
    set_if_not_defined(CMAKE_SKIP_BUILD_RPATH TRUE "")
    set_if_not_defined(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE "")
    if(APPLE)
        set_if_not_defined(CMAKE_INSTALL_RPATH "@loader_path/" "")
    else()
        # Linux and other Unix systems use $ORIGIN instead of @loader_path
        set_if_not_defined(CMAKE_INSTALL_RPATH "$ORIGIN/" "")
        # Force RPATH instead of RUNPATH on Linux for better library discovery
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-new-dtags")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--disable-new-dtags")
    endif()
    set_if_not_defined(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE "")

    # macOS specific (ignored on other platforms)
    list(LENGTH CMAKE_OSX_ARCHITECTURES osx_arch_count)
    if(osx_arch_count GREATER 1)
        message(FATAL_ERROR "Universal builds are not supported because of libjpeg-turbo")
    endif()
    set_if_not_defined(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" "Minimum OSX deployment version")
    set_if_not_defined(CMAKE_MACOSX_RPATH ON "")
endif()

# Enable only the typical build types.
set_if_not_defined(CMAKE_CONFIGURATION_TYPES "Debug;Release;MinSizeRel;RelWithDebInfo" "List of build types")
if (NOT "${CMAKE_BUILD_TYPE}" IN_LIST CMAKE_CONFIGURATION_TYPES)
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
if (PROJECT_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# By default, generate all XCode schemes.
set(CMAKE_XCODE_GENERATE_SCHEME  ON)

# By default, set the output directory for all the sub-projects.
set_if_not_defined(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin" "Default output directory for binaries")
set_if_not_defined(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib" "Default output directory for libraries")

#---------------------------------------------------------------------------------------------------

# This section only adds cmake options.

include(CMakeDependentOption)

# An option to enable the use of precompiled headers.
# TODO: Implement precompiled headers.
option(ENABLE_PRECOMPILED_HEADERS "Enable precompiled headers" ON)

# Optional: Path to a local OpenUSD install (CMake config, not source tree).
# NOTE: This refers to an install of OpenUSD, not the source.
set(OPENUSD_INSTALL_PATH CACHE PATH "Path to a local OpenUSD install (optional)")

# Optional: Path to a local OpenUSD install for debug (CMake config, not source tree).
# NOTE: This refers to an install of OpenUSD, not the source.
set(OPENUSD_INSTALL_PATH_DEBUG CACHE PATH "Path to a local debug OpenUSD install (optional)")

# Enable the use of pending changes to OpenUSD from Autodesk.
# NOTE: These are not currently available publicly, but are expected to be available in a future
# version of OpenUSD.
option(ENABLE_ADSK_OPENUSD_PENDING "Enable the use of pending changes to OpenUSD from Autodesk" OFF)

# Enable/Disable warnings as errors.
option(ENABLE_WARNINGS_AS_ERRORS "Enable all the warnings as errors" ON)

# Use draw order.
cmake_dependent_option(ENABLE_DRAW_ORDER "Use draw order" ON ENABLE_ADSK_OPENUSD_PENDING OFF)

# By default, build as a dynamic library.
option(BUILD_SHARED_LIBS "Build dynamic libraries" ON)

#---------------------------------------------------------------------------------------------------

# This section lists all the third-party dependencies common to all sub-projects.

# Find OpenGL, which is mandatory for OpenUSD on Windows and macOS.
if (WIN32 OR (APPLE AND NOT IOS))
    find_package(OpenGL REQUIRED)
    if (NOT OpenGL_FOUND)
        message(FATAL_ERROR "OpenGL was not found.")
    endif()
endif()

get_cmake_property(_MULTI_CONFIG GENERATOR_IS_MULTI_CONFIG)

# True if the build is done from a locally compiled OpenUSD
set(USE_LOCAL_USD OFF)

if(OPENUSD_INSTALL_PATH)
    set(USE_LOCAL_USD ON)
endif()

if (OPENUSD_INSTALL_PATH AND EXISTS "${OPENUSD_INSTALL_PATH}")
    set(pxr_DIR "${OPENUSD_INSTALL_PATH}")
    if (_MULTI_CONFIG AND "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES)
        if (NOT OPENUSD_INSTALL_PATH_DEBUG OR NOT EXISTS "${OPENUSD_INSTALL_PATH_DEBUG}")
            if(WIN32)
                message(WARNING "The local OpenUSD debug install path is missing: on Windows, mixing debug and release will most likely result in a broken build! Defaulting to the release build.")
            else()
                message(STATUS "The local OpenUSD debug install path is missing: defaulting to the release one")
            endif()
            set(OPENUSD_INSTALL_PATH_DEBUG "${OPENUSD_INSTALL_PATH}")
        endif()
    endif()
else()
    # Set the path to the vcpkg installed USD if not user provided USD.
    if(_MULTI_CONFIG)
        set(OPENUSD_INSTALL_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        set(OPENUSD_INSTALL_PATH_DEBUG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug")
    else()
        if(CMAKE_BUILD_TYPE STREQUAL Debug)
            set(OPENUSD_INSTALL_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug")
        else()
            set(OPENUSD_INSTALL_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        endif()
    endif()
endif()

# Find OpenUSD if not already found.
if (NOT pxr_FOUND)
    find_package(pxr CONFIG REQUIRED NO_CMAKE_FIND_ROOT_PATH)
endif()

#---------------------------------------------------------------------------------------------------

# This section only adds settings applicable to all the projects.

# Set the HVT include (public API) and source (private API) directories, for use by sub-projects.
set(_HVT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(_HVT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source")
set(_HVT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

if (ENABLE_ADSK_OPENUSD_PENDING)
    # By default, set the symbol visibility to hidden (to avoid exporting private symbols by mistake).
    # Note: Only supported by OpenUSD adsk/dev.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# Allowing tbb to work in debug mode in different setups
add_compile_definitions(
	$<$<CONFIG:Debug>:TBB_USE_DEBUG>
)

# Enable warnings and warnings as errors for all sub-projects.
if (MSVC)

    # Displays level 1, level 2, and level 3 warnings, and all level 4 (informational) warnings.
    add_compile_options(/W4)

    # Disable specific warnings.
    add_compile_options(/wd4251)  # DLL interface warnings.
    add_compile_options(/wd4275)  # DLL interface warnings.

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
if(_MULTI_CONFIG)
    message(STATUS "  Build types              : ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "  Build type               : ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "  Warnings as errors       : ${ENABLE_WARNINGS_AS_ERRORS}")
message(STATUS "")
message(STATUS "  OpenUSD build path       : ${OPENUSD_INSTALL_PATH}")
if (_MULTI_CONFIG)
    message(STATUS "  OpenUSD build path (dbg) : ${OPENUSD_INSTALL_PATH_DEBUG}")
endif()
message(STATUS "  OpenUSD version          : ${pxr_VERSION}")
message(STATUS "  Autodesk OpenUSD pending : ${ENABLE_ADSK_OPENUSD_PENDING}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Draw order               : ${ENABLE_DRAW_ORDER}")
message(STATUS "-------------------------------------------")
