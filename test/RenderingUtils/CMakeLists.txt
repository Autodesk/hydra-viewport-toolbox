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
set(_TARGET "hvt_test_utils")
set(_FRAMEWORK_UTILS_TARGET ${_TARGET} PARENT_SCOPE)

set(_FILES
    "CollectTraces.h"
    "ImageUtils.cpp"
    "ImageUtils.h"
    "stb/stb_image.h"
    "stb/stb_image_impl.cpp"
    "stb/stb_image_write.h"
)

# Define the library.
# NOTE: OBJECT is used so that the parent library contain the contents of this one.
add_library(${_TARGET} OBJECT
    ${_FILES}
)

# Set the required include directories for the library.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${_HVT_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
        "../."
)

target_link_libraries(${_TARGET}
    PUBLIC
        hd
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