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

function(get_glew)
    FetchContent_Declare(
        glew
        GIT_REPOSITORY https://git.autodesk.com/autodesk-forks/glew-cmake
        GIT_TAG 79985c4
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "[C|c]lang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        # FIXME It applies to all the following projects i.e., these settings are not encapsulated.
        add_compile_options(-Wno-strict-prototypes)
    endif()

    set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glew)
endfunction()

get_glew()

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

#---------------------------------------------------------------------------------------------------

enable_testing()

add_executable(${_TARGET}
    main.cpp
    test1.cpp
    test2.cpp
)

if(APPLE)
    # Do not copy the third-party libraries:
    # 1. The OpenUSD library location.
    # 2. The default library location.
    # 3. The faulty glfw library location.

    # Adjust the output destination as Xcode always appends the build type to the output directory
    # but single config generators (e.g., Ninja) do not.
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set_target_properties(${_TARGET}
            PROPERTIES
                INSTALL_RPATH "${pxr_DIR}/lib;${_HVT_OUTPUT_DIR}/lib/${CMAKE_BUILD_TYPE};${_HVT_OUTPUT_DIR}/_deps/glfw-build/src/${CMAKE_BUILD_TYPE}"
        )
    else()
        set_target_properties(${_TARGET}
            PROPERTIES
                INSTALL_RPATH "${pxr_DIR}/lib;${_HVT_OUTPUT_DIR}/lib;${_HVT_OUTPUT_DIR}/_deps/glfw-build/src"
        )
    endif()
elseif(WIN32)
    # Adjust the output destination as Visual Studio always appends the build type to the output
    # directory but single config generators do not.
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set(_DST "${_HVT_OUTPUT_DIR}/bin/${CMAKE_BUILD_TYPE}")
    else()
        set(_DST "${_HVT_OUTPUT_DIR}/bin")
    endif()
    # On the Windows platform, the safest approach is to copy libraries where are the executables.
    file(GLOB lib_files "${pxr_DIR}/lib/*.dll")
    file(COPY ${lib_files} DESTINATION "${_DST}")
    file(GLOB bin_files "${pxr_DIR}/bin/*.dll")
    file(COPY ${bin_files} DESTINATION "${_DST}")
    file(GLOB plugin_files "${pxr_DIR}/plugin/usd/*")
    file(COPY ${plugin_files} DESTINATION "${_DST}/usd")
    file(GLOB plugin_files "${pxr_DIR}/lib/usd/*")
    file(COPY ${plugin_files} DESTINATION "${_DST}/usd")
else()
    message(FATAL_ERROR "Platform not supported")
endif()

set_target_properties(${_TARGET}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_HVT_OUTPUT_DIR}/bin"
)

target_link_libraries(${_TARGET}
    GTest::gtest_main
    hvt
    hdx
    usdImaging
    OpenGL::GL
    libglew_static
    glfw
)

#---------------------------------------------------------------------------------------------------

include(GoogleTest)

# Delay test discovery which has a better chance at finding appropriate runtime dependencies
# fixing the error of running test executable during the build.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

gtest_discover_tests(${_TARGET})
