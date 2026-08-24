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

# Defines the rendering test framework (i.e. the helpers shared by all the rendering unit tests)
# as a single reusable target, so that its list of source files only lives in this repository.
#
# The framework sources are consumed by this repository and by downstream repositories embedding
# HVT (which cannot use the 'hvt_test_framework' target itself because HVT's test directory is
# only added for a top-level build). Without this module, every consumer must repeat the list of
# source files, and adding or renaming a file means changing every consumer.

# The location of the framework sources, captured while this module is being included because the
# module is included from the consumer's directory (i.e. any relative path would resolve there).
get_filename_component(_HVT_RENDERING_FRAMEWORK_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../test/RenderingFramework" ABSOLUTE)
set(HVT_RENDERING_FRAMEWORK_DIR "${_HVT_RENDERING_FRAMEWORK_DIR}"
    CACHE INTERNAL "Directory containing the HVT rendering test framework sources")

# The directory to add to the include path so that the framework headers are reachable as
# 'RenderingFramework/<header>' (i.e. the way all the unit tests include them).
get_filename_component(_HVT_RENDERING_FRAMEWORK_INCLUDE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../test" ABSOLUTE)
set(HVT_RENDERING_FRAMEWORK_INCLUDE_DIR "${_HVT_RENDERING_FRAMEWORK_INCLUDE_DIR}"
    CACHE INTERNAL "Include directory for the HVT rendering test framework headers")

# Creates a target containing the rendering test framework.
#
# Usage:
#   hvt_add_rendering_framework(
#       TARGET       <target>
#       TYPE         <OBJECT|STATIC>
#       WITH_VULKAN  <ON|OFF>
#   )
#
# The created target only carries what is identical for every consumer: the source files, the
# include directory for the framework headers, and the libraries the framework always needs.
# Everything which legitimately differs per consumer is left to the caller, i.e. the
# HVT_TEST_DATA_PATH, TEST_DATA_OUTPUT_PATH, and HVT_RESOURCE_PATH definitions, the windowing
# and OpenGL loader libraries (whose target names depend on how they were provided), the Vulkan
# libraries, the compiler flags, and the target properties.
#
# NOTE: WITH_VULKAN is an explicit argument because the option enabling Vulkan does not have the
# same name in every consumer.
function(hvt_add_rendering_framework)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET;TYPE;WITH_VULKAN" "")

    if (NOT ARG_TARGET)
        message(FATAL_ERROR "hvt_add_rendering_framework: TARGET is required.")
    endif()

    if (NOT ARG_TYPE MATCHES "^(OBJECT|STATIC)$")
        message(FATAL_ERROR
            "hvt_add_rendering_framework: TYPE must be OBJECT or STATIC (got '${ARG_TYPE}').")
    endif()

    if (ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "hvt_add_rendering_framework: unknown argument(s) '${ARG_UNPARSED_ARGUMENTS}'.")
    endif()

    #-----------------------------------------------------------------------------------------------

    # The sources common to all the platforms.
    set(_SOURCE_FILES
        CollectTraces.h
        GpuImageCapture.cpp
        GpuImageCapture.h
        ImageUtils.cpp
        ImageUtils.h
        TestHelpers.cpp
        TestHelpers.h
        UsdHelpers.h
        TestFlags.h
        TestContextCreator.h
        stb/stb_image.h
        stb/stb_image_impl.cpp
        stb/stb_image_write.h
    )

    # The rendering context implementation depends on the platform.
    if (IOS)
        list(APPEND _SOURCE_FILES
            MetalTestContext.mm
            MetalTestContext.h
            iOSTestHelpers.mm
            iOSTestHelpers.h
            pxrusd.h
        )
    elseif (ANDROID)
        list(APPEND _SOURCE_FILES
            AndroidTestContext.cpp
            AndroidTestContext.h
        )
    else()
        list(APPEND _SOURCE_FILES
            OpenGLTestContext.cpp
            OpenGLTestContext.h
        )
    endif()

    # VulkanTestContext is only needed when Vulkan is enabled.
    if (ARG_WITH_VULKAN)
        list(APPEND _SOURCE_FILES
            VulkanTestContext.cpp
            VulkanTestContext.h
        )
    endif()

    # The sources are always referenced with an absolute path because this module is included from
    # the consumer's directory.
    list(TRANSFORM _SOURCE_FILES PREPEND "${HVT_RENDERING_FRAMEWORK_DIR}/")

    #-----------------------------------------------------------------------------------------------

    add_library(${ARG_TARGET} ${ARG_TYPE}
        ${_SOURCE_FILES}
    )

    # The framework headers are included as 'RenderingFramework/<header>' by the framework itself
    # and by all the unit tests, so the include directory is public.
    target_include_directories(${ARG_TARGET}
        PUBLIC
            "$<BUILD_INTERFACE:${HVT_RENDERING_FRAMEWORK_INCLUDE_DIR}>"
    )

    # HVT is exposed in TestHelpers.h.
    target_link_libraries(${ARG_TARGET}
        PUBLIC
            hvt
    )

    # The OpenUSD libraries the framework always needs. The existence of every target is checked
    # because the available OpenUSD libraries depend on the platform and on the OpenUSD build.
    foreach(_LIBRARY IN ITEMS gf glf hd hgi tf usd usdGeom)
        if (TARGET ${_LIBRARY})
            target_link_libraries(${ARG_TARGET}
                PRIVATE
                    ${_LIBRARY}
            )
        endif()
    endforeach()
endfunction()
