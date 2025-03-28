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
project(Engine
    VERSION "${_VERSION}"
    DESCRIPTION "Hydra viewport toolbox engine"
    LANGUAGES CXX
)

# Prepare data needed for the target.
# NOTE: The target name is also provided to the parent scope.
set(_TARGET "hvt_engine")
set(_ENGINE_TARGET ${_TARGET} PARENT_SCOPE)
set(_ENGINE_INCLUDE_DIR "${_HVT_INCLUDE_DIR}/hvt/engine")

# Collect the source files.
set(_SOURCE_FILES
    "delegateStreamUtils.h"
    "framePass.cpp"
    "framePassUtils.cpp"
    "hgiInstance.cpp"
    "lightingManager.cpp"
    "lightingManager.h"
    "renderBufferManager.cpp"
    "renderBufferManager.h"
    "renderIndexProxy.cpp"
    "selectionHelper.cpp"
    "selectionHelper.h"
    "syncDelegate.cpp"
    "taskCreationHelpers.cpp"
    "taskManager.cpp"
    "taskUtils.cpp"
    "viewport.cpp"
    "viewportEngine.cpp"
    "usdStageUtils.cpp"
)

# Collect the header files.
set(_HEADER_FILES
    "${_ENGINE_INCLUDE_DIR}/basicLayerParams.h"
    "${_ENGINE_INCLUDE_DIR}/framePass.h"
    "${_ENGINE_INCLUDE_DIR}/framePassUtils.h"
    "${_ENGINE_INCLUDE_DIR}/hgiInstance.h"
    "${_ENGINE_INCLUDE_DIR}/lightingSettingsProvider.h"
    "${_ENGINE_INCLUDE_DIR}/renderBufferSettingsProvider.h"
    "${_ENGINE_INCLUDE_DIR}/renderIndexProxy.h"
    "${_ENGINE_INCLUDE_DIR}/SelectionSettingsProvider.h"
    "${_ENGINE_INCLUDE_DIR}/syncDelegate.h"
    "${_ENGINE_INCLUDE_DIR}/taskCreationHelpers.h"
    "${_ENGINE_INCLUDE_DIR}/taskManager.h"
    "${_ENGINE_INCLUDE_DIR}/taskUtils.h"
    "${_ENGINE_INCLUDE_DIR}/usdStageUtils.h"
    "${_ENGINE_INCLUDE_DIR}/viewport.h"
    "${_ENGINE_INCLUDE_DIR}/viewportEngine.h"
    "${_ENGINE_INCLUDE_DIR}/viewportRect.h"
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
        "${_HVT_INCLUDE_DIR}"
        "${PXR_INCLUDE_DIRS}"
        "${_PYTHON3_INCLUDE_DIR}"
    PRIVATE
        "${CMAKE_SOURCE_DIR}" # Access to source/utils headers.
)
