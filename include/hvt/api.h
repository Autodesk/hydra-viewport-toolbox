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

/// Define an "HVT_API export symbol for the library. This depends on the platform and whether the
/// library is being built or used. The symbol should be added to all public functions; it should
/// *not* be added to classes in this project.
#if defined(_WIN32) || defined(_WIN64)
    #if defined(HVT_SHARED)
        #if defined(HVT_BUILD)
            // Building a Windows DLL, so specify export.
            #define HVT_API __declspec(dllexport)
        #else
            // Using a Windows DLL, so specify import.
            #define HVT_API __declspec(dllimport)
        #endif
    #else
        #define HVT_API
    #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
    #define HVT_API __attribute__((visibility("default")))
#else
    #define HVT_API
#endif

/// Include namespace and version symbols, generated from the CMake project version.
#include <hvt/namespace.h>
