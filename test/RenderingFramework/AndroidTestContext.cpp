//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include "AndroidTestContext.h"

#include <pxr/base/gf/frustum.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgiVulkan/commandBuffer.h>
#include <pxr/imaging/hgiVulkan/graphicsCmds.h>
#include <pxr/imaging/hgiVulkan/instance.h>
#include <pxr/imaging/hgiVulkan/texture.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/hgiInstance.h>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <filesystem>

#include <vulkan/vulkan_android.h>

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

VulkanRendererContext::VulkanRendererContext(int width, int height) :
    HydraRendererContext(width, height),
    _compositionCmdPool(VK_NULL_HANDLE),
    _compositionCmdBfr(VK_NULL_HANDLE)
{
    createHGI(pxr::HgiTokens->Vulkan);
    init();
}

VulkanRendererContext::~VulkanRendererContext()
{
    // TODO: These lines cause crash for Android UT. Comment them like VulkanTestContext.cpp.
    //    destroyHGI();
    //    shutdown();
}

void VulkanRendererContext::init()
{
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    _device                               = hgiVulkan->GetPrimaryDevice()->GetVulkanDevice();
    if (_device == VK_NULL_HANDLE)
        throw std::runtime_error("Vulkan device not found");

    createCommandPool(_compositionCmdPool);
    createCommandBuffer(_compositionCmdPool, _compositionCmdBfr);
}

void VulkanRendererContext::shutdown()
{
    destroyCommandPool(_compositionCmdPool);
}

void VulkanRendererContext::run(
    std::function<bool()> render, hvt::FramePass* framePass)
{
    bool moreFrames = true;
    while (moreFrames)
    {
        try
        {
            moreFrames = render();
            composite(framePass);
        }
        catch (const std::exception& ex)
        {
            throw std::runtime_error(
                std::string("Failed to render the frame pass: ") + ex.what() + ".");
        }
        catch (...)
        {
            throw std::runtime_error(
                std::string("Failed to render the frame pass: Unexpected error."));
        }
    }
}

bool VulkanRendererContext::saveImage(const std::string& fileName)
{
    static const std::filesystem::path filePath = hvt::TestFramework::getOutputDataFolder();
    const std::filesystem::path screenShotPath  = getFilename(filePath, fileName + "_computed");
    const std::filesystem::path directory       = screenShotPath.parent_path();
    if (!std::filesystem::exists(directory))
    {
        if (!std::filesystem::create_directories(directory))
        {
            throw std::runtime_error(
                std::string("Failed to create the directory: ") + directory.string());
        }
    }

    // Remove the previous saved image if exists.
    std::filesystem::remove(screenShotPath);

    const size_t byteSize =
        pxr::HgiGetDataSize(pxr::HgiFormatUNorm8Vec4, pxr::GfVec3i(width(), height(), 1));

    std::vector<uint8_t> texels(byteSize, 0);
    pxr::HgiTextureGpuToCpuOp readBackOp;
    readBackOp.cpuDestinationBuffer      = texels.data();
    readBackOp.destinationBufferByteSize = byteSize;
    readBackOp.destinationByteOffset     = 0;
    readBackOp.gpuSourceTexture          = _dstTexture;
    readBackOp.mipLevel                  = 0;
    readBackOp.sourceTexelOffset         = pxr::GfVec3i(0);

    pxr::HgiVulkan* hgiVulkan          = static_cast<pxr::HgiVulkan*>(_hgi.get());
    pxr::HgiBlitCmdsUniquePtr blitCmds = hgiVulkan->CreateBlitCmds();
    blitCmds->CopyTextureGpuToCpu(readBackOp);
    hgiVulkan->SubmitCmds(blitCmds.get(), pxr::HgiSubmitWaitTypeWaitUntilCompleted);

    return stbi_write_png(screenShotPath.string().c_str(), width(), height(), 4, texels.data(), 0);
}

