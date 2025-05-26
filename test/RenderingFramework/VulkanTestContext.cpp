//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include "VulkanTestContext.h"

#if _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4244)
#endif

#include <pxr/base/gf/frustum.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgiVulkan/blitCmds.h>
#include <pxr/imaging/hgiVulkan/commandBuffer.h>
#include <pxr/imaging/hgiVulkan/graphicsCmds.h>
#include <pxr/imaging/hgiVulkan/instance.h>
#include <pxr/imaging/hgiVulkan/texture.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/hgiInstance.h>

#if _MSC_VER
#pragma warning(pop)
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <filesystem>

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#elif defined(_linux_)
#include <vulkan/vulkan_xlib.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

VulkanRendererContext::VulkanRendererContext(int width, int height) :
    HydraRendererContext(width, height),
    _surface(VK_NULL_HANDLE),
    _swapChain(VK_NULL_HANDLE),
    _swapchainImageList(),
    _currentSwapChainId(0),
    _acquireSwapchainSemaphore(VK_NULL_HANDLE),
    _renderingCompleteSemaphore(VK_NULL_HANDLE),
    _copyToSwapChainCompleteSemaphore(VK_NULL_HANDLE),
    _compositionCmdPool(VK_NULL_HANDLE),
    _compositionCmdBfr(VK_NULL_HANDLE)
{
    // This flag translates to use of Present Task inside USD pipeline.
    // If the presentation task is enabled, interop-present task get involved.
    // Which for the Vulkan backend involves copying a Vulkan image to OpenGL
    // before presenting to an OpenGL context. This is against the intended
    // design of our-case. We wish to explicitly present to a pure Vulkan
    // implementation, which Vulkan Renderer Context takes care of.
    _presentationEnabled = false;

    _shaderConstants.modelMatrix.SetIdentity();
    _shaderConstants.viewMatrix.SetIdentity();
    _shaderConstants.projectionMatrix.SetIdentity();

    createHGI(pxr::TfToken("Vulkan"));
    init();
}

VulkanRendererContext::~VulkanRendererContext()
{
    destroyHGI();
}

void VulkanRendererContext::init()
{
    _mSDLWWindow = SDL_CreateWindow("Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width(),
        height(), SDL_WINDOW_SHOWN);
    if (!_mSDLWWindow)
        throw std::runtime_error("Creation of SDL Window Failed");

    // Get queue family index
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    uint32_t qfIndex                      = hgiVulkan->GetPrimaryDevice()->GetGfxQueueFamilyIndex();

    CreateSurface(_mSDLWWindow);
    CreateSwapchain((uint32_t)width(), (uint32_t)height());
    CreateSwapchainImages();
    CreateCommandPool(qfIndex, _compositionCmdPool);
    CreateCommandBuffer(_compositionCmdPool, _compositionCmdBfr);
    CreateASemaphore(_acquireSwapchainSemaphore);
    CreateASemaphore(_copyToSwapChainCompleteSemaphore);
    CreateSampler();
    InitCamera();
}

void VulkanRendererContext::shutdown()
{
    DestroySampler();
    DestroyASemaphore(_copyToSwapChainCompleteSemaphore);
    DestroyASemaphore(_acquireSwapchainSemaphore);
    DestroyCommandPool(_compositionCmdPool);
    DestroySwapchainImages();
    DestroySurface();

    if (_mSDLWWindow)
    {
        SDL_DestroyWindow(_mSDLWWindow);
        _mSDLWWindow = nullptr;
        SDL_Quit();
    }
}

void VulkanRendererContext::CreateShaderHandle(ShaderProgDesc& shaderProgDesc)
{
    shaderProgDesc.vertShaderFunc = _hgi->CreateShaderFunction(shaderProgDesc.vertShaderDesc);
    if (!shaderProgDesc.vertShaderFunc->GetCompileErrors().empty())
        throw std::runtime_error("Shader creation - CreateShaderFunction vertex shader of " +
            shaderProgDesc.debugName.GetString() + "failed");

    shaderProgDesc.fragShaderFunc = _hgi->CreateShaderFunction(shaderProgDesc.fragShaderDesc);
    if (!shaderProgDesc.fragShaderFunc->GetCompileErrors().empty())
        throw std::runtime_error("Shader creation - CreateShaderFunction fragment shader of " +
            shaderProgDesc.debugName.GetString() + "failed");

    pxr::HgiShaderProgramDesc programDesc;
    programDesc.debugName = shaderProgDesc.debugName.GetString();
    programDesc.shaderFunctions.push_back(std::move(shaderProgDesc.vertShaderFunc));
    programDesc.shaderFunctions.push_back(std::move(shaderProgDesc.fragShaderFunc));
    shaderProgDesc.shaderProg = _hgi->CreateShaderProgram(programDesc);
    if (!shaderProgDesc.shaderProg->GetCompileErrors().empty())
        throw std::runtime_error("Shader creation - CreateShaderProgram of " +
            shaderProgDesc.debugName.GetString() + "failed");
}

