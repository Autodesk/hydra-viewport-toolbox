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

#include <RenderingFramework/TestHelpers.h>

#if _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4244)
#endif

#include <pxr/imaging/hgiVulkan/hgi.h>

#if _MSC_VER
#pragma warning(pop)
#endif

// Vulkan can indirectly include the Xlib.h header,
// which defines a ton of common words a macros.
// Guard against the conflicting and risk ones.
#undef Bool
#undef None
#undef Status
#undef True
#undef False

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

/// \brief Headless (offscreen) Vulkan renderer context.
///
/// The unit tests never present to a screen: the FramePass color AOV is read back through Hgi
/// and written to a PNG (see HydraRendererContext::captureColorTexture). Therefore, no window,
/// surface or swapchain is created, and no Vulkan API is called directly. Everything goes
/// through HgiVulkan, so the context works on machines without a window system.
class VulkanRendererContext : public HydraRendererContext
{
public:
    VulkanRendererContext(int width, int height);
    VulkanRendererContext(const VulkanRendererContext&)            = delete;
    VulkanRendererContext& operator=(const VulkanRendererContext&) = delete;
    ~VulkanRendererContext();

    void shutdown() override;
    void run(std::function<bool()> render, hvt::FramePass* framePass) override;
    void waitForGPUIdle() override;
};

/// \brief Helper to build a unit test.
/// \note Some unit tests from this unit test suite needs a fixture but others do not. So, a
/// google test fixture cannot be used. The following class is then used in place of the fixture
/// only when a unit test needs it.
class VulkanTestContext : public TestContext
{
public:
    VulkanTestContext();
    VulkanTestContext(int w, int h);
    ~VulkanTestContext() {};

private:
    /// Initialize the backend.
    void init() override;
};

} // namespace TestHelpers
