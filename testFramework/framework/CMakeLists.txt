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
set(_TEST_FRAMEWORK_TARGET ${_TARGET} PARENT_SCOPE)
set(_TEST_FRAMEWORK_INCLUDE_DIR "${_HVT_INCLUDE_DIR}/hvt/testFramework")

set(_SOURCE_FILES
    testFramework.cpp
    testContextCreator.cpp
    testGlobalFlags.cpp
)

set(_HEADER_FILES
    "${_TEST_FRAMEWORK_INCLUDE_DIR}/testContextCreator.h"
    "${_TEST_FRAMEWORK_INCLUDE_DIR}/testFramework.h"
    "${_TEST_FRAMEWORK_INCLUDE_DIR}/testGlobalFlags.h"
    "${_TEST_FRAMEWORK_INCLUDE_DIR}/testUsdHelpers.h"
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

#---------------------------------------------------------------------------------------------------

# Define the library.
# NOTE: OBJECT is used so that the parent library contain the contents of this one.
add_library(${_TARGET} OBJECT
    ${_SOURCE_FILES}
    ${_HEADER_FILES}
)

#---------------------------------------------------------------------------------------------------

# Set the required include directories for the library.
target_include_directories(${_TARGET}
    PUBLIC
        "$<BUILD_INTERFACE:${_HVT_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "../." # Access to utils headers.
)

# Where to find all the resource files.
target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_RESOURCE_PATH=${_HVT_INCLUDE_DIR}/hvt/resources
)

# Where to find all the data files for the unit tests.
target_compile_definitions(${_TARGET}
    PRIVATE
        HVT_TEST_DATA_PATH=${CMAKE_SOURCE_DIR}/test
)

# Where to output any processing results.
target_compile_definitions(${_TARGET}
    PRIVATE
        TEST_DATA_OUTPUT_PATH=${CMAKE_BINARY_DIR}/test
)

#---------------------------------------------------------------------------------------------------

# GLEW target is suffixed when static.
if(TARGET GLEW::glew_s)
    set(glew_dep GLEW::glew_s)
else()
    set(glew_dep GLEW::glew)
endif()

target_link_libraries(${_TARGET}
    PUBLIC
        glfw
        ${glew_dep}
        hvt
)

#---------------------------------------------------------------------------------------------------

# Add the target to the export set for HVT.
install(TARGETS ${_TARGET}
    EXPORT ${_EXPORT_NAME}Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