void VulkanRendererContext::DestroyShaderHandle(ShaderProgDesc& desc)
{
    _hgi->DestroyShaderProgram(&desc.shaderProg);
    _hgi->DestroyShaderFunction(&desc.vertShaderFunc);
    _hgi->DestroyShaderFunction(&desc.fragShaderFunc);
}

// Begin of frame
void VulkanRendererContext::beginVk()
{
    AcquireNextSwapchain(_currentSwapChainId);
}

// End of frame
void VulkanRendererContext::endVk()
{
    VkSemaphoreList waitsemaphore { _copyToSwapChainCompleteSemaphore };
    Present(waitsemaphore);
}

void VulkanRendererContext::CreateASemaphore(VkSemaphore& semaphore)
{
    VkDevice device = GetVulkanDevice();
    VkSemaphoreCreateInfo semaphoreCreateInfo {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore) != VK_SUCCESS)
        throw std::runtime_error("Semaphore Creation - vkCreateSemaphore failed");
}

void VulkanRendererContext::DestroyASemaphore(VkSemaphore& semaphore)
{
    VkDevice device = GetVulkanDevice();
    vkDestroySemaphore(device, semaphore, nullptr);
}

void VulkanRendererContext::CreateSurface(SDL_Window* window)
{
    SDL_SysWMinfo info {};

    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info))
        throw std::runtime_error("Surface Creation - SDL_GetWindowWMInfo failed");

    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    VkInstance instance                   = hgiVulkan->GetVulkanInstance()->GetVulkanInstance();

    if (instance == VK_NULL_HANDLE)
        throw std::runtime_error("Surface Creation - Vulkan instance not found");

#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR createInfo {};
    createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = info.info.win.hinstance;
    createInfo.hwnd      = info.info.win.window;
    if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &_surface) != VK_SUCCESS)
        throw std::runtime_error("Surface Creation - vkCreateWin32SurfaceKHR failed");

#elif ANDROID
#endif
}

void VulkanRendererContext::DestroySurface()
{
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    VkInstance instance                   = hgiVulkan->GetVulkanInstance()->GetVulkanInstance();

    if (instance == VK_NULL_HANDLE)
        throw std::runtime_error("Surface Destruction - Vulkan instance not found");

    vkDestroySurfaceKHR(instance, _surface, nullptr);
}

void VulkanRendererContext::CreateSwapchain(uint32_t w, uint32_t h)
{
    // Brute forcing swapchain count to 2 without querying for capabilities
    // doing this by setting minImageCount = FRAME_BUFFER_COUNT
    // - Double buffering works on both desktop and mobile hardware,
    // so, can skip querying for support on this for now
    // Forcing presentation mode to FIFO without querying for support
    // - Since FIFO should be available on all Vulkan supported Devices
    // - FIFO disables VSync and ensures the swapchain image is available
    // as soon as the presentation engine swaps the image
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkSwapchainCreateInfoKHR swapChainCreateInfo {};
    swapChainCreateInfo.sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.pNext              = nullptr;
    swapChainCreateInfo.surface            = _surface;
    swapChainCreateInfo.minImageCount      = FRAME_BUFFER_COUNT;
    swapChainCreateInfo.imageFormat        = VK_FORMAT_B8G8R8A8_UNORM;
    swapChainCreateInfo.imageColorSpace    = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapChainCreateInfo.imageExtent.height = h;
    swapChainCreateInfo.imageExtent.width  = w;
    swapChainCreateInfo.imageArrayLayers   = 1;
    swapChainCreateInfo.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapChainCreateInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    swapChainCreateInfo.queueFamilyIndexCount = 0;
    swapChainCreateInfo.pQueueFamilyIndices   = nullptr;
    swapChainCreateInfo.preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapChainCreateInfo.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainCreateInfo.presentMode           = presentMode;
    swapChainCreateInfo.clipped               = VK_TRUE;
    swapChainCreateInfo.oldSwapchain          = VK_NULL_HANDLE;

    VkDevice device = GetVulkanDevice();
    if (vkCreateSwapchainKHR(device, &swapChainCreateInfo, nullptr, &_swapChain) != VK_SUCCESS)
        throw std::runtime_error("Swapchain Creation - vkCreateSwapchainKHR failed");
}

