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

#---------------------------------------------------------------------------------------------------

# This section lists all the Unit Test Framework libraries.

# That's a private library.
add_subdirectory("RenderingUtils")

# That's a private library.
add_subdirectory("RenderingFramework")

#---------------------------------------------------------------------------------------------------

add_executable(${_TARGET}
    main.cpp
    howTos/howTo01_CreateHgiImplementation.cpp
    howTos/howTo02_CreateOneFramePass.cpp
    howTos/howTo03_CreateTwoFramePasses.cpp
    howTos/howTo04_CreateACustomRenderTask.cpp
    howTos/howTo05_UseSSAORenderTask.cpp
    howTos/howTo06_UseFXAARenderTask.cpp
    howTos/howTo07_UseIncludeExclude.cpp
    howTos/howTo08_UseBoundingBoxSceneIndex.cpp
    howTos/howTo09_UseWireFrameSceneIndex.cpp
    howTos/howTo10_CustomListOfTasks.cpp
    howTos/howTo11_UseSkyDomeTask.cpp
    tests/composeTaskHelpers.cpp
    tests/composeTaskHelpers.h
    tests/testComposeTask.cpp
    tests/testFramePass.cpp
    tests/testFramePasses.cpp
    tests/testSearches.cpp
    tests/testTaskManager.cpp
    tests/testMultiSampling.cpp
)

# Is the generator a multi-config one?
# Note: The multi-configuration generators always append the build type to the output directories
# (where the single-configuration generators do not) invalidating/changing some predefined paths.
get_cmake_property(_MULTI_CONFIG GENERATOR_IS_MULTI_CONFIG)

if (UNIX)
    # Do not copy the third-party libraries by updating the rpath with:
    # 1. The OpenUSD library location.
    # 2. The default library location.
    # 3. The OpenUSD plugins location.
    # Note: Handle the multi-generator cases.

    if(_MULTI_CONFIG)
        set(openusd_path_gen "$<IF:$<CONFIG:Debug>,${OPENUSD_INSTALL_PATH_DEBUG},${OPENUSD_INSTALL_PATH}>")
        list(APPEND _RPATH "${openusd_path_gen}/lib")
        list(APPEND _RPATH "${openusd_path_gen}/plugin")
        list(APPEND _RPATH "${_HVT_OUTPUT_DIR}/lib/$<CONFIG>")
    else()
        list(APPEND _RPATH "${OPENUSD_INSTALL_PATH}/lib")
        list(APPEND _RPATH "${OPENUSD_INSTALL_PATH}/plugin")
        list(APPEND _RPATH "${_HVT_OUTPUT_DIR}/lib")
    endif()

    set_target_properties(${_TARGET} PROPERTIES INSTALL_RPATH "${_RPATH}")
elseif (WIN32)
    # On the Windows platform, the safest approach is to copy libraries where are the executables.

    if(_MULTI_CONFIG)
        set(_CONFIGS ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(_CONFIGS ${CMAKE_BUILD_TYPE})
    endif()

    foreach(_CONFIG IN LISTS _CONFIGS)
        if(_MULTI_CONFIG)
            if(_CONFIG STREQUAL Debug)
                set(_SRC "${OPENUSD_INSTALL_PATH_DEBUG}")
            else()
                set(_SRC "${OPENUSD_INSTALL_PATH}")
            endif()
            set(_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_CONFIG}")
        else()
            set(_SRC "${OPENUSD_INSTALL_PATH}")
            set(_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        endif()

        # By default, the OpenUSD build from vcpkg only uses the official repo and the 'origin/dev' branch.
        # Then the ENABLE_ADSK_OPENUSD_PENDING cmake variable can only be false.
        if (NOT USE_LOCAL_USD)
            if (ENABLE_ADSK_OPENUSD_PENDING)
                message(FATAL_ERROR "ENABLE_ADSK_OPENUSD_PENDING and vcpkg cannot be defined at the same time")
            endif()

            # vcpkg & origin/dev
            file(GLOB dll_files "${_SRC}/bin/*.dll")
            file(COPY ${dll_files} DESTINATION "${_DST}")
            file(GLOB pdb_files "${_SRC}/bin/*.pdb")
            file(COPY ${pdb_files} DESTINATION "${_DST}")

            file(GLOB plugin_files "${_SRC}/plugin/usd/*")
            file(COPY ${plugin_files} DESTINATION "${_DST}/usd")

            file(GLOB more_plugin_files "${_SRC}/bin/usd/*")
            file(COPY ${more_plugin_files} DESTINATION "${_DST}/usd")
            file(COPY "${_SRC}/lib/usd/plugInfo.json" DESTINATION "${_DST}/usd")
        else()
            # local USD
            file(GLOB dll_files "${_SRC}/bin/*.dll")
            file(COPY ${dll_files} DESTINATION "${_DST}")
            file(GLOB pdb_files "${_SRC}/bin/*.pdb")
            file(COPY ${pdb_files} DESTINATION "${_DST}")

            file(GLOB dll_files "${_SRC}/lib/*.dll")
            file(COPY ${dll_files} DESTINATION "${_DST}")

            file(GLOB plugin_files "${_SRC}/plugin/usd/*")
            file(COPY ${plugin_files} DESTINATION "${_DST}/usd")

            file(GLOB more_plugin_files "${_SRC}/lib/usd/*")
            file(COPY ${more_plugin_files} DESTINATION "${_DST}/usd")
        endif()

    endforeach()
else()
    message(FATAL_ERROR "Platform not supported")
endif()

target_link_libraries(${_TARGET}
    PRIVATE
        GTest::gtest_main
        OpenGL::GL
        hdx
        hvt_test_framework
        hvt_test_utils
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
    PROPERTIES WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
