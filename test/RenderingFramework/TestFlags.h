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

#define HVT_TEST_DEFAULT_BACKEND(TestSuiteName, TestName)                                          \
    void HVTTestDefaultBackend##TestName();                                                        \
    TEST(TestSuiteName, TestName)                                                                  \
    {                                                                                              \
        TestHelpers::gParameterizedTests = false;                                                  \
        TestHelpers::gTestNames =                                                                  \
            TestHelpers::getTestNames(::testing::UnitTest::GetInstance()->current_test_info());    \
        HVTTestDefaultBackend##TestName();                                                         \
    }                                                                                              \
    void HVTTestDefaultBackend##TestName()

#if defined(ENABLE_VULKAN) && defined(WIN32)

    #define HVT_TEST(TestSuiteName, TestName)                                                        \
        std::string ParamTestName##TestName(const testing::TestParamInfo<std::string>& info)         \
        {                                                                                            \
            return info.param;                                                                       \
        }                                                                                            \
        class TestName : public ::testing::TestWithParam<std::string>                                \
        {                                                                                            \
        public:                                                                                      \
            void HVTTest##TestName(                                                                  \
                [[maybe_unused]] const std::string& computedImageName,                               \
                [[maybe_unused]] const std::string& imageFile);                                      \
        };                                                                                           \
        /* TODO: Enable "Vulkan" backend when Vulkan support is complete and stable.                 \
                   Currently, only "OpenGL" is enabled for testing. */                               \
        INSTANTIATE_TEST_SUITE_P(TestSuiteName, TestName, ::testing::Values(/*"Vulkan",*/ "OpenGL"), \
            ParamTestName##TestName);                                                                \
        TEST_P(TestName, TestName)                                                                   \
        {                                                                                            \
            TestHelpers::gRunVulkanTests     = (GetParam() == "Vulkan");                             \
            TestHelpers::gParameterizedTests = true;                                                 \
            TestHelpers::gTestNames          = TestHelpers::getTestNames(                            \
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

    #define HVT_TEST(TestSuiteName, TestName) HVT_TEST_DEFAULT_BACKEND(TestSuiteName, TestName)

#endif

namespace TestHelpers
{
    struct TestNames
    {
        std::string suiteName;
        std::string fixtureName;
        std::string paramName;
    };

    inline bool gRunVulkanTests = false;
    inline bool gParameterizedTests = false;
    inline TestNames gTestNames = TestNames {};

    inline TestNames getTestNames(const ::testing::TestInfo* testInfo)
    {
        TestNames testNames;
        if (testInfo)
        {
            std::string testSuiteName = testInfo->test_suite_name();
            std::string testName      = testInfo->name();

            // If parameterized tests are used, test_suite_name and name returns
            // SuiteName/TestName/Param. Hence the following filtering is
            // needed.
            if (TestHelpers::gParameterizedTests)
            {
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
            else // Otherwise the following is enough
            {
                testNames.suiteName   = testInfo->test_suite_name();
                testNames.fixtureName = testInfo->name();
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
