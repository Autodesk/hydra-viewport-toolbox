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

#include <filesystem>
#include <iostream>
#include <string>

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hgi/tokens.h>

namespace TestHelpers
{
/// \brief Selects the Hgi backend the tests render with.
///
/// Strips a \c --hgi=<OpenGL|Metal|Vulkan> argument from \p argv when present. The backend is
/// chosen by precedence: that flag, then the \c HVT_TEST_HGI environment variable, then the
/// OpenUSD platform default (see \c GetPlatformDefaultBackend). Names are case-insensitive.
/// \note Call it from main() before ::testing::InitGoogleTest(). When it is never called,
/// \c GetRenderingBackend() falls back to the environment variable and the platform default.
/// \throw std::invalid_argument when the requested name is not a known Hgi backend.
void InitRenderingBackend(int* argc, char** argv);

/// Returns the Hgi backend the tests render with (see \c InitRenderingBackend).
PXR_NS::TfToken const& GetRenderingBackend();

/// \brief Returns the backend \c pxr::Hgi::CreatePlatformDefaultHgi() would create.
/// \note This is the only place where the platform implies a graphics API. Everything else
/// must query \c GetRenderingBackend() or \c pxr::Hgi::GetAPIName().
PXR_NS::TfToken GetPlatformDefaultBackend();

/// Returns the suffix that identifies the current operating system in baseline image names
/// (i.e., "_osx", "_win" or "_linux").
std::string GetPlatformSuffix();

/// Returns the suffix that identifies \p backend in baseline image names (e.g., "_Metal").
std::string GetBackendSuffix(PXR_NS::TfToken const& backend);

/// \brief True when the \c HVT_TEST_RUN_SKIPPED environment variable is set (e.g., to 1).
/// The skip macros (\c HVT_TEST_SKIP_IF, \c HVT_TEST_SKIP_BACKEND) then run the tests anyway,
/// to see the current state of every known issue.
bool RunSkippedTests();

/// True when the tests are built for the corresponding operating system. Use these to skip a
/// test for an operating-system issue; use \c HVT_TEST_SKIP_BACKEND for a graphics API one.
#if defined(_WIN32)
inline constexpr bool kIsWindows = true;
#else
inline constexpr bool kIsWindows = false;
#endif
#if defined(__APPLE__)
inline constexpr bool kIsMacOS = true;
#else
inline constexpr bool kIsMacOS = false;
#endif
#if defined(__linux__)
inline constexpr bool kIsLinux = true;
#else
inline constexpr bool kIsLinux = false;
#endif
} // namespace TestHelpers

#define HVT_TEST(TestSuiteName, TestName)                                                          \
    void HVTTestDefaultBackend##TestName([[maybe_unused]] const std::string& computedImageName,    \
        [[maybe_unused]] const std::string& imageFile);                                            \
    TEST(TestSuiteName, TestName)                                                                  \
    {                                                                                              \
        TestHelpers::gTestNames.suiteName   = #TestSuiteName;                                      \
        TestHelpers::gTestNames.fixtureName = #TestName;                                           \
        TestHelpers::gTestNames.backendName = TestHelpers::GetRenderingBackend().GetString();      \
        const std::string imageFile =                                                              \
            (std::filesystem::path(#TestSuiteName) / #TestName).string();                          \
        const std::string computedImageName = imageFile;                                           \
        HVTTestDefaultBackend##TestName(computedImageName, imageFile);                             \
    }                                                                                              \
    void HVTTestDefaultBackend##TestName([[maybe_unused]] const std::string& computedImageName,    \
        [[maybe_unused]] const std::string& imageFile)

/// Skips the test when \p condition (e.g., \c TestHelpers::kIsLinux) holds, unless
/// \c TestHelpers::RunSkippedTests() is true: the test then runs and the reason is only logged.
#define HVT_TEST_SKIP_IF(condition, reason)                                                        \
    if (condition)                                                                                 \
    {                                                                                              \
        if (TestHelpers::RunSkippedTests())                                                        \
        {                                                                                          \
            std::cerr << "[ NO SKIP  ] " << reason << std::endl;                                   \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            GTEST_SKIP() << reason;                                                                \
        }                                                                                          \
    }

/// Skips the test for \p backend (e.g., \c pxr::HgiTokens->Metal), see \c HVT_TEST_SKIP_IF.
#define HVT_TEST_SKIP_BACKEND(backend)                                                             \
    HVT_TEST_SKIP_IF(TestHelpers::GetRenderingBackend() == backend,                                \
        "Skipping test for the " << backend.GetString() << " backend.")

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
