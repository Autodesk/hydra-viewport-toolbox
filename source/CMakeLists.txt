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

# Declare the project.
project(hvt
    VERSION     "${_VERSION}"
    DESCRIPTION "Hydra viewport toolbox"
    LANGUAGES   CXX
)

#---------------------------------------------------------------------------------------------------

# This section lists all the third-party dependencies.

# Find the Python3 include directory, which may be required by OpenUSD.
# NOTE: Without this, the OpenUSD headers will be unable to find Python headers like pyconfig.h.
find_package(Python3 REQUIRED COMPONENTS Development)
if (Python3_FOUND)
    message(STATUS "Found Python3 version ${Python3_VERSION}.")
    set(_PYTHON3_INCLUDE_DIR "${Python3_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Python3 not found.")
endif()

# Find OpenGL i.e., mandatory for OpenUSD.
find_package(OpenGL REQUIRED)
if (NOT OpenGL_FOUND)
    message(FATAL_ERROR "OpenGL was not found.")
endif()

# If OpenUSD was built with Vulkan support, the Vulkan package is also required. We detect this by
# checking for a specific directory in the OpenUSD install path.
if (EXISTS "${OPENUSD_INSTALL_PATH}/lib/usd/hgiVulkan")
    find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
    if (NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan was not found.")
    endif()
endif()

# Find OpenUSD, and populate variables needed for building (e.g. PXR_INCLUDE_DIRS) and library names
# (e.g. hdx, usdImaging) that can be used for linking.
# NOTE: The special variable <package>_DIR is used by find_package(), so in this case we must
# specify "pxr_DIR" because OpenUSD uses "pxr" as the package name.
set(pxr_DIR ${OPENUSD_INSTALL_PATH})
find_package(pxr REQUIRED NO_CMAKE_FIND_ROOT_PATH)
if (NOT pxr_FOUND)
    message(FATAL_ERROR "OpenUSD was not found.")
else()
    message(STATUS "Found OpenUSD version ${PXR_VERSION}.")
endif()

#---------------------------------------------------------------------------------------------------

# FIXME: Drop use of adsk/dev

# When using an Autodesk fork of OpenUSD, add a suffix to the namespace to avoid conflict with
# official releases from Pixar.
if (ENABLE_ADSK_OPENUSD OR ENABLE_ADSK_OPENUSD_PENDING)
    set(_NS_ADSK "_adsk")
endif()

# Create the versioned namespace, which includes a prefix, version, and optional suffix, 
# e.g. "hvt_v0_25_03_adsk".
set(_SRCFILE ${CMAKE_CURRENT_SOURCE_DIR}/namespace.h.in)
set(_DSTFILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hvt/namespace.h)
configure_file(${_SRCFILE} ${_DSTFILE})

#---------------------------------------------------------------------------------------------------

# This section lists all the existing projects.

# That's a private library.
add_subdirectory("utils")
LIST(APPEND _PRIVATE_LIBRARIES ${_UTILS_TARGET})

# That's a private library.
add_subdirectory("shadow")
LIST(APPEND _PRIVATE_LIBRARIES ${_SHADOW_TARGET})

add_subdirectory("dataSource")
LIST(APPEND _PUBLIC_LIBRARIES ${_DATA_SOURCE_TARGET})

add_subdirectory("sceneIndex")
LIST(APPEND _PUBLIC_LIBRARIES ${_SCENE_INDEX_TARGET})

add_subdirectory("tasks")
LIST(APPEND _PUBLIC_LIBRARIES ${_TASKS_TARGET})

add_subdirectory("engine")
LIST(APPEND _PUBLIC_LIBRARIES ${_ENGINE_TARGET})

# TODO
# add_subdirectory("event")
# LIST(APPEND _PUBLIC_LIBRARIES ${_EVENT_TARGET})

add_subdirectory("geometry")
LIST(APPEND _PUBLIC_LIBRARIES ${_GEOMETRY_TARGET})

# TODO
# add_subdirectory("material")
# LIST(APPEND _PUBLIC_LIBRARIES ${_MATERIAL_TARGET})

#---------------------------------------------------------------------------------------------------

# Add the main library for the project, which will be built as a static library by default.
set(_TARGET "${PROJECT_NAME}")
set(_MODULE_NAME "hvt")
add_library(${_TARGET} "main.cpp")

set_target_properties(${_TARGET}
    PROPERTIES
        OUTPUT_NAME ${_MODULE_NAME}
)

if (MSVC)
    # Set PDB file name & location.
    set_target_properties(${_TARGET}
        PROPERTIES
            COMPILE_PDB_NAME ${_MODULE_NAME}
            COMPILE_PDB_OUTPUT_DIR "${CMAKE_BINARY_DIR}"
    )
endif()

# Link to the libraries from the sub-projects. Also link to the OpenUSD libraries through its hdx
# and usdImaging libraries, as linking to those is enough to get the rest of the dependencies.
# See the PXR_LIBRARIES variable for the full list of libraries.
target_link_libraries(${_TARGET} 
    PUBLIC 
        ${_PUBLIC_LIBRARIES}
        hdx
        usdImaging
    PRIVATE
        ${_PRIVATE_LIBRARIES}
)

#---------------------------------------------------------------------------------------------------

# Prepare for creating relocatable packages.
include(CMakePackageConfigHelpers)

# Create package configuration file to ensure that the resulting package is relocatable,
# by avoiding hardcoded paths in the installed configuration file.
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${_MODULE_NAME}Config.cmake.in
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

# Install exported targets for relocatable package.
# TODO: This currently gives a "not in any export set" error, so this is temporarily disabled. We
# will need to add appropriate install() commands in the sub-projects.
# if (BUILD_SHARED_LIBS)
#     install(EXPORT ${_MODULE_NAME}Targets
#         DESTINATION lib/cmake/${_MODULE_NAME}
#         FILE ${_MODULE_NAME}Targets.cmake
#         NAMESPACE ${_MODULE_NAME}::
#         EXPORT_LINK_INTERFACE_LIBRARIES
#     )
# endif()
