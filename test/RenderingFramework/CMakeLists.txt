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
set(_TARGET "hvt_test_framework")
set(_FRAMEWORK_TARGET ${_TARGET} PARENT_SCOPE)

# Define the library.
# NOTE: The list of source files is owned by the HvtTestFramework module so that the downstream
# repositories embedding HVT can build the very same framework without repeating it.
# NOTE: OBJECT is used so that the parent library contain the contents of this one.
include(HvtTestFramework)

hvt_add_rendering_framework(
    TARGET      ${_TARGET}
    TYPE        OBJECT
    WITH_VULKAN ${HVT_ENABLE_VULKAN}
)

# Set the required include directories for the library.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${_HVT_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# Where to find all the resource files.
target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_RESOURCE_PATH=${CMAKE_SOURCE_DIR}/include/hvt/resources
)

# Where to find all the data files for the unit tests.
cmake_path(SET hvt_test_data_normalized NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../test")
# Remove the last '/' if any.
get_filename_component(hvt_test_data_normalized "${hvt_test_data_normalized}" DIRECTORY)
target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_TEST_DATA_PATH=${hvt_test_data_normalized}
)

# Where to output any processing results.
cmake_path(SET test_data_output_normalized NORMALIZE "${CMAKE_CURRENT_BINARY_DIR}/..")
# Remove the last '/' if any.
get_filename_component(test_data_output_normalized "${test_data_output_normalized}" DIRECTORY)
target_compile_definitions(${_TARGET}
    PRIVATE
        TEST_DATA_OUTPUT_PATH=${test_data_output_normalized}
)

target_link_libraries(${_TARGET}
    PUBLIC
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        hd
        $<IF:$<TARGET_EXISTS:GLEW::glew>,GLEW::glew,GLEW::glew_s>
)

if (HVT_ENABLE_VULKAN)
    target_link_libraries(${_TARGET}
        PUBLIC
            GPUOpen::VulkanMemoryAllocator
    )
endif()

# Add the target to the export set for HVT.
# NOTE: Even though this is a private library and should not normally be exported, we do export
# it for the Emscripten generator because it needs access to everything.
if (EMSCRIPTEN)
    install(TARGETS ${_TARGET}
        EXPORT ${_EXPORT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()
