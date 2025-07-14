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

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif

#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/capabilities.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <gtest/gtest.h>

#include <hvt/testFramework/testContextCreator.h>

#include <hvt/testFramework/testGlobalFlags.h>

#if !defined(__APPLE__) && !defined(__ANDROID__)
#define OGL_TEST_ENABLED
#endif

//
// How to create an Hgi implementation?
//
TEST(howTo, createHgiImplementation)
{
    pxr::HgiUniquePtr hgi;
    pxr::HdDriver hgiDriver;

#ifdef OGL_TEST_ENABLED
    // A Window context is required to successfully create an OpenGL Hgi.
    // The GL Version is defined at the creation of this window context.
    auto context = hvt::TestFramework::CreateOpenGLTestContext();

    // Creates the platform default Hgi implementation e.g., OpenGL for Windows, Metal for macOS.

    {
        // Creates the platform default Hgi implementation and its associated driver instance.

#if defined(ADSK_OPENUSD_PENDING)
        hgi = pxr::Hgi::CreatePlatformDefaultHgi();
#elif defined(_WIN32)
        hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->OpenGL);
#elif defined(__APPLE__)
        hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->Metal);
#elif defined(__linux__)
        hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->OpenGL);
#else
        #error "The platform is not supported"
#endif

        hgiDriver.name   = pxr::HgiTokens->renderDriver;
        hgiDriver.driver = pxr::VtValue(hgi.get());

        // Some basic checks.

        ASSERT_TRUE(hgi->IsBackendSupported());

        // Destroys the Hgi implementation.

        hgi       = nullptr;
        hgiDriver = {};
    }

#endif

    // Explicitly creates a platform specific Hgi implementation.

    {
        // Creates the platform Hgi implementation and its associated driver instance.
#if defined(__ANDROID__)
        auto backendType = pxr::HgiTokens->Vulkan;
#elif defined(__APPLE__)
        auto backendType = pxr::HgiTokens->Metal;
#else
        auto backendType = pxr::HgiTokens->OpenGL;
#if defined(ENABLE_VULKAN)
        if (hvt::TestFramework::isRunningVulkan())
        {
            backendType = pxr::HgiTokens->Vulkan;
        }
#endif
#endif

        hgi              = pxr::Hgi::CreateNamedHgi(backendType);
        hgiDriver.name   = pxr::HgiTokens->renderDriver;
        hgiDriver.driver = pxr::VtValue(hgi.get());

        // Some basic checks.

        ASSERT_TRUE(hgi->IsBackendSupported());
        ASSERT_EQ(hgi->GetAPIName(), backendType);

        // Destroys the Hgi implementation.

        hgi       = nullptr;
        hgiDriver = {};
    }
}
