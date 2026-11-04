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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wconditional-uninitialized"
#pragma clang diagnostic ignored "-Wuninitialized"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <gtest/gtest.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <string>
#include <filesystem>

#include <pxr/imaging/hgi/tokens.h>
#include <pxr/base/tf/token.h>

namespace TestHelpers
{
    // Rendering backend based on a specific platform.
    inline PXR_NS::TfToken GetRenderingBackend()
    {
#if defined(ENABLE_VULKAN) || defined(__ANDROID__)
        return PXR_NS::HgiTokens->Vulkan;
#elif defined(_WIN32) || defined(__linux__) 
        return PXR_NS::HgiTokens->OpenGL;
#elif defined(__APPLE__)
        return PXR_NS::HgiTokens->Metal;
#else
        return PXR_NS::HgiTokens->OpenGL;
#endif
    }
} // namespace TestHelpers

#define HVT_TEST(TestSuiteName, TestName)                                                           \
    void HVTTestDefaultBackend##TestName(                                                           \
        [[maybe_unused]] const std::string& computedImageName,                                      \
        [[maybe_unused]] const std::string& imageFile);                                             \
    TEST(TestSuiteName, TestName)                                                                   \
    {                                                                                               \
        TestHelpers::gTestNames.suiteName   = #TestSuiteName;                                       \
        TestHelpers::gTestNames.fixtureName = #TestName;                                            \
        TestHelpers::gTestNames.backendName = TestHelpers::GetRenderingBackend().GetString();       \
        const std::string imageFile = (std::filesystem::path(#TestSuiteName) / #TestName).string(); \
        const std::string computedImageName = imageFile;                                            \
        HVTTestDefaultBackend##TestName(computedImageName, imageFile);                              \
    }                                                                                               \
    void HVTTestDefaultBackend##TestName(                                                           \
        [[maybe_unused]] const std::string& computedImageName,                                      \
        [[maybe_unused]] const std::string& imageFile)

#define HVT_TEST_SKIP_BACKEND(backend)                                                             \
    if (TestHelpers::GetRenderingBackend() == backend)                                             \
    {                                                                                              \
        GTEST_SKIP() << "Skipping test for the " << backend.GetString() << " backend.";            \
    }

namespace TestHelpers
{
    struct TestNames
    {
        // The name of the test suite extracted from the test information.
        std::string suiteName;
        // The name of the test fixture extracted from the test suite name.
        std::string fixtureName;
        // The rendering backend name.
        std::string backendName;
    };

    inline TestNames gTestNames = TestNames {};

    // Gets the image file based on the rendering backend.
    inline std::string getComputedImagePath()
    {
        return gTestNames.fixtureName + "_" + gTestNames.backendName;
    }

    // Appends the rendering backend to the image file.
    inline std::string appendParamToImageFile(const std::string& fileName)
    {
        return fileName + "_" + gTestNames.backendName;
    }

} // namespace TestHelpers
