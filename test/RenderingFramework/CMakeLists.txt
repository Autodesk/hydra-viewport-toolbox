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

set(_SOURCE_FILES
    TestHelpers.cpp
    TestHelpers.h
    UsdHelpers.h
    TestFlags.h
    TestContextCreator.h
)

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

add_library(${_TARGET} STATIC
    ${_SOURCE_FILES}
)

# Set the required include directories for the library.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${_HVT_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
        "../."
)

# Where to find all the resource files.
target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_RESOURCE_PATH=${CMAKE_SOURCE_DIR}/include/hvt/resources
)

# Where to find all the data files for the unit tests.
target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_TEST_DATA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Where to output any processing results.
target_compile_definitions(${_TARGET}
    PRIVATE
        TEST_DATA_OUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/..
)

# GLEW target is suffixed when static.
if(TARGET GLEW::glew_s)
    set(glew_dep GLEW::glew_s)
else()
    set(glew_dep GLEW::glew)
endif()

target_link_libraries(${_TARGET}
    PUBLIC
        glfw
        hvt
        hvt_test_utils
        ${glew_dep}
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
