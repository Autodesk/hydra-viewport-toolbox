// Copyright 2025 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

// Generate version defines from the CMake project version.
#define HVT_MAJOR_VERSION 0
#define HVT_MINOR_VERSION 25
#define HVT_PATCH_LEVEL   03
#define HVT_VERSION (HVT_MAJOR_VERSION * 10000 + HVT_MINOR_VERSION * 100 + HVT_PATCH_LEVEL)

// C preprocessor trickery to expand arguments.
#define HVT_CONCAT(A, B) HVT_CONCAT_IMPL(A, B)
#define HVT_CONCAT_IMPL(A, B) A##B

// Versioned namespace includes the major version number.
// Note: The goal is to define a namespace like 'hvt_v0_25_03_adsk'.
#define hvt_0 HVT_CONCAT(hvt, _v0)
#define hvt_1 HVT_CONCAT(hvt_0, _25)
#define hvt_2 HVT_CONCAT(hvt_1, _03)
#define hvt HVT_CONCAT(hvt_2, )
