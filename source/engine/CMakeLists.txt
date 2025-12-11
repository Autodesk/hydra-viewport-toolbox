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
    "${_ENGINE_INCLUDE_DIR}/renderBufferManager.h"
    "${_ENGINE_INCLUDE_DIR}/renderBufferSettingsProvider.h"
    "${_ENGINE_INCLUDE_DIR}/renderIndexProxy.h"
    "${_ENGINE_INCLUDE_DIR}/selectionSettingsProvider.h"
    "${_ENGINE_INCLUDE_DIR}/syncDelegate.h"
    "${_ENGINE_INCLUDE_DIR}/taskCreationHelpers.h"
    "${_ENGINE_INCLUDE_DIR}/taskManager.h"
    "${_ENGINE_INCLUDE_DIR}/taskUtils.h"
    "${_ENGINE_INCLUDE_DIR}/usdStageUtils.h"
    "${_ENGINE_INCLUDE_DIR}/viewport.h"
    "${_ENGINE_INCLUDE_DIR}/viewportEngine.h"
)

# Define the library.
# NOTE: OBJECT is used so that the parent library contain the contents of this one (for shared builds).
# For static builds, use STATIC so the sub-libraries can be properly installed and linked.
if (EMSCRIPTEN OR NOT BUILD_SHARED_LIBS)
    add_library(${_TARGET} STATIC
        ${_SOURCE_FILES}
        ${_HEADER_FILES}
    )
else()
    add_library(${_TARGET} OBJECT
        ${_SOURCE_FILES}
        ${_HEADER_FILES}
    )
endif()

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
    PUBLIC hdSt usdImaging
    PRIVATE hvt_dataSource
)

# Add the target to the export set for HVT.
# NOTE: Even though this is a private library and should not normally be exported, we do export
# it for the Emscripten generator and static builds because CMake needs access to everything.
if (EMSCRIPTEN OR NOT BUILD_SHARED_LIBS)
    install(TARGETS ${_TARGET}
        EXPORT ${_EXPORT_NAME}Targets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()