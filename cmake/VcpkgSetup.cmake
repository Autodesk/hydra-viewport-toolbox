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


# Features to enable in vcpkg prior to the project definition.
if(ENABLE_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
# Enable vcpkg USD feature only if no local USD path is provided
if(NOT DEFINED OPENUSD_INSTALL_PATH)
  list(APPEND VCPKG_MANIFEST_FEATURES "usd-build")
endif()

# VCPKG Bootstrap prior to the project definition.
if(VCPKG_MANIFEST_FEATURES)
    set(VCPKG_DIR "${CMAKE_SOURCE_DIR}/vcpkg")   
    if(NOT EXISTS "${VCPKG_DIR}")
        message(STATUS "vcpkg directory not found but vcpkg features were requested. Fetching submodules.")
        execute_process(COMMAND git submodule update --init --recursive
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
    endif()
    # vcpkg executable is present if boostrap was already executed
    if(WIN32)
        if(NOT EXISTS "${VCPKG_DIR}/vcpkg.exe")
            message(STATUS "Bootstrapping vcpkg (Windows)...")
            execute_process(COMMAND cmd /c bootstrap-vcpkg.bat
                WORKING_DIRECTORY "${VCPKG_DIR}")
        endif()
    else()
        if(NOT EXISTS "${VCPKG_DIR}/vcpkg")
            message(STATUS "Bootstrapping vcpkg (Unix)...")
            execute_process(COMMAND ./bootstrap-vcpkg.sh
                WORKING_DIRECTORY "${VCPKG_DIR}")
        endif()
    endif()
    # Define the toolchain if it was not already defined in the command line
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()
    # Overlay ports are there to change the default behavior of vcpkg on standard ports or add new ports.
    set(VCPKG_OVERLAY_PORTS "${CMAKE_SOURCE_DIR}/overlay-ports")
else()
    message(STATUS "Not using vcpkg for hydra-viewport-toolbox because no dependencies requested...")
endif()