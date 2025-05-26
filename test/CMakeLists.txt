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
project(Test
    VERSION     "${_VERSION}"
    DESCRIPTION "Hydra viewport toolbox testing"
    LANGUAGES   CXX)

# Prepare data needed for the target.
# NOTE: The target name is also provided to the parent scope.
set(_TARGET "hvt_test")
set(_TEST_TARGET ${_TARGET} PARENT_SCOPE)

#---------------------------------------------------------------------------------------------------

# This section lists all the third-party dependencies only needed by the unit tests.

# Find vcpkg-provided packages
find_package(GTest CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glew CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

#---------------------------------------------------------------------------------------------------

add_executable(${_TARGET}
    main.cpp
    test1.cpp
    test2.cpp
)

# Is the generator a multi-config one?
# Note: The multi-configuration generators always append the build type to the output directories
# (where the single-configuration generators do not) invalidating/changing some predefined paths.
get_cmake_property(_MULTI_CONFIG GENERATOR_IS_MULTI_CONFIG)

if (APPLE)
    # Do not copy the third-party libraries by updating the rpath with:
    # 1. The OpenUSD library location.
    # 2. The default library location.
    # 3. The faulty glfw library location.

    if(_MULTI_CONFIG)
        set_target_properties(${_TARGET}
            PROPERTIES
                INSTALL_RPATH "${OPENUSD_INSTALL_PATH}/lib;${_HVT_OUTPUT_DIR}/lib/${CMAKE_BUILD_TYPE}"
        )
    else()
        set_target_properties(${_TARGET}
            PROPERTIES
                INSTALL_RPATH "${OPENUSD_INSTALL_PATH}/lib;${_HVT_OUTPUT_DIR}/lib"
        )
    endif()
elseif (WIN32)
    # On the Windows platform, the safest approach is to copy libraries where are the executables.

    if(_MULTI_CONFIG)
        set(_DST "${_HVT_OUTPUT_DIR}/bin/${CMAKE_BUILD_TYPE}")
    else()
        set(_DST "${_HVT_OUTPUT_DIR}/bin")
    endif()
    file(GLOB lib_files "${OPENUSD_INSTALL_PATH}/lib/*.dll")
    file(COPY ${lib_files} DESTINATION "${_DST}")
    file(GLOB bin_files "${OPENUSD_INSTALL_PATH}/bin/*.dll")
    file(COPY ${bin_files} DESTINATION "${_DST}")
    file(GLOB plugin_files "${OPENUSD_INSTALL_PATH}/plugin/usd/*")
    file(COPY ${plugin_files} DESTINATION "${_DST}/usd")
    file(GLOB plugin_files "${OPENUSD_INSTALL_PATH}/lib/usd/*")
    file(COPY ${plugin_files} DESTINATION "${_DST}/usd")
else()
    message(FATAL_ERROR "Platform not supported")
endif()

target_link_libraries(${_TARGET}
    PRIVATE
        GTest::gtest_main
        glfw
        GLEW::glew_s
        OpenGL::GL
        hvt
        hdx
        usdImaging
)

#---------------------------------------------------------------------------------------------------

# This section defines & discovers the unit tests.

include(GoogleTest)

# Delay test discovery which has a better chance at finding appropriate runtime dependencies
# fixing the error of running test executable during the build.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

# Register the test with CTest and allow more time to discover the tests.
gtest_discover_tests(hvt_test
    DISCOVERY_TIMEOUT 30
    PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
