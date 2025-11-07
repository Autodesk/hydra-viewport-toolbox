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

#include <gtest/gtest.h>
#include <string>
#include <filesystem>

#include <pxr/imaging/hgi/tokens.h>
#include <pxr/base/tf/token.h>

namespace TestHelpers
{
    using RenderingBackend = PXR_NS::TfToken;

    // Default rendering backend based on platform.
    inline RenderingBackend GetDefaultRenderingBackend()
    {
#if defined(_WIN32) || defined(__linux__) 
        return PXR_NS::HgiTokens->OpenGL;
#elif defined(__APPLE__)
        return PXR_NS::HgiTokens->Metal;
#else
        return PXR_NS::HgiTokens->OpenGL;
#endif
    }
} // namespace TestHelpers

// Note: The public openUSD repo does not yet contain all the needed changes
// related to the Vulkan backend support.
#if defined(ENABLE_VULKAN) && defined(WIN32) && defined(ADSK_OPENUSD_PENDING)

#define HVT_TEST(TestSuiteName, TestName)                                                         \
    std::string ParamTestName##TestName(                                                          \
        const testing::TestParamInfo<TestHelpers::RenderingBackend>& info)                        \
    {                                                                                             \
        return info.param.GetString();                                                            \
    }                                                                                             \
    class TestName : public ::testing::TestWithParam<TestHelpers::RenderingBackend>               \
    {                                                                                             \
    public:                                                                                       \
        void HVTTest##TestName(                                                                   \
            [[maybe_unused]] const std::string& computedImageName,                                \
            [[maybe_unused]] const std::string& imageFile);                                       \
    };                                                                                            \
    INSTANTIATE_TEST_SUITE_P(TestSuiteName, TestName,                                             \
        ::testing::Values(PXR_NS::HgiTokens->Vulkan,                                              \
            PXR_NS::HgiTokens->OpenGL),                                                           \
        ParamTestName##TestName);                                                                 \
    TEST_P(TestName, TestName)                                                                    \
    {                                                                                             \
        TestHelpers::gRunVulkanTests = (GetParam() == PXR_NS::HgiTokens->Vulkan);                 \
        TestHelpers::gTestNames      = TestHelpers::getTestNames(                                 \
            ::testing::UnitTest::GetInstance()->current_test_info());                             \
        const std::string imageFile = (std::filesystem::path(TestHelpers::gTestNames.suiteName) / \
            TestHelpers::gTestNames.fixtureName).string();                                        \
        const std::string computedImageName = TestHelpers::appendParamToImageFile(imageFile);     \
        HVTTest##TestName(computedImageName, imageFile);                                          \
    }                                                                                             \
    void TestName::HVTTest##TestName(                                                             \
        [[maybe_unused]] const std::string& computedImageName,                                    \
        [[maybe_unused]] const std::string& imageFile)
#else

    #define GetParam() ([]() -> TestHelpers::RenderingBackend {                                         \
        return TestHelpers::GetDefaultRenderingBackend(); }())

    #define HVT_TEST(TestSuiteName, TestName)                                                           \
        void HVTTestDefaultBackend##TestName(                                                           \
            [[maybe_unused]] const std::string& computedImageName,                                      \
            [[maybe_unused]] const std::string& imageFile);                                             \
        TEST(TestSuiteName, TestName)                                                                   \
        {                                                                                               \
            TestHelpers::gTestNames.suiteName   = #TestSuiteName;                                       \
            TestHelpers::gTestNames.fixtureName = #TestName;                                            \
            TestHelpers::gTestNames.paramName   = "";                                                   \
            const std::string imageFile = (std::filesystem::path(#TestSuiteName) / #TestName).string(); \
            const std::string computedImageName = imageFile;                                            \
            HVTTestDefaultBackend##TestName(computedImageName, imageFile);                              \
        }                                                                                               \
        void HVTTestDefaultBackend##TestName(                                                           \
            [[maybe_unused]] const std::string& computedImageName,                                      \
            [[maybe_unused]] const std::string& imageFile)

#endif

namespace TestHelpers
{
    struct TestNames
    {
        /// @brief The name of the test suite extracted from the test information
        std::string suiteName;
        /// @brief The name of the test fixture extracted from the test suite name
        std::string fixtureName;
        /// @brief The parameter name extracted from the test name for parameterized tests
        std::string paramName;
    };

    inline bool gRunVulkanTests = false;
    inline TestNames gTestNames = TestNames {};

    inline TestNames getTestNames(const ::testing::TestInfo* testInfo)
    {
        TestNames testNames;
        if (testInfo)
        {
            std::string testSuiteName = testInfo->test_suite_name();
            std::string testName      = testInfo->name();

            size_t pos = testSuiteName.find('/');
            if (pos != std::string::npos)
            {
                testNames.suiteName   = testSuiteName.substr(0, pos);
                testNames.fixtureName = testSuiteName.substr(pos + 1);
            }

            pos = testName.find('/');
            if (pos != std::string::npos)
            {
                testNames.paramName = testName.substr(pos + 1);
            }
        }
        return testNames;
    }

    /// Gets the image file based on the test parameter.
    inline std::string getComputedImagePath()
    {
        return gTestNames.paramName.empty() ? gTestNames.fixtureName
                                            : (gTestNames.fixtureName + "_" + gTestNames.paramName);
    }

    /// Appends image file based on the test parameter.
    inline std::string appendParamToImageFile(const std::string& fileName)
    {
        return gTestNames.paramName.empty() ? fileName : (fileName + "_" + gTestNames.paramName);
    }

} // namespace TestHelpers
