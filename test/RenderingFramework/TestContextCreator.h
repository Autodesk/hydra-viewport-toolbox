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

#include <RenderingFramework/OpenGLTestContext.h>
#include <RenderingFramework/TestFlags.h>

// The Vulkan context is only built when OpenUSD has HgiVulkan (HVT_ENABLE_VULKAN).
#ifdef HVT_ENABLE_VULKAN
    #include <RenderingFramework/VulkanTestContext.h>
#endif

#include <memory>
#include <stdexcept>

namespace TestHelpers
{
/// Throws when the selected backend has no test context in this build.
inline void ValidateRenderingBackend()
{
#ifndef HVT_ENABLE_VULKAN
    if (GetRenderingBackend() == PXR_NS::HgiTokens->Vulkan)
    {
        throw std::runtime_error(
            "The Vulkan backend was selected but HVT was built without HVT_ENABLE_VULKAN.");
    }
#endif
}

// The contexts are selected by the runtime backend (see GetRenderingBackend). The Vulkan
// context is headless; the OpenGL one owns an SDL window and also hosts every other backend
// (e.g., Metal), presenting through OpenGL interop.

inline std::shared_ptr<TestContext> CreateTestContext()
{
    ValidateRenderingBackend();
#ifdef HVT_ENABLE_VULKAN
    if (GetRenderingBackend() == PXR_NS::HgiTokens->Vulkan)
        return std::make_shared<TestHelpers::VulkanTestContext>();
#endif
    return std::make_shared<TestHelpers::OpenGLTestContext>();
}

inline std::shared_ptr<TestContext> CreateTestContext(int w, int h)
{
    ValidateRenderingBackend();
#ifdef HVT_ENABLE_VULKAN
    if (GetRenderingBackend() == PXR_NS::HgiTokens->Vulkan)
        return std::make_shared<TestHelpers::VulkanTestContext>(w, h);
#endif
    return std::make_shared<TestHelpers::OpenGLTestContext>(w, h);
}

inline std::shared_ptr<TestHelpers::HydraRendererContext> CreateRenderContext(int w, int h)
{
    ValidateRenderingBackend();
#ifdef HVT_ENABLE_VULKAN
    if (GetRenderingBackend() == PXR_NS::HgiTokens->Vulkan)
        return std::make_shared<TestHelpers::VulkanRendererContext>(w, h);
#endif
    return std::make_shared<TestHelpers::OpenGLRendererContext>(w, h);
}

} // namespace TestHelpers
