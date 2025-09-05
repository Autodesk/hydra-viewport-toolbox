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
set(_TARGET "hvt_utils")
set(_UTILS_TARGET ${_TARGET} PARENT_SCOPE)
set(_UTILS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Collect the source files.
set(_SOURCE_FILES
    "pathUtils.cpp"
)

# Collect the header files.
set(_HEADER_FILES
    "${_UTILS_INCLUDE_DIR}/pathUtils.h"
)

if(APPLE)
    # Compile cpp as Objective-C++
    set_source_files_properties(pathUtils.cpp
        PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
endif ()

# Define the library.
# NOTE: OBJECT is used so that the parent library contain the contents of this one.
add_library(${_TARGET} OBJECT
    ${_SOURCE_FILES}
    ${_HEADER_FILES}
)

# Set the required include directories for the library.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${_HVT_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

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
