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

#include <RenderingFramework/OpenGLTestContext.h>
#include <RenderingFramework/TestFlags.h>

//
// How to create an Hgi implementation?
//
HVT_TEST(howTo, CreateHgiImplementation)
{
    pxr::HgiUniquePtr hgi;
    pxr::HdDriver hgiDriver;

    // A window context is required to successfully create an OpenGL Hgi, and the GL version is
    // defined at the creation of this window context. The other backends do not need it.
    TestHelpers::OpenGLWindow glWindow = TestHelpers::OpenGLWindow(640, 480);
    glWindow.makeContextCurrent();

    // Creates the platform default Hgi implementation e.g., OpenGL for Windows, Metal for macOS,
    // or Vulkan when the HGI_ENABLE_VULKAN environment variable is set.

    {
        // Creates the platform default Hgi implementation and its associated driver instance.

        hgi = pxr::Hgi::CreatePlatformDefaultHgi();

        hgiDriver.name   = pxr::HgiTokens->renderDriver;
        hgiDriver.driver = pxr::VtValue(hgi.get());

        // Some basic checks.

        ASSERT_TRUE(hgi);
        ASSERT_TRUE(hgi->IsBackendSupported());
        ASSERT_EQ(hgi->GetAPIName(), TestHelpers::GetPlatformDefaultBackend());

        // Destroys the Hgi implementation.

        hgi       = nullptr;
        hgiDriver = {};
    }

    // Explicitly creates a named Hgi implementation, here the one the tests render with.

    {
        // Creates the named Hgi implementation and its associated driver instance.
        auto backendType = TestHelpers::GetRenderingBackend();

        hgi              = pxr::Hgi::CreateNamedHgi(backendType);
        hgiDriver.name   = pxr::HgiTokens->renderDriver;
        hgiDriver.driver = pxr::VtValue(hgi.get());

        // Some basic checks.

        ASSERT_TRUE(hgi);
        ASSERT_TRUE(hgi->IsBackendSupported());
        ASSERT_EQ(hgi->GetAPIName(), backendType);

        // Destroys the Hgi implementation.

        hgi       = nullptr;
        hgiDriver = {};
    }
}
