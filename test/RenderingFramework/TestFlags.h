//
// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//
#pragma once

#include <gtest/gtest.h>
#include <string>

namespace TestHelpers
{
    enum class RenderingBackend
    {
        Vulkan,
        OpenGL
    };

    inline std::string renderingBackendToString(RenderingBackend backend)
    {
        switch (backend)
        {
            case RenderingBackend::Vulkan:
                return "Vulkan";
            case RenderingBackend::OpenGL:
                return "OpenGL";
            default:
                return "Unknown";
        }
    }
} // namespace TestHelpers

#if defined(ENABLE_VULKAN) && defined(WIN32)

#define HVT_TEST(TestSuiteName, TestName)                                                        \
    std::string ParamTestName##TestName(                                                         \
        const testing::TestParamInfo<TestHelpers::RenderingBackend>& info)                       \
    {                                                                                            \
        return TestHelpers::renderingBackendToString(info.param);                                \
    }                                                                                            \
    class TestName : public ::testing::TestWithParam<TestHelpers::RenderingBackend>              \
    {                                                                                            \
    public:                                                                                      \
        void HVTTest##TestName(                                                                  \
            [[maybe_unused]] const std::string& computedImageName,                               \
            [[maybe_unused]] const std::string& imageFile);                                      \
    };                                                                                           \
    INSTANTIATE_TEST_SUITE_P(TestSuiteName, TestName,                                            \
        ::testing::Values(TestHelpers::RenderingBackend::Vulkan,                                 \
            TestHelpers::RenderingBackend::OpenGL),                                              \
        ParamTestName##TestName);                                                                \
    TEST_P(TestName, TestName)                                                                   \
    {                                                                                            \
        TestHelpers::gRunVulkanTests = (GetParam() == TestHelpers::RenderingBackend::Vulkan);    \
        TestHelpers::gTestNames      = TestHelpers::getTestNames(                                \
            ::testing::UnitTest::GetInstance()->current_test_info());                            \
        const std::string imageFile = TestHelpers::gTestNames.suiteName +                        \
            std::string("/") + TestHelpers::gTestNames.fixtureName;                              \
        const std::string computedImageName = TestHelpers::appendParamToImageFile(imageFile);    \
        HVTTest##TestName(computedImageName, imageFile);                                         \
    }                                                                                            \
    void TestName::HVTTest##TestName(                                                            \
        [[maybe_unused]] const std::string& computedImageName,                                   \
        [[maybe_unused]] const std::string& imageFile)
#else

    #define GetParam() ([]() -> TestHelpers::RenderingBackend {                                  \
        return static_cast<TestHelpers::RenderingBackend>(-1); }())

    #define HVT_TEST(TestSuiteName, TestName)                                                    \
        void HVTTestDefaultBackend##TestName(                                                    \
            [[maybe_unused]] const std::string& computedImageName,                               \
            [[maybe_unused]] const std::string& imageFile);                                      \
        TEST(TestSuiteName, TestName)                                                            \
        {                                                                                        \
            TestHelpers::gTestNames.suiteName   = #TestSuiteName;                                \
            TestHelpers::gTestNames.fixtureName = #TestName;                                     \
            TestHelpers::gTestNames.paramName   = "";                                            \
            const std::string imageFile = std::string(#TestSuiteName) + "/" + #TestName;         \
            const std::string computedImageName = imageFile;                                     \
            HVTTestDefaultBackend##TestName(computedImageName, imageFile);                       \
        }                                                                                        \
        void HVTTestDefaultBackend##TestName(                                                    \
            [[maybe_unused]] const std::string& computedImageName,                               \
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