void VulkanRendererContext::CreateSwapchainImages()
{
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    VkDevice device                       = hgiVulkan->GetPrimaryDevice()->GetVulkanDevice();

    uint32_t scCount = FRAME_BUFFER_COUNT;
    if (vkGetSwapchainImagesKHR(device, _swapChain, &scCount, _swapchainImageList) != VK_SUCCESS)
        throw std::runtime_error("Swapchain Image Creation - vkGetSwapchainImagesKHR failed");

    for (int it = 0; it != FRAME_BUFFER_COUNT; ++it)
    {
        VkImageViewCreateInfo l_vkSwapChainImageViewInfo {};
        l_vkSwapChainImageViewInfo.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        l_vkSwapChainImageViewInfo.pNext        = nullptr;
        l_vkSwapChainImageViewInfo.image        = _swapchainImageList[it];
        l_vkSwapChainImageViewInfo.viewType     = VK_IMAGE_VIEW_TYPE_2D;
        l_vkSwapChainImageViewInfo.format       = VK_FORMAT_B8G8R8A8_UNORM;
        l_vkSwapChainImageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        l_vkSwapChainImageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        l_vkSwapChainImageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        l_vkSwapChainImageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        l_vkSwapChainImageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        l_vkSwapChainImageViewInfo.subresourceRange.baseMipLevel   = 0;
        l_vkSwapChainImageViewInfo.subresourceRange.levelCount     = 1;
        l_vkSwapChainImageViewInfo.subresourceRange.baseArrayLayer = 0;
        l_vkSwapChainImageViewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(device, &l_vkSwapChainImageViewInfo, nullptr,
                &_swapchainImageViewList[it]) != VK_SUCCESS)
            throw std::runtime_error("Swapchain Image View Creation - vkCreateImageView failed");

        _swapchainLayout[it] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

void VulkanRendererContext::DestroySwapchainImages()
{
    // Note: Do not explicitly destroy the swapchain images.
    // calling vkDestroySwapchainKHR should delete the swapchain
    // and swapchain images
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    VkDevice device                       = hgiVulkan->GetPrimaryDevice()->GetVulkanDevice();

    // destroying image views
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
    {
        vkDestroyImageView(device, _swapchainImageViewList[i], nullptr);
    }

    if (device == VK_NULL_HANDLE)
        throw std::runtime_error("Swapchain Destruction - Vulkan Device not found");

    vkDestroySwapchainKHR(device, _swapChain, nullptr);
}

void VulkanRendererContext::run(std::function<bool()> render,
    hvt::FramePass* framePass [[maybe_unused]])
{
    SDL_Event event;
    bool moreFrames = true;
    while (moreFrames)
    {
        SDL_PollEvent(&event);
        if (event.type == SDL_QUIT)
        {
            return;
        }

        beginVk();

        moreFrames = render();
        Composite(framePass);

        endVk();
    }
}

bool VulkanRendererContext::saveImage(const std::string& fileName)
{
    static const std::filesystem::path filePath = TestHelpers::getOutputDataFolder();

    const std::filesystem::path screenShotPath = getFilename(filePath, fileName + "_computed");
    const std::filesystem::path directory      = screenShotPath.parent_path();
    if (!std::filesystem::exists(directory))
    {
        if (!std::filesystem::create_directories(directory))
        {
            throw std::runtime_error(
                std::string("Failed to create the directory: ") + directory.string());
        }
    }

    const auto byteSize =
        pxr::HgiGetDataSize(pxr::HgiFormatUNorm8Vec4, pxr::GfVec3i(width(), height(), 1));

    pxr::HgiTextureDesc desc {};
    desc.debugName      = "Save Pixel Texture";
    desc.dimensions     = pxr::GfVec3i(width(), height(), 1);
    desc.usage          = pxr::HgiTextureUsageBitsColorTarget | pxr::HgiTextureUsageBitsShaderRead;
    desc.type           = pxr::HgiTextureType2D;
    desc.layerCount     = 1;
    desc.format         = pxr::HgiFormatUNorm8Vec4;
    desc.mipLevels      = 1;
    desc.initialData    = nullptr;
    desc.pixelsByteSize = byteSize;

    auto texture = _hgi->CreateTexture(desc);

    auto vkTexturePtr        = static_cast<pxr::HgiVulkanTexture*>(texture.Get());
    VkImageLayout prevLayout = vkTexturePtr->GetImageLayout();
    VkImage textureImage     = vkTexturePtr->GetImage();

    // Next step, we need to get the command buffers for actually submitting
    pxr::HgiBlitCmdsUniquePtr blitCmds = _hgi->CreateBlitCmds();
    blitCmds->PushDebugGroup("Save Pixels");

    auto vkBlitCmdsPtr       = static_cast<pxr::HgiVulkanBlitCmds*>(blitCmds.get());
    VkCommandBuffer vkCmdBuf = vkBlitCmdsPtr->GetCommandBuffer()->GetVulkanCommandBuffer();

    SetLayoutBarrier(vkCmdBuf, textureImage, prevLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    SetLayoutBarrier(vkCmdBuf, _swapchainImageList[_currentSwapChainId],
        _swapchainLayout[_currentSwapChainId], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    _swapchainLayout[_currentSwapChainId] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    BlitColorToImage(vkCmdBuf, _swapchainImageList[_currentSwapChainId], { width(), height(), 1 },
        textureImage, { width(), height(), 1 });
    // Restore the texture back to its original form
    SetLayoutBarrier(vkCmdBuf, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, prevLayout);

    std::vector<uint8_t> texels(byteSize, 0);
    pxr::HgiTextureGpuToCpuOp readBackOp {};
    readBackOp.cpuDestinationBuffer      = texels.data();
    readBackOp.destinationBufferByteSize = byteSize;
    readBackOp.destinationByteOffset     = 0;
    readBackOp.gpuSourceTexture          = texture;
    readBackOp.mipLevel                  = 0;
    readBackOp.sourceTexelOffset         = pxr::GfVec3i(0);
    blitCmds->CopyTextureGpuToCpu(readBackOp);

    blitCmds->PopDebugGroup();

    // Hopefully this works because we are single threaded and commands are
    // executing in sequence on the same queue. Here we hijack a blitCmdBuffer
    // from hgi to copy swapchain image to this screenshot image. This command
    // should wait until last command (which is the composite command) to finish
    _hgi->SubmitCmds(blitCmds.get(), pxr::HgiSubmitWaitType::HgiSubmitWaitTypeWaitUntilCompleted);
    _hgi->DestroyTexture(&texture);

    blitCmds.reset();
    std::error_code non_exist;
    std::filesystem::remove(screenShotPath, non_exist);
    return stbi_write_png(screenShotPath.string().c_str(), width(), height(), 4, texels.data(), 0);
}

void VulkanRendererContext::SetFinalColorImage(const pxr::HgiTextureHandle& image)
{
    _finalColorTarget          = image;
    _compositeWithoutFramePass = true;
}

void VulkanRendererContext::SetRenderCompleteSemaphore(const VkSemaphore& semaphore)
{
    _renderingCompleteSemaphore = semaphore;
}

void VulkanRendererContext::CreateGfxCmdHandle(
    const pxr::HgiGraphicsCmdsDesc& gfxCmdDesc, pxr::HgiGraphicsCmdsUniquePtr& handle)
{
    handle = _hgi->CreateGraphicsCmds(gfxCmdDesc);
    if (!handle)
        throw std::runtime_error("Gfx Command Creation - CreateGraphicsCmds failed");
}

void VulkanRendererContext::CreateTexture(const pxr::HgiTextureUsage& usage,
    const pxr::HgiFormat& format, pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view)
{
    // Create color Render Target
    pxr::HgiTextureDesc texDesc {};
    texDesc.componentMapping = pxr::HgiComponentMapping {
        pxr::HgiComponentSwizzleR,
        pxr::HgiComponentSwizzleG,
        pxr::HgiComponentSwizzleB,
        pxr::HgiComponentSwizzleA,
    };
    texDesc.debugName      = "Color Buffer";
    texDesc.dimensions     = pxr::GfVec3i(width(), height(), 1);
    texDesc.format         = format;
    texDesc.initialData    = nullptr;
    texDesc.layerCount     = 1;
    texDesc.mipLevels      = 1;
    texDesc.pixelsByteSize = 0;
    texDesc.sampleCount    = pxr::HgiSampleCount1;
    texDesc.type           = pxr::HgiTextureType2D;
    texDesc.usage          = usage;
    texture                = _hgi->CreateTexture(texDesc);
    if (!texture)
        throw std::runtime_error("Image Creation - CreateTexture failed");

    // create color view
    pxr::HgiTextureViewDesc viewDesc {};
    viewDesc.debugName        = "Color Buffer View";
    viewDesc.format           = format;
    viewDesc.layerCount       = 1;
    viewDesc.mipLevels        = 1;
    viewDesc.sourceFirstLayer = 0;
    viewDesc.sourceFirstMip   = 0;
    viewDesc.sourceTexture    = texture;
    view                      = _hgi->CreateTextureView(viewDesc);
    if (!view)
        throw std::runtime_error("Image Creation - CreateTextureView failed");
}

void VulkanRendererContext::CreateTexture(const pxr::HgiTextureDesc& texDesc,
    pxr::HgiTextureViewDesc& viewDesc, pxr::HgiTextureHandle& texture,
    pxr::HgiTextureViewHandle& view)
{
    texture = _hgi->CreateTexture(texDesc);
    if (!texture)
        throw std::runtime_error("Image Creation - CreateTexture failed");

    viewDesc.sourceTexture = texture;
    view                   = _hgi->CreateTextureView(viewDesc);
    if (!view)
        throw std::runtime_error("Image Creation - CreateTextureView failed");
}

void VulkanRendererContext::DestroyTexture(
    pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view)
{
    _hgi->DestroyTextureView(&view);
    _hgi->DestroyTexture(&texture);
}

void VulkanRendererContext::CreateBuffer(
    const pxr::HgiBufferDesc& bufDesc, pxr::HgiBufferHandle& buffer)
{
    buffer = _hgi->CreateBuffer(bufDesc);
    if (!buffer)
        throw std::runtime_error("Buffer Creation - CreateBuffer failed" + bufDesc.debugName);
}

void VulkanRendererContext::DestroyBuffer(pxr::HgiBufferHandle& buffer)
{
    _hgi->DestroyBuffer(&buffer);
}

void VulkanRendererContext::CreateGfxPipeline(
    const pxr::HgiGraphicsPipelineDesc& pipelineDesc, pxr::HgiGraphicsPipelineHandle& pipeline)
{
    pipeline = _hgi->CreateGraphicsPipeline(pipelineDesc);
    if (!pipeline)
        throw std::runtime_error(
            "CreateGfxPipeline - CreateGraphicsPipeline failed - " + pipelineDesc.debugName);
}

void VulkanRendererContext::DestroyGfxPipeline(pxr::HgiGraphicsPipelineHandle& pipeline)
{
    _hgi->DestroyGraphicsPipeline(&pipeline);
}

void VulkanRendererContext::Submit(pxr::HgiCmds* cmds, const pxr::HgiSubmitWaitType& wait)
{
    SetRenderCompleteSemaphore(
        static_cast<pxr::HgiVulkanGraphicsCmds*>(cmds)->GetCommandBuffer()->GetVulkanSemaphore());
    _hgi->SubmitCmds(cmds, wait);
}

void VulkanRendererContext::AcquireNextSwapchain(uint32_t& swapChainID)
{
    VkDevice device = GetVulkanDevice();
    if (vkAcquireNextImageKHR(device, _swapChain, UINT64_MAX, _acquireSwapchainSemaphore,
            VK_NULL_HANDLE, &swapChainID) != VK_SUCCESS)
        throw std::runtime_error("Acquire Next Swapchain - vkAcquireNextImageKHR failed");
}

void VulkanRendererContext::BeginCommandBuffer(const VkCommandBuffer& cmdBfr)
{
    VkCommandBufferBeginInfo beginInfo {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmdBfr, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Begin CommandBuffer - vkBeginCommandBuffer failed");
}

void VulkanRendererContext::Submit(const VkCommandBuffer& cmdBfr, const VkQueue& queue,
    const VkSemaphoreList& waitSemaphores, const VkSemaphoreList& signalSemaphores)
{
    vkEndCommandBuffer(cmdBfr);

    VkPipelineStageFlags waitBit = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };

    VkSubmitInfo submitInfo {};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmdBfr;
    submitInfo.pSignalSemaphores    = signalSemaphores.data();
    submitInfo.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
    submitInfo.pWaitSemaphores      = waitSemaphores.data();
    submitInfo.waitSemaphoreCount   = (uint32_t)waitSemaphores.size();
    submitInfo.pWaitDstStageMask    = &waitBit;

    if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Submit CommandBuffer - vkQueueSubmit failed");
}

void VulkanRendererContext::SetLayoutBarrier(const VkCommandBuffer& cmdBfr, const VkImage& image,
    const VkImageLayout& oldLayout, const VkImageLayout& newLayout)
{
    VkImageMemoryBarrier imgMemBarrier {};
    imgMemBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // Following flags used to transfer one image type to other
    imgMemBarrier.oldLayout = oldLayout;
    imgMemBarrier.newLayout = newLayout;
    // If you wish to transfer the queue family ownership, use the following flags
    imgMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // for now we don't care
    imgMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // for now we don't care
    // Setting affected image and specified part
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

void VulkanRendererContext::CopyColorToSwapChain(
    const VkCommandBuffer& p_cmdbfr, const VkImage& inputColor, const uint32_t& p_swapChainIndex)
{
    VkImageCopy copydes {};
    copydes.dstOffset      = VkOffset3D { 0, 0, 0 };
    copydes.dstSubresource = VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copydes.extent         = VkExtent3D { (uint32_t)width(), (uint32_t)height(), 1 };
    copydes.srcOffset      = VkOffset3D { 0, 0, 0 };
    copydes.srcSubresource = VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    vkCmdCopyImage(p_cmdbfr, inputColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        _swapchainImageList[p_swapChainIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copydes);
}

void VulkanRendererContext::BlitColorToImage(const VkCommandBuffer& cmdbfr, const VkImage& src,
    const VkOffset3D& srcOffset, VkImage dst, const VkOffset3D& dstOffset)
{
    // Blit commands supports color channel conversion
    VkImageBlit imageBlit {};
    imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBlit.srcSubresource.layerCount = 1;
    imageBlit.srcSubresource.mipLevel   = 0;
    imageBlit.srcOffsets[1]             = srcOffset;

    imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBlit.dstSubresource.layerCount = 1;
    imageBlit.dstSubresource.mipLevel   = 0;
    imageBlit.dstOffsets[1]             = dstOffset;

    vkCmdBlitImage(cmdbfr, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);
}

void VulkanRendererContext::BlitColorToSwapChain(const VkCommandBuffer& cmdbfr,
    const VkImage& inputColor, const pxr::GfVec4d& rect, const uint32_t& swapChainIndex)
{
    VkRect2D viewport {
        { (int32_t)rect[0], (int32_t)rect[1] },
        { (uint32_t)rect[2], (uint32_t)rect[3] },
    };

    VkImageBlit imageBlit {};
    // VkCmdBlitImage copies the srcOffsets[0,1] to dstOffsets[0,1]. We deal
    // with 2 flips here:
    //
    // 1. The image itself is y-flipped, so srcOffsets flip with _renderHeight -
    // viewport.offset.y
    //
    // 2. the viewport coordinates is HdxRenderTaskParams which is in
    // a y-down coordinate system (so the dstOffsets does not need to change).
    imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBlit.srcSubresource.layerCount = 1;
    imageBlit.srcSubresource.mipLevel   = 0;
    imageBlit.srcOffsets[0].x           = viewport.offset.x;
    imageBlit.srcOffsets[1].x           = viewport.offset.x + viewport.extent.width;
    imageBlit.srcOffsets[0].y           = height() - viewport.offset.y;
    imageBlit.srcOffsets[1].y           = height() - (viewport.offset.y + viewport.extent.height);
    imageBlit.srcOffsets[0].z           = 0;
    imageBlit.srcOffsets[1].z           = 1;
    imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBlit.dstSubresource.layerCount = 1;
    imageBlit.dstSubresource.mipLevel   = 0;
    imageBlit.dstOffsets[0].x           = viewport.offset.x;
    imageBlit.dstOffsets[1].x           = viewport.offset.x + viewport.extent.width;
    imageBlit.dstOffsets[0].y           = viewport.offset.y;
    imageBlit.dstOffsets[1].y           = viewport.offset.y + viewport.extent.height;
    imageBlit.dstOffsets[0].z           = 0;
    imageBlit.dstOffsets[1].z           = 1;

    vkCmdBlitImage(cmdbfr, inputColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        _swapchainImageList[swapChainIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit,
        VK_FILTER_LINEAR);
}

void VulkanRendererContext::CreateCommandPool(const uint32_t& qfIndex, VkCommandPool& cmdPool)
{
    VkDevice device = GetVulkanDevice();
    VkCommandPoolCreateInfo commandPoolCreateInfo {};
    commandPoolCreateInfo       = VkCommandPoolCreateInfo {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = qfIndex;
    if (vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &cmdPool) != VK_SUCCESS)
        throw std::runtime_error("Create Command Pool - vkCreateCommandPool failed");
}

void VulkanRendererContext::DestroyCommandPool(const VkCommandPool& cmdPool)
{
    VkDevice device = GetVulkanDevice();
    vkDestroyCommandPool(device, cmdPool, nullptr);
}

void VulkanRendererContext::CreateCommandBuffer(
    const VkCommandPool& p_cmdPool, VkCommandBuffer& p_cmdBuf)
{
    VkDevice device = GetVulkanDevice();
    VkCommandBufferAllocateInfo cmdBfrAllocInfo {};
    cmdBfrAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBfrAllocInfo.commandPool        = p_cmdPool;
    cmdBfrAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBfrAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cmdBfrAllocInfo, &p_cmdBuf) != VK_SUCCESS)
        throw std::runtime_error("Create CommandBuffer - vkAllocateCommandBuffers failed");
}

void VulkanRendererContext::Composite(hvt::FramePass* framePass)
{
    // This composition path is used to test legacy unit tests from testHgiVulkan
    // since they are not based on Viewport Toolbox's framePass, instead test the
    // Hgi layer.
    if (_compositeWithoutFramePass)
    {
        pxr::GfVec4d rect { 0.0, 0.0, (double)width(), (double)height() };
        VkImage image = static_cast<pxr::HgiVulkanTexture*>(_finalColorTarget.Get())->GetImage();
        VkImageLayout layout =
            static_cast<pxr::HgiVulkanTexture*>(_finalColorTarget.Get())->GetImageLayout();
        return Composite(image, layout, rect);
    }

    // Conventional composition path to test all other tests written for Viewport Toolbox.
    if (framePass)
    {
        _finalColorTarget = framePass->GetRenderTexture(pxr::HdAovTokens->color);
        if (_finalColorTarget.Get() == nullptr)
        {
            return;
        }

        pxr::HgiVulkanTexture* vkTex = static_cast<pxr::HgiVulkanTexture*>(_finalColorTarget.Get());
        VkImage inputColor           = vkTex->GetImage();
        VkImageLayout inputColorLayout = vkTex->GetImageLayout();
        return Composite(inputColor, inputColorLayout, framePass->GetViewport());
    }
}

void VulkanRendererContext::Composite(
    const VkImage& inputColor, const VkImageLayout& inputColorLayout, const pxr::GfVec4d& rect)
{
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    PXR_INTERNAL_NS::HgiVulkanCommandQueue* hgiQueue =
        hgiVulkan->GetPrimaryDevice()->GetCommandQueue();
    if (!hgiQueue)
        throw std::runtime_error("Composite - HgiVulkanCommandQueue not found");

    VkQueue gfxQueue = hgiQueue->GetVulkanGraphicsQueue();

    BeginCommandBuffer(_compositionCmdBfr);

    SetLayoutBarrier(
        _compositionCmdBfr, inputColor, inputColorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    SetLayoutBarrier(_compositionCmdBfr, _swapchainImageList[_currentSwapChainId],
        _swapchainLayout[_currentSwapChainId], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    BlitColorToSwapChain(_compositionCmdBfr, inputColor, rect, _currentSwapChainId);

    SetLayoutBarrier(_compositionCmdBfr, _swapchainImageList[_currentSwapChainId],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    _swapchainLayout[_currentSwapChainId] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    SetLayoutBarrier(
        _compositionCmdBfr, inputColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, inputColorLayout);

    VkSemaphoreList waitSemaphores { _acquireSwapchainSemaphore };
    VkSemaphoreList signalSemaphores { _copyToSwapChainCompleteSemaphore };
    Submit(_compositionCmdBfr, gfxQueue, waitSemaphores, signalSemaphores);
}

void VulkanRendererContext::Present(const VkSemaphoreList& waitSemaphores)
{
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    PXR_INTERNAL_NS::HgiVulkanCommandQueue* hgiQueue =
        hgiVulkan->GetPrimaryDevice()->GetCommandQueue();
    if (!hgiQueue)
        throw std::runtime_error("Present - HgiVulkanCommandQueue not found");

    VkQueue gfxQueue = hgiQueue->GetVulkanGraphicsQueue();

    VkPresentInfoKHR presentInfo {};
    VkResult presentResult         = VkResult::VK_RESULT_MAX_ENUM;
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
    presentInfo.pWaitSemaphores    = waitSemaphores.data();
    presentInfo.swapchainCount     = 1; // number of swap chains we want to present to
    presentInfo.pSwapchains        = &_swapChain;
    presentInfo.pImageIndices      = &_currentSwapChainId;
    presentInfo.pResults           = &presentResult;
    if (vkQueuePresentKHR(gfxQueue, &presentInfo) != VK_SUCCESS)
        throw std::runtime_error("Present - vkQueuePresentKHR failed");
}

void VulkanRendererContext::QueueWaitIdle()
{
    PXR_INTERNAL_NS::HgiVulkan* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    PXR_INTERNAL_NS::HgiVulkanCommandQueue* hgiQueue =
        hgiVulkan->GetPrimaryDevice()->GetCommandQueue();
    if (!hgiQueue)
        throw std::runtime_error("Composite - HgiVulkanCommandQueue not found");

    VkQueue gfxQueue = hgiQueue->GetVulkanGraphicsQueue();
    vkQueueWaitIdle(gfxQueue);
}

void VulkanRendererContext::CreateSampler()
{
    pxr::HgiSamplerDesc desc {};
    desc.debugName = pxr::TfToken("Linear Sampler");
    desc.magFilter = pxr::HgiSamplerFilterLinear;
    desc.minFilter = pxr::HgiSamplerFilterLinear;

    _linearSampler = _hgi->CreateSampler(desc);
    if (!_linearSampler)
        throw std::runtime_error("Linear Sample Creation failed");
}

void VulkanRendererContext::DestroySampler()
{
    _hgi->DestroySampler(&_linearSampler);
}

void VulkanRendererContext::CreateTextureBindings(
    const HgiTextureHandleList& textureList, pxr::HgiResourceBindingsHandle& textureBindings)
{
    pxr::HgiResourceBindingsDesc resourceDesc;
    resourceDesc.debugName = "Linear Sampled Texture Binding";
    for (auto& texture : textureList)
    {
        if (texture)
        {
            pxr::HgiTextureBindDesc texBindDesc;
            texBindDesc.bindingIndex = 0;
            texBindDesc.stageUsage   = pxr::HgiShaderStageFragment;
            texBindDesc.writable     = false;
            texBindDesc.textures.push_back(texture);
            if (_linearSampler)
            {
                texBindDesc.samplers.push_back(_linearSampler);
            }
            resourceDesc.textures.push_back(texBindDesc);
        }
    }

    textureBindings = _hgi->CreateResourceBindings(resourceDesc);
    if (!textureBindings)
        throw std::runtime_error("Texture Binding Creation failed");
}

void VulkanRendererContext::DestroyTextureBindings(pxr::HgiResourceBindingsHandle& textureBindings)
{
    if (textureBindings)
        _hgi->DestroyResourceBindings(&textureBindings);
}

void VulkanRendererContext::InitCamera()
{
    // Placing the model in front of the camera so that it
    // fits in its entirety within the view frustum.
    _shaderConstants.modelMatrix *=
        pxr::GfMatrix4f().SetTranslate(pxr::GfVec3f(0.0f, 0.0f, -12.0f));

    pxr::GfFrustum frustrum;
    frustrum.SetPerspective(45, true, (double)width() / (double)height(), 0.1f, 1000.0);
    _shaderConstants.projectionMatrix = (pxr::GfMatrix4f)frustrum.ComputeProjectionMatrix();
}

std::vector<float> VulkanRendererContext::ShaderConsts::CopyToBuffer() const
{
    std::vector<float> raw;
    std::copy(modelMatrix[0], modelMatrix[16], std::back_inserter(raw));
    std::copy(viewMatrix[0], viewMatrix[16], std::back_inserter(raw));
    std::copy(projectionMatrix[0], projectionMatrix[16], std::back_inserter(raw));
    return raw;
}

VulkanTestContext::VulkanTestContext()
{
    init();
}

VulkanTestContext::VulkanTestContext(int w, int h) : TestContext(w, h)
{
    init();
}

void VulkanTestContext::init()
{
    namespace fs = std::filesystem;

    _sceneFilepath = TOSTRING(TEST_DATA_RESOURCE_PATH) + "/data/usd/test_fixed.usda";

    // Create the renderer context required for Hydra.
    _backend = std::make_shared<TestHelpers::VulkanRendererContext>(_width, _height);
    if (!_backend)
    {
        throw std::runtime_error("Failed to initialize the unit test backend!");
    }

    fs::path dataPath =
        std::filesystem::path(TOSTRING(TEST_DATA_RESOURCE_PATH), fs::path::native_format);
    dataPath.append("Data");
    _backend->setDataPath(dataPath);

    // If the presentation task is enabled, interop-present task get involved.
    // Which for the Vulkan backend involves copying a Vulkan image to OpenGL
    // before presenting to an OpenGL context. This is against the intended
    // design of our-case. We wish to explicitly present to a pure Vulkan
    // implementation. 
    _usePresentationTask = false;

    _enableFrameCancellation = true;
}

} // namespace TestHelpers
