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
set(_TARGET "hvt_geometry")
set(_GEOMETRY_TARGET ${_TARGET} PARENT_SCOPE)
set(_GEOMETRY_INCLUDE_DIR "${_HVT_INCLUDE_DIR}/hvt/geometry")

# Collect the source and header files.
set(_SOURCE_FILES
    "geometry.cpp"
)
set(_HEADER_FILES
    "${_GEOMETRY_INCLUDE_DIR}/geometry.h"
)

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

# Link to the relevant OpenUSD targets. This will populate include directories, link libraries, etc.
# for both the OpenUSD targets and their own dependencies.
# NOTE: A minimal set of targets is specified here; this may need to change if the dependencies
# between the OpenUSD targets change in the future.
target_link_libraries(${_TARGET}
    PUBLIC hdSt
)

# Add the target to the export set for HVT.
install(TARGETS ${_TARGET}
    EXPORT ${_EXPORT_NAME}Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)