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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-extension"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4244)
#endif

#include <pxr/base/gf/matrix4f.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/imaging/hgiVulkan/hgi.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

#include <vulkan/vulkan.h>

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

class VulkanRendererContext : public HydraRendererContext
{
public:
    VulkanRendererContext(int width, int height);
    VulkanRendererContext(const VulkanRendererContext&)        = delete;
    VulkanRendererContext& operator=(const VulkanRendererContext&) = delete;
    ~VulkanRendererContext();

    void init();
    void shutdown() override;
    bool saveImage(const std::string& fileName) override;
    void run(std::function<bool()> render, hvt::FramePass* framePass) override;
    void waitForGPUIdle() override;

private:
    pxr::HgiTextureHandle _dstTexture;
    pxr::HgiTextureViewHandle _dstTextureView;

    VkDevice _device;
    VkCommandPool _compositionCmdPool;
    VkCommandBuffer _compositionCmdBfr;

    void createCommandPool(VkCommandPool& cmdPool);
    void destroyCommandPool(const VkCommandPool& cmdPool);

    void queueWaitIdle();

    void createCommandBuffer(const VkCommandPool& cmdPool, VkCommandBuffer& cmdBuf);
    void beginCommandBuffer(const VkCommandBuffer& cmdBfr);

    void createTexture(pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view);
    void setLayoutBarrier(const VkCommandBuffer& cmdBfr, const VkImage& image,
        const VkImageLayout& oldLayout, const VkImageLayout& newLayout);
    void composite(hvt::FramePass* framePass);
};

/// \brief Helper to build a unit test.
/// \note Some unit tests from this unit test suite needs a fixture but others do not. So, a
/// google test fixture cannot be used. The following class is then used in place of the fixture
/// only when a unit test needs it.
class AndroidTestContext : public TestContext
{
public:
    AndroidTestContext();
    AndroidTestContext(int w, int h);
    ~AndroidTestContext() {};

private:
    /// Initialize the backend.
    void init() override;
};

} // namespace TestHelpers
