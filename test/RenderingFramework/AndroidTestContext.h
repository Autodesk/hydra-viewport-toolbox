//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//
#pragma once

#include <hvt/testFramework/testHelpers.h>

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
    void run(
        std::function<bool()> render, hvt::FramePass* framePass) override;

private:
    pxr::HgiTextureHandle _dstTexture;
    pxr::HgiTextureViewHandle _dstTextureView;

    VkDevice _device;
    VkCommandPool _compositionCmdPool;
    VkCommandBuffer _compositionCmdBfr;

    void createCommandPool(VkCommandPool& cmdPool);
    void destroyCommandPool(const VkCommandPool& cmdPool);

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
