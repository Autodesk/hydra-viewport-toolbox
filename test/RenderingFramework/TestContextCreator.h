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