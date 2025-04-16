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
project(hvt_test
    VERSION     "${_VERSION}"
    DESCRIPTION "Hydra viewport toolbox testing"
    LANGUAGES   CXX)

# Prepare data needed for the target.
# NOTE: The target name is also provided to the parent scope.
set(_TARGET "hvt_test")
set(_TEST_TARGET ${_TARGET} PARENT_SCOPE)

#---------------------------------------------------------------------------------------------------

# This section lists all the third-party dependencies.

include(FetchContent)

function(get_googletest)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.16.0
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endfunction()

get_googletest()

#------------

function(get_glfw)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "[C|c]lang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # FIXME It applies to all the following projects i.e., these settings are not encapsulated.
        add_compile_options(-Wno-unused-parameter)
        add_compile_options(-Wno-missing-field-initializers)
        add_compile_options(-Wno-sign-compare)
        add_compile_options(-Wno-pedantic)
    endif()

    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endfunction()

get_glfw()

#------------

# Find OpenGL i.e., mandatory for OpenUSD.
find_package(OpenGL REQUIRED)
if (NOT OpenGL_FOUND)
    message(FATAL_ERROR "OpenGL was not found.")
endif()

#------------

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

#------------

# If OpenUSD was built with Vulkan support, the Vulkan package is also required. We detect this by
# checking for a specific directory in the OpenUSD install path.
if (EXISTS "${pxr_DIR}/lib/usd/hgiVulkan")
    find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
    if (NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan was not found.")
    endif()
endif()

#---------------------------------------------------------------------------------------------------

enable_testing()

# Do not copy all the OpenUSD libraries.
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_INSTALL_RPATH "${pxr_DIR}/lib")

add_executable(${_TARGET}
    test1.cpp
    test2.cpp
    test3.cpp
)

target_link_libraries(${_TARGET}
    GTest::gtest_main
    hvt
    hdx
    usdImaging
    glfw
)

include(GoogleTest)
gtest_discover_tests(${_TARGET})