void VulkanRendererContext::beginCommandBuffer(const VkCommandBuffer& cmdBfr)
{
    VkCommandBufferBeginInfo beginInfo {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmdBfr, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Begin CommandBuffer - vkBeginCommandBuffer failed");
}

void VulkanRendererContext::createCommandPool(VkCommandPool& cmdPool)
{
    VkCommandPoolCreateInfo commandPoolCreateInfo {};
    commandPoolCreateInfo       = VkCommandPoolCreateInfo {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    const uint32_t qfIndex                = hgiVulkan->GetPrimaryDevice()->GetGfxQueueFamilyIndex();
    commandPoolCreateInfo.queueFamilyIndex = qfIndex;

    if (vkCreateCommandPool(_device, &commandPoolCreateInfo, nullptr, &cmdPool) != VK_SUCCESS)
        throw std::runtime_error("Create Command Pool - vkCreateCommandPool failed");
}

void VulkanRendererContext::destroyCommandPool(const VkCommandPool& cmdPool)
{
    vkDestroyCommandPool(_device, cmdPool, nullptr);
}

void VulkanRendererContext::createCommandBuffer(
    const VkCommandPool& p_cmdPool, VkCommandBuffer& p_cmdBuf)
{
    VkCommandBufferAllocateInfo cmdBfrAllocInfo {};
    cmdBfrAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBfrAllocInfo.commandPool        = p_cmdPool;
    cmdBfrAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBfrAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(_device, &cmdBfrAllocInfo, &p_cmdBuf) != VK_SUCCESS)
        throw std::runtime_error("Create CommandBuffer - vkAllocateCommandBuffers failed");
}

void VulkanRendererContext::createTexture(
    pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view)
{
    pxr::HgiTextureDesc texDesc {};
    texDesc.componentMapping = pxr::HgiComponentMapping {
        pxr::HgiComponentSwizzleR,
        pxr::HgiComponentSwizzleG,
        pxr::HgiComponentSwizzleB,
        pxr::HgiComponentSwizzleA,
    };
    texDesc.dimensions     = pxr::GfVec3i(width(), height(), 1);
    texDesc.format         = pxr::HgiFormatUNorm8Vec4;
    texDesc.initialData    = nullptr;
    texDesc.layerCount     = 1;
    texDesc.mipLevels      = 1;
    texDesc.pixelsByteSize = 0;
    texDesc.sampleCount    = pxr::HgiSampleCount1;
    texDesc.type           = pxr::HgiTextureType2D;
    texDesc.usage          = pxr::HgiTextureUsageBitsColorTarget;
    texture                = _hgi->CreateTexture(texDesc);
    if (!texture)
        throw std::runtime_error("Image Creation - CreateTexture failed");

    pxr::HgiTextureViewDesc viewDesc {};
    viewDesc.format           = pxr::HgiFormatUNorm8Vec4;
    viewDesc.layerCount       = 1;
    viewDesc.mipLevels        = 1;
    viewDesc.sourceFirstLayer = 0;
    viewDesc.sourceFirstMip   = 0;
    viewDesc.sourceTexture    = texture;
    view                      = _hgi->CreateTextureView(viewDesc);
    if (!view)
        throw std::runtime_error("Image Creation - CreateTextureView failed");
}

void VulkanRendererContext::setLayoutBarrier(const VkCommandBuffer& cmdBfr, const VkImage& image,
    const VkImageLayout& oldLayout, const VkImageLayout& newLayout)
{
    VkImageMemoryBarrier imgMemBarrier {};
    imgMemBarrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imgMemBarrier.oldLayout                       = oldLayout;
    imgMemBarrier.newLayout                       = newLayout;
    imgMemBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    imgMemBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    imgMemBarrier.image                           = image;
    imgMemBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    imgMemBarrier.subresourceRange.baseArrayLayer = 0;
    imgMemBarrier.subresourceRange.levelCount     = 1;
    imgMemBarrier.subresourceRange.layerCount     = 1;
    imgMemBarrier.subresourceRange.baseMipLevel   = 0;
    imgMemBarrier.srcAccessMask                   = 0;
    imgMemBarrier.dstAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT;

    VkPipelineStageFlags src, dst;
    src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dst = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;

    vkCmdPipelineBarrier(cmdBfr, src, dst, 0, 0, nullptr, 0, nullptr, 1, &imgMemBarrier);
}

void VulkanRendererContext::composite(hvt::FramePass* framePass)
{
    createTexture(_dstTexture, _dstTextureView);
    pxr::HgiVulkanTexture* dstTex = static_cast<pxr::HgiVulkanTexture*>(_dstTexture.Get());

    auto colorTexHandle            = framePass->GetRenderTexture(pxr::HdAovTokens->color);
    pxr::HgiVulkanTexture* vkTex   = static_cast<pxr::HgiVulkanTexture*>(colorTexHandle.Get());
    VkImage inputColor             = vkTex->GetImage();
    VkImageLayout inputColorLayout = vkTex->GetImageLayout();

    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    PXR_INTERNAL_NS::HgiVulkanCommandQueue* hgiQueue =
        hgiVulkan->GetPrimaryDevice()->GetCommandQueue();
    if (!hgiQueue)
        throw std::runtime_error("Composite - HgiVulkanCommandQueue not found");

    VkQueue gfxQueue = hgiQueue->GetVulkanGraphicsQueue();

    beginCommandBuffer(_compositionCmdBfr);

    setLayoutBarrier(
        _compositionCmdBfr, inputColor, inputColorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImageLayout dstImageLayout = dstTex->GetImageLayout();
    setLayoutBarrier(_compositionCmdBfr, dstTex->GetImage(), dstTex->GetImageLayout(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageBlit imageBlit {};
    imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBlit.srcSubresource.layerCount = 1;
    imageBlit.srcSubresource.mipLevel   = 0;
    imageBlit.srcOffsets[1].x           = width();
    imageBlit.srcOffsets[1].y           = height();
    imageBlit.srcOffsets[1].z           = 1;
    imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBlit.dstSubresource.layerCount = 1;
    imageBlit.dstSubresource.mipLevel   = 0;
    imageBlit.dstOffsets[1].x           = width();
    imageBlit.dstOffsets[1].y           = height();
    imageBlit.dstOffsets[1].z           = 1;
    vkCmdBlitImage(_compositionCmdBfr, inputColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstTex->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

    setLayoutBarrier(_compositionCmdBfr, dstTex->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        dstImageLayout);
    setLayoutBarrier(
        _compositionCmdBfr, inputColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, inputColorLayout);

    vkEndCommandBuffer(_compositionCmdBfr);

    VkPipelineStageFlags waitBit = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };

    VkSubmitInfo submitInfo {};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &_compositionCmdBfr;
    submitInfo.pWaitDstStageMask  = &waitBit;

    VkFenceCreateInfo fenceCreateInfo {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
    VkFence fence;
    if (vkCreateFence(_device, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
        throw std::runtime_error("Create fence - vkCreateFence failed");
    if (vkQueueSubmit(gfxQueue, 1, &submitInfo, fence) != VK_SUCCESS)
        throw std::runtime_error("Submit CommandBuffer - vkQueueSubmit failed");
    vkWaitForFences(_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(_device, fence, nullptr);
}

AndroidTestContext::AndroidTestContext()
{
    init();
}

AndroidTestContext::AndroidTestContext(int w, int h) : TestContext(w, h)
{
    init();
}

void AndroidTestContext::init()
{
    std::string localAppPath = getenv("HVT_TEST_ASSETS");
    _sceneFilepath           = localAppPath + "/usd/test_fixed.usda";

    // Create the renderer context required for Hydra.
    _backend = std::make_shared<TestHelpers::VulkanRendererContext>(_width, _height);
    if (!_backend)
    {
        throw std::runtime_error("Failed to initialize the unit test backend!");
    }

    _backend->setDataPath(localAppPath);
}

} // namespace TestHelpers
