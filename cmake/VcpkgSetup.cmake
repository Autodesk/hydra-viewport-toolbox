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

# We can't use PROJECT_IS_TOP_LEVEL because it's
# empty until the first project() call.
if(PROJECT_SOURCE_DIR)
    message(STATUS "Skipping vcpkg when used in another project")
    return()
endif()

# Features to enable in vcpkg prior to the project definition.
if(ENABLE_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
# Enable vcpkg USD feature only if no local USD path is provided
if(NOT OPENUSD_INSTALL_PATH)
  list(APPEND VCPKG_MANIFEST_FEATURES "usd-minimal")
endif()

set(vcpkg_dir "${CMAKE_SOURCE_DIR}/vcpkg")
if(NOT EXISTS "${vcpkg_dir}/ports")
    message(STATUS "Initializing vcpkg submodule...")
    execute_process(COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
endif()

# vcpkg executable is present if boostrap was already executed
if(WIN32)
    if(NOT EXISTS "${vcpkg_dir}/vcpkg.exe")
        message(STATUS "Bootstrapping vcpkg...")
        execute_process(COMMAND cmd /c bootstrap-vcpkg.bat
            WORKING_DIRECTORY "${vcpkg_dir}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
else()
    if(NOT EXISTS "${vcpkg_dir}/vcpkg")
        message(STATUS "Bootstrapping vcpkg...")
        execute_process(COMMAND ./bootstrap-vcpkg.sh
            WORKING_DIRECTORY "${vcpkg_dir}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
endif()

# Define the toolchain if it was not already defined in the command line
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${vcpkg_dir}/scripts/buildsystems/vcpkg.cmake")
endif()
