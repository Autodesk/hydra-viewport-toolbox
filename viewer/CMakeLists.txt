# Copyright 2026 Autodesk, Inc.
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

set(_TARGET "hvt_viewer")

#---------------------------------------------------------------------------------------------------

# This section finds the third-party dependencies only needed by the viewer.
#
# NOTE: The dependencies are already available when the unit tests are enabled, and vcpkg provides
# them when HVT is the top-level project. But when HVT is built as part of another project, they may
# be missing; the viewer is then skipped instead of breaking that build.

macro(hvt_viewer_find_target OUTPUT_VARIABLE)
    set(${OUTPUT_VARIABLE} "")
    foreach(_CANDIDATE IN ITEMS ${ARGN})
        if (TARGET ${_CANDIDATE})
            set(${OUTPUT_VARIABLE} ${_CANDIDATE})
            break()
        endif()
    endforeach()
endmacro()

hvt_viewer_find_target(_SDL2_TARGET SDL2::SDL2 SDL2::SDL2-static)
if (NOT _SDL2_TARGET)
    find_package(SDL2 CONFIG QUIET)
    hvt_viewer_find_target(_SDL2_TARGET SDL2::SDL2 SDL2::SDL2-static)
endif()

# NOTE: The vcpkg builds define the GLEW:: targets, where the ones from the Autodesk dependency
# tool define libglew_shared.
set(_GLEW_CANDIDATES GLEW::glew GLEW::glew_s GLEW::GLEW libglew_shared libglew_static)

hvt_viewer_find_target(_GLEW_TARGET ${_GLEW_CANDIDATES})
if (NOT _GLEW_TARGET)
    find_package(glew CONFIG QUIET)
    hvt_viewer_find_target(_GLEW_TARGET ${_GLEW_CANDIDATES})
endif()

if (NOT TARGET OpenGL::GL)
    find_package(OpenGL QUIET)
endif()

set(_MISSING_DEPENDENCIES "")
if (NOT _SDL2_TARGET)
    list(APPEND _MISSING_DEPENDENCIES "SDL2")
endif()
if (NOT _GLEW_TARGET)
    list(APPEND _MISSING_DEPENDENCIES "GLEW")
endif()
if (NOT TARGET OpenGL::GL)
    list(APPEND _MISSING_DEPENDENCIES "OpenGL")
endif()

if (_MISSING_DEPENDENCIES)
    list(JOIN _MISSING_DEPENDENCIES ", " _MISSING_DEPENDENCIES)
    message(STATUS "Skipping the ${_TARGET} target: ${_MISSING_DEPENDENCIES} not found.")
    return()
endif()

#---------------------------------------------------------------------------------------------------

# This section defines the viewer executable.

add_executable(${_TARGET}
    glWindow.cpp
    glWindow.h
    main.cpp
    orbitCamera.cpp
    orbitCamera.h
)

target_link_libraries(${_TARGET}
    PRIVATE
        ${_SDL2_TARGET}
        ${_GLEW_TARGET}
        OpenGL::GL
        hdx
        hvt
)

# Where to find all the resource files, and the model displayed when none is provided.
# NOTE: PROJECT_SOURCE_DIR is the HVT source directory, whether HVT is the top-level project or not.
cmake_path(SET _RESOURCE_PATH NORMALIZE "${PROJECT_SOURCE_DIR}/include/hvt/resources")
cmake_path(SET _DEFAULT_SCENE NORMALIZE "${PROJECT_SOURCE_DIR}/test/data/assets/usd/test_fixed.usda")

target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_RESOURCE_PATH=${_RESOURCE_PATH}
        HVT_VIEWER_DEFAULT_SCENE=${_DEFAULT_SCENE}

        # Keep the standard main() entry point, i.e., do not let SDL redirect it to its own entry
        # point, which would require linking with the SDL2main library.
        SDL_MAIN_HANDLED
)

#---------------------------------------------------------------------------------------------------

# On Windows, copy the USD DLLs and plugins next to the viewer executable, so that it can run from
# the build directory.
# NOTE: When HVT is built as part of another project, that project is responsible for the runtime
# dependencies of its executables.
if (WIN32 AND PROJECT_IS_TOP_LEVEL)
    # Note: The multi-configuration generators always append the build type to the output
    # directories, where the single-configuration generators do not.
    get_cmake_property(_MULTI_CONFIG GENERATOR_IS_MULTI_CONFIG)

    if (_MULTI_CONFIG)
        set(_CONFIGS ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(_CONFIGS ${CMAKE_BUILD_TYPE})
    endif()

    foreach(_CONFIG IN LISTS _CONFIGS)
        if (_MULTI_CONFIG)
            if (_CONFIG STREQUAL Debug)
                set(_SRC "${OPENUSD_INSTALL_PATH_DEBUG}")
            else()
                set(_SRC "${OPENUSD_INSTALL_PATH}")
            endif()
            set(_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_CONFIG}")
        else()
            set(_SRC "${OPENUSD_INSTALL_PATH}")
            set(_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        endif()

        # The 'bin' directory holds the DLLs of a vcpkg build, and 'lib' the ones of a local build.
        file(GLOB _DLL_FILES "${_SRC}/bin/*.dll" "${_SRC}/lib/*.dll")
        file(COPY ${_DLL_FILES} DESTINATION "${_DST}")

        # Same story for the USD plugins.
        file(GLOB _PLUGIN_FILES "${_SRC}/plugin/usd/*" "${_SRC}/bin/usd/*" "${_SRC}/lib/usd/*")
        file(COPY ${_PLUGIN_FILES} DESTINATION "${_DST}/usd")
    endforeach()
endif()
