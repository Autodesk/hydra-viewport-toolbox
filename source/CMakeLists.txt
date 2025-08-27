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

#---------------------------------------------------------------------------------------------------

# TODO: Drop use of adsk/dev

# When using an Autodesk fork of OpenUSD, add a suffix to the namespace to avoid conflict with
# official releases from Pixar.
if (ENABLE_ADSK_OPENUSD_PENDING)
    set(_NS_ADSK "_adsk")
endif()

# Create the versioned namespace, which includes a prefix, version, and optional suffix, 
# e.g. "hvt_v0_25_03_adsk".
set(_SRCFILE "${CMAKE_CURRENT_SOURCE_DIR}/namespace.h.in")
set(_DSTFILE "${_HVT_INCLUDE_DIR}/hvt/namespace.h")
configure_file(${_SRCFILE} ${_DSTFILE})

#---------------------------------------------------------------------------------------------------

# Add the sub-libraries, which are contained in subdirectories.

# Specify a name (prefix) used for exports, e.g. calls to install().
set(_EXPORT_NAME "hvt")

# Specify the private libraries, i.e. those that do _not_ expose symbols to HVT clients.
add_subdirectory("shadow")
LIST(APPEND _PRIVATE_LIBRARIES ${_SHADOW_TARGET})
add_subdirectory("utils")
LIST(APPEND _PRIVATE_LIBRARIES ${_UTILS_TARGET})

#Specify the public libraries, i.e. those that expose symbols to HVT clients.
add_subdirectory("dataSource")
LIST(APPEND _PUBLIC_LIBRARIES ${_DATA_SOURCE_TARGET})
add_subdirectory("engine")
LIST(APPEND _PUBLIC_LIBRARIES ${_ENGINE_TARGET})
add_subdirectory("geometry")
LIST(APPEND _PUBLIC_LIBRARIES ${_GEOMETRY_TARGET})
add_subdirectory("sceneIndex")
LIST(APPEND _PUBLIC_LIBRARIES ${_SCENE_INDEX_TARGET})
add_subdirectory("tasks")
LIST(APPEND _PUBLIC_LIBRARIES ${_TASKS_TARGET})
add_subdirectory("pageableBuffer")  
LIST(APPEND _PUBLIC_LIBRARIES ${_PAGEABLE_BUFFER_TARGET})

#---------------------------------------------------------------------------------------------------

# This section generates the Windows resource file.

if (WIN32 AND BUILD_SHARED_LIBS)
    # Get the current year for the copyright notice in the dll resource.
    string(TIMESTAMP HVT_BUILD_YEAR "%Y")

    set(DEBUG_SUFFIX)

    # Generate the DLL rc file.
    configure_file(hvt.rc.in hvt.rc)

    # Add DLL version information.
    list(APPEND _PRIVATE_FILES ${CMAKE_CURRENT_BINARY_DIR}/hvt.rc)
endif()

#---------------------------------------------------------------------------------------------------

# Add the main library for the project, which will be built as a static library by default.
set(_TARGET "hvt")
add_library(${_TARGET} "main.cpp" ${_PRIVATE_FILES})

if (MSVC)
    # Set PDB file name & location.
    set_target_properties(${_TARGET}
        PROPERTIES
            COMPILE_PDB_NAME ${_TARGET}
            COMPILE_PDB_OUTPUT_DIR "${CMAKE_BINARY_DIR}"
    )
endif()

# Link to the targets from the sub-libraries.
target_link_libraries(${_TARGET}
    PUBLIC ${_PUBLIC_LIBRARIES}
    PRIVATE ${_PRIVATE_LIBRARIES}
)

if (APPLE OR LINUX)
    # Add version to binary name to let consumers define their binary compatibility expectations
    # e.g. take libhvt.so.1 means that any minor or patch changes are fine.
    set_target_properties(${_TARGET} 
        PROPERTIES
            VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
    )
endif()

#---------------------------------------------------------------------------------------------------

# Prepare for creating relocatable packages.
include(CMakePackageConfigHelpers)

# Create package configuration file to ensure that the resulting package is relocatable,
# by avoiding hardcoded paths in the installed configuration file.
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/${_EXPORT_NAME}Config.cmake.in"
    "${PROJECT_BINARY_DIR}/${_EXPORT_NAME}Config.cmake"
    INSTALL_DESTINATION "lib/cmake/${_EXPORT_NAME}"
    NO_SET_AND_CHECK_MACRO
)

# Create a package version file to provide better compatibility support.
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/${_EXPORT_NAME}ConfigVersion.cmake"
    VERSION "${${PROJECT_NAME}_VERSION}"
    COMPATIBILITY SameMajorVersion
)

#---------------------------------------------------------------------------------------------------

# Install the binaries.
install(TARGETS ${_TARGET}
    EXPORT ${_EXPORT_NAME}Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install the public header files.
install(DIRECTORY ${_HVT_INCLUDE_DIR}/hvt DESTINATION include)

# Install the PDB file.
if (MSVC AND BUILD_SHARED_LIBS)
    install(FILES "$<TARGET_PDB_FILE:${_TARGET}>"
        DESTINATION bin
        CONFIGURATIONS RelWithDebInfo Debug
    )
endif()

# Install configuration and version files for relocatable package.
install(FILES
    "${PROJECT_BINARY_DIR}/${_EXPORT_NAME}Config.cmake"
    "${PROJECT_BINARY_DIR}/${_EXPORT_NAME}ConfigVersion.cmake"
    DESTINATION "lib/cmake/${_EXPORT_NAME}"
)

# Install exported targets for relocatable package.
if (BUILD_SHARED_LIBS)
    install(EXPORT ${_EXPORT_NAME}Targets
        DESTINATION "lib/cmake/${_EXPORT_NAME}"
        FILE "${_EXPORT_NAME}Targets.cmake"
        NAMESPACE ${_EXPORT_NAME}::
        EXPORT_LINK_INTERFACE_LIBRARIES
    )
endif()
