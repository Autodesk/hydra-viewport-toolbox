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

#include <RenderingFramework/TestFlags.h>

#if defined(__APPLE__)
#include "TargetConditionals.h" // For TARGET_OS_IPHONE
#endif

#if TARGET_OS_IPHONE == 1
#include <RenderingFramework/MetalTestContext.h>
#elif defined(__ANDROID__)
#include <RenderingFramework/AndroidTestContext.h>
#else
#include <RenderingFramework/OpenGLTestContext.h>
#ifdef ENABLE_VULKAN
#include <RenderingFramework/VulkanTestContext.h>
#endif
#endif

namespace TestHelpers
{
    inline
	std::shared_ptr<TestContext> CreateTestContext()
    {
#if TARGET_OS_IPHONE
        return std::make_shared<TestHelpers::MetalTestContext>();
#elif defined(__ANDROID__)
        return std::make_shared<TestHelpers::AndroidTestContext>();
#else

        // Handle Vulkan and OpenGL fall-back together.
#ifdef ENABLE_VULKAN
        if (gRunVulkanTests)
            return std::make_shared<TestHelpers::VulkanTestContext>();
#endif
        return std::make_shared<TestHelpers::OpenGLTestContext>();
#endif
    }

    inline
	std::shared_ptr<TestContext> CreateTestContext(int w, int h)
    {
#if TARGET_OS_IPHONE
        return std::make_shared<TestHelpers::MetalTestContext>(w, h);
#elif defined(__ANDROID__)
        return std::make_shared<TestHelpers::AndroidTestContext>(w, h);
#else

        // Handle Vulkan and OpenGL fall-back together.
#ifdef ENABLE_VULKAN
        if (gRunVulkanTests)
            return std::make_shared<TestHelpers::VulkanTestContext>(w, h);
#endif
        return std::make_shared<TestHelpers::OpenGLTestContext>(w, h);
#endif
    }

    inline 
    std::shared_ptr<TestHelpers::HydraRendererContext> CreateRenderContext(
        int w, int h)
    {
#if TARGET_OS_IPHONE
        return std::make_shared<TestHelpers::MetalRendererContext>(w, h);
#elif defined(__ANDROID__)
        // Included from AndroidTestContext.h
        return std::make_shared<TestHelpers::VulkanRendererContext>(w, h);
#else

        // Handle Vulkan and OpenGL fall-back together.
#ifdef ENABLE_VULKAN
        if (gRunVulkanTests)
            return std::make_shared<TestHelpers::VulkanRendererContext>(w, h);
#endif
        return std::make_shared<TestHelpers::OpenGLRendererContext>(w, h);
#endif
    }

} // namespace TestHelpers