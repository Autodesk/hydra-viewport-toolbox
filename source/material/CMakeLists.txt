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
project(Material
    VERSION "${_VERSION}"
    DESCRIPTION "Hydra viewport toolbox material utilities"
    LANGUAGES CXX
)

# Prepare data needed for the target.
# NOTE: The target name is also provided to the parent scope.
set(_TARGET "hvt_material")
set(_MATERIAL_TARGET ${_TARGET} PARENT_SCOPE)
set(_MATERIAL_INCLUDE_DIR "${_HVT_INCLUDE_DIR}/hvt/material")

# TODO: Complete the project definition.
