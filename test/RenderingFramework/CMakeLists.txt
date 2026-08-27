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

set(_TARGET "hvt_test_framework")
set(_FRAMEWORK_TARGET ${_TARGET} PARENT_SCOPE)

#---------------------------------------------------------------------------------------------------

# The framework owns its own dependency lookups so that it can be added independently of the unit
# test suite (which used to be the only place these were found). Only look them up when the consumer
# has not already provided them, because downstream repositories embedding HVT find these first.
if (NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
    find_package(SDL2 CONFIG REQUIRED)
endif()
if (NOT TARGET GLEW::glew AND NOT TARGET GLEW::glew_s AND NOT TARGET libglew_shared)
    find_package(glew CONFIG REQUIRED)
endif()

# The SDL2 and GLEW target names differ by provider: vcpkg exports SDL2::SDL2 / SDL2::SDL2-static and
# GLEW::glew / GLEW::glew_s, while the Autodesk dependency tool exports SDL2::SDL2 and libglew_shared.
# The framework is embedded in builds that use either, so bind to whichever target exists.
if (TARGET SDL2::SDL2)
    set(_SDL2_TARGET SDL2::SDL2)
elseif (TARGET SDL2::SDL2-static)
    set(_SDL2_TARGET SDL2::SDL2-static)
else()
    message(FATAL_ERROR "hvt_test_framework: no SDL2 target found (SDL2::SDL2 / SDL2::SDL2-static).")
endif()

if (TARGET GLEW::glew)
    set(_GLEW_TARGET GLEW::glew)
elseif (TARGET GLEW::glew_s)
    set(_GLEW_TARGET GLEW::glew_s)
elseif (TARGET libglew_shared)
    set(_GLEW_TARGET libglew_shared)
else()
    message(FATAL_ERROR "hvt_test_framework: no GLEW target found (GLEW::glew / GLEW::glew_s / libglew_shared).")
endif()

#---------------------------------------------------------------------------------------------------

# The sources common to every platform.
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
if (HVT_ENABLE_VULKAN)
    list(APPEND _SOURCE_FILES
        VulkanTestContext.cpp
        VulkanTestContext.h
    )
endif()

#---------------------------------------------------------------------------------------------------

# The rendering test framework is intentionally a static library: it is a unit-test helper, so a
# static archive avoids the import/export-symbol handling a shared library would need on Windows.
add_library(${_TARGET} STATIC ${_SOURCE_FILES})
add_library(hvt::${_TARGET} ALIAS ${_TARGET})

# The framework headers are included as 'RenderingFramework/<header>' by the framework itself and by
# every unit test, so the parent of this directory is a public include directory. On install the
# headers live under 'include/hvt/test/RenderingFramework', so the install interface points at
# 'include/hvt/test'.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
        "$<INSTALL_INTERFACE:include/hvt/test>"
)

# hvt is exposed through TestHelpers.h; SDL2/GLEW/hd are part of the framework's public surface.
target_link_libraries(${_TARGET}
    PUBLIC
        hvt
        ${_SDL2_TARGET}
        ${_GLEW_TARGET}
        hd
)

# The remaining OpenUSD libraries the framework needs. Each target's existence is checked because
# the available OpenUSD libraries depend on the platform and on the OpenUSD build.
foreach(_LIBRARY IN ITEMS gf glf hgi tf usd usdGeom)
    if (TARGET ${_LIBRARY})
        target_link_libraries(${_TARGET} PRIVATE ${_LIBRARY})
    endif()
endforeach()

if (HVT_ENABLE_VULKAN)
    # VulkanTestContext.cpp calls the Vulkan API directly, so the loader (which exports the vk*
    # entry points) is part of the framework's PUBLIC link interface: a consumer that only links
    # hvt::hvt_test_framework (e.g. a downstream repository) must inherit it or it hits unresolved
    # vk* symbols at link time. Look it up only when the embedding build has not already provided it,
    # mirroring the SDL2/GLEW handling above.
    if (NOT TARGET Vulkan::Loader)
        find_package(VulkanLoader CONFIG REQUIRED)
    endif()
    target_link_libraries(${_TARGET}
        PUBLIC
            Vulkan::Loader
            GPUOpen::VulkanMemoryAllocator
    )
endif()

set_target_properties(${_TARGET} PROPERTIES FOLDER "Unit Tests")

#---------------------------------------------------------------------------------------------------

# The framework resolves its data paths from the environment at runtime (see TestHelpers.cpp), so
# the installed archive carries no build-tree path. These compile-time defaults are the fallback
# used when the environment variables are unset, keeping HVT's own top-level test run zero-config.

# Where to find all the data files for the unit tests.
cmake_path(SET hvt_test_data_normalized NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../test")
# Remove the last '/' if any.
get_filename_component(hvt_test_data_normalized "${hvt_test_data_normalized}" DIRECTORY)

# Where to output any processing results.
cmake_path(SET test_data_output_normalized NORMALIZE "${CMAKE_CURRENT_BINARY_DIR}/..")
# Remove the last '/' if any.
get_filename_component(test_data_output_normalized "${test_data_output_normalized}" DIRECTORY)

target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_TEST_DATA_PATH=${hvt_test_data_normalized}
        HVT_TEST_DATA_OUTPUT_PATH=${test_data_output_normalized}
        # Based on HVT's own include dir (not CMAKE_SOURCE_DIR) so the default is correct both for a
        # top-level HVT build and for downstream repositories embedding HVT.
        HVT_RESOURCE_PATH=${_HVT_INCLUDE_DIR}/hvt/resources
)

#---------------------------------------------------------------------------------------------------
# Install the rendering test framework as a relocatable package so other libraries can consume it via
# find_package(hvt_test_framework). Kept separate from the main hvt package so its test-only
# dependencies (SDL2, GLEW, VMA) do not enter every find_package(hvt).

include(CMakePackageConfigHelpers)

set(_PKG "hvt_test_framework")

# The Vulkan dependency lines are only emitted when the framework was built with Vulkan. The loader
# is part of the public link interface (see above), so it must be re-found by downstream consumers.
if (HVT_ENABLE_VULKAN)
    set(_HVT_TEST_FRAMEWORK_VULKAN_DEP "find_dependency(VulkanLoader)\nfind_dependency(VulkanMemoryAllocator)")
else()
    set(_HVT_TEST_FRAMEWORK_VULKAN_DEP "")
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/${_PKG}Config.cmake.in"
    "${PROJECT_BINARY_DIR}/${_PKG}Config.cmake"
    INSTALL_DESTINATION "lib/cmake/${_PKG}"
    NO_SET_AND_CHECK_MACRO
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/${_PKG}ConfigVersion.cmake"
    VERSION "${${PROJECT_NAME}_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(TARGETS ${_TARGET}
    EXPORT ${_PKG}Targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Public headers, preserving the 'RenderingFramework/<header>' include layout.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
    DESTINATION "include/hvt/test/RenderingFramework"
    FILES_MATCHING
        PATTERN "*.h"
)

# NOTE: A static library has no linker-produced PDB ($<TARGET_PDB_FILE>), so there is nothing to
# install here; debug information travels with the object code in the archive.

install(FILES
    "${PROJECT_BINARY_DIR}/${_PKG}Config.cmake"
    "${PROJECT_BINARY_DIR}/${_PKG}ConfigVersion.cmake"
    DESTINATION "lib/cmake/${_PKG}"
)

install(EXPORT ${_PKG}Targets
    DESTINATION "lib/cmake/${_PKG}"
    FILE "${_PKG}Targets.cmake"
    NAMESPACE hvt::
    EXPORT_LINK_INTERFACE_LIBRARIES
)
