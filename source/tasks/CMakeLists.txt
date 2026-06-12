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
set(_TARGET "hvt_tasks")
set(_TASKS_TARGET ${_TARGET} PARENT_SCOPE)
set(_TASKS_INCLUDE_DIR "${_HVT_INCLUDE_DIR}/hvt/tasks")

# Collect the source files.
set(_SOURCE_FILES
    "aovInputTask.cpp"
    "blurTask.cpp"
    "composeTask.cpp"
    "flashPickTask.cpp"
    "fxaaTask.cpp"
    "resources.cpp"
    "ssaoTask.cpp"
    "wboitRenderTask.cpp"
    "wboitResolveTask.cpp"
)

# Collect the public header files.
set(_HEADER_FILES
    "${_TASKS_INCLUDE_DIR}/ambientOcclusion.h"
    "${_TASKS_INCLUDE_DIR}/aovInputTask.h"
    "${_TASKS_INCLUDE_DIR}/blurTask.h"
    "${_TASKS_INCLUDE_DIR}/composeTask.h"
    "${_TASKS_INCLUDE_DIR}/flashPickTask.h"
    "${_TASKS_INCLUDE_DIR}/fxaaTask.h"
    "${_TASKS_INCLUDE_DIR}/resources.h"
    "${_TASKS_INCLUDE_DIR}/ssaoTask.h"
    "${_TASKS_INCLUDE_DIR}/wboitRenderTask.h"
    "${_TASKS_INCLUDE_DIR}/wboitResolveTask.h"
)

set(_PRIVATE_HEADER_FILES
    "wboitTokens.h"
)

# Add custom VisualizeAovTask implementation when ADSK_OPENUSD_PENDING is NOT enabled.
# When ADSK_OPENUSD_PENDING is defined, the OpenUSD HdxVisualizeAovTask is used instead.
# This provides a custom AOV visualization task with GPU-only depth min/max computation.
if(NOT ENABLE_ADSK_OPENUSD_PENDING)
    list(APPEND _SOURCE_FILES
        "visualizeAovCompute.cpp"
        "visualizeAovTask.cpp"
    )
    list(APPEND _HEADER_FILES
        "${_TASKS_INCLUDE_DIR}/visualizeAovTask.h"
    )
    list(APPEND _PRIVATE_HEADER_FILES
        "visualizeAovCompute.h"
    )
endif()

# Define the library.
hvt_add_sublibrary(${_TARGET}
    SOURCES ${_SOURCE_FILES}
    HEADERS ${_HEADER_FILES} ${_PRIVATE_HEADER_FILES}
)

# Set the required include directories for the library.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${_HVT_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${_HVT_SOURCE_DIR}" # Access to private headers.
)

# Link to the relevant OpenUSD targets. This will populate include directories, link libraries, etc.
# for both the OpenUSD targets and their own dependencies.
# NOTE: A minimal set of targets is specified here; this may need to change if the dependencies
# between the OpenUSD targets change in the future.
target_link_libraries(${_TARGET}
    PUBLIC hdx hdSt
)

# Add the target to the export set for HVT.
hvt_install_sublibrary(${_TARGET} ${_EXPORT_NAME})