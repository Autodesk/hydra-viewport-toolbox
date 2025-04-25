//
// Copyright 2024 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#endif

#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/capabilities.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

#include <gtest/gtest.h>

#if !defined(__APPLE__) && !defined(__ANDROID__)
#define OGL_TEST_ENABLED
#include <RenderingFramework/OpenGLTestContext.h>
#if defined(ENABLE_VULKAN)
#include <RenderingFramework/VulkanTestContext.h>
#endif
#endif

#include <RenderingFramework/TestFlags.h>

//
// How to create an Hgi implementation?
//
TEST(HowTo, CreateHgiImplementation)
{
    pxr::HgiUniquePtr hgi;
    pxr::HdDriver hgiDriver;

#ifdef OGL_TEST_ENABLED
    // A Window context is required to successfully create an OpenGL Hgi.
    // The GL Version is defined at the creation of this window context.
    TestHelpers::OpenGLWindow glWindow = TestHelpers::OpenGLWindow(640, 480);
    glWindow.makeContextCurrent();

    // Creates the platform default Hgi implementation e.g., OpenGL for Windows, Metal for macOS.

    {
        // Creates the platform default Hgi implementation and its associated driver instance.

        hgi              = pxr::Hgi::CreatePlatformDefaultHgi();
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
        if (TestHelpers::gRunVulkanTests)
            backendType = pxr::HgiTokens->Vulkan;
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
