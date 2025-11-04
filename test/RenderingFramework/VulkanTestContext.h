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

#include <RenderingFramework/TestHelpers.h>

#if _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4244)
#endif

#include <pxr/base/gf/matrix4f.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/imaging/hgiVulkan/hgi.h>

#if _MSC_VER
#pragma warning(pop)
#endif

// There are parts of Vulkan handles that are acquired from HGI but a
// significant portion of the local handles are created naively for
// the presentation and swap-chain management process. Since HGI Vulkan,
// at the moment does not have support for it.
#include <vulkan/vulkan.h>
// Vulkan can indirectly include the Xlib.h header,
// which defines a ton of common words a macros.
// Guard against the conflicting and risk ones.
#undef Bool
#undef None
#undef Status
#undef True
#undef False

#define FRAME_BUFFER_COUNT 2

struct SDL_Window;

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

class VulkanRendererContext : public HydraRendererContext
{
public:
    struct ShaderProgDesc
    {
        pxr::TfToken debugName;
        pxr::HgiShaderFunctionDesc vertShaderDesc;
        pxr::HgiShaderFunctionDesc fragShaderDesc;

        pxr::HgiShaderFunctionHandle vertShaderFunc;
        pxr::HgiShaderFunctionHandle fragShaderFunc;

        pxr::HgiShaderProgramHandle shaderProg;
    };

    struct ShaderConsts
    {
        pxr::GfMatrix4f modelMatrix;
        pxr::GfMatrix4f viewMatrix;
        pxr::GfMatrix4f projectionMatrix;

        std::vector<float> CopyToBuffer() const;
    };

    struct Vertex
    {
        float pos[3];
        float normal[3];
        float uv[2];
        float tangent[4];
    };

    typedef std::vector<VkSemaphore> VkSemaphoreList;
    typedef std::vector<pxr::HgiTextureHandle> HgiTextureHandleList;

    VulkanRendererContext(int width, int height);
    VulkanRendererContext(const VulkanRendererContext&)        = delete;
    VulkanRendererContext& operator=(const VulkanRendererContext&) = delete;
    ~VulkanRendererContext();

    void init();
    void shutdown() override;
    bool saveImage(const std::string& fileName) override;
    void run(std::function<bool()> render, hvt::FramePass* framePass) override;
    void waitForGPUIdle() override;
    
    /// \brief Sets the final color buffer for copy-to-swapchain
    /// Call this before presenting so that Vulkan composition can
    /// happen before presentation
    void SetFinalColorImage(const pxr::HgiTextureHandle& image);

    ///\brief Helper functions to be called from the unit test.
    /// These functions are  wrappers since hgi  handle is not
    /// accessible from the unit test
    void CreateGfxCmdHandle(const pxr::HgiGraphicsCmdsDesc& desc, pxr::HgiGraphicsCmdsUniquePtr&);
    void CreateTexture(const pxr::HgiTextureUsage& usage, const pxr::HgiFormat& format,
        pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view);
    void CreateTexture(const pxr::HgiTextureDesc& texDesc, pxr::HgiTextureViewDesc& viewDesc,
        pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view);
    void DestroyTexture(pxr::HgiTextureHandle& texture, pxr::HgiTextureViewHandle& view);

    void CreateTextureBindings(
        const HgiTextureHandleList& textureList, pxr::HgiResourceBindingsHandle& resourceBindings);
    void DestroyTextureBindings(pxr::HgiResourceBindingsHandle& textureBindings);

    void CreateBuffer(const pxr::HgiBufferDesc& desc, pxr::HgiBufferHandle& buffer);
    void DestroyBuffer(pxr::HgiBufferHandle& buffer);

    void CreateGfxPipeline(
        const pxr::HgiGraphicsPipelineDesc& desc, pxr::HgiGraphicsPipelineHandle& pipeline);
    void DestroyGfxPipeline(pxr::HgiGraphicsPipelineHandle& pipeline);
    void Submit(pxr::HgiCmds* cmds, const pxr::HgiSubmitWaitType& wait);

    void CreateShaderHandle(ShaderProgDesc& desc);
    void DestroyShaderHandle(ShaderProgDesc& desc);

    const ShaderConsts& GetShaderConstants() { return _shaderConstants; }

protected:
    void beginVk();
    void endVk();

private:
    bool _compositeWithoutFramePass = false;

    SDL_Window* _mSDLWWindow = nullptr;
    VkSurfaceKHR _surface;
    VkSwapchainKHR _swapChain;
    VkImage _swapchainImageList[FRAME_BUFFER_COUNT];
    VkImageView _swapchainImageViewList[FRAME_BUFFER_COUNT];
    VkImageLayout _swapchainLayout[FRAME_BUFFER_COUNT];

    uint32_t _currentSwapChainId;
    VkSemaphore _acquireSwapchainSemaphore;
    VkSemaphore _renderingCompleteSemaphore;
    VkSemaphore _copyToSwapChainCompleteSemaphore;

    VkCommandPool _compositionCmdPool;
    VkCommandBuffer _compositionCmdBfr;

    pxr::HgiTextureHandle _finalColorTarget;
    ShaderConsts _shaderConstants;

    pxr::HgiSamplerHandle _linearSampler;

    inline VkDevice GetVulkanDevice()
    {
        PXR_INTERNAL_NS::HgiVulkan* hgiVulkan =
            static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
        VkDevice device = hgiVulkan->GetPrimaryDevice()->GetVulkanDevice();
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("Vulkan device not found");
        return device;
    }

    /// \brief Vulkan specific helper functions for Swapchain management,
    /// final render target composition and presentation management
    void CreateSurface(SDL_Window*);
    void DestroySurface();
    void CreateSwapchain(uint32_t w, uint32_t h);
    void CreateSwapchainImages();
    void DestroySwapchainImages();

    void CreateCommandPool(const uint32_t& qfIndex, VkCommandPool& cmdPool);
    void DestroyCommandPool(const VkCommandPool& cmdPool);
    void CreateCommandBuffer(const VkCommandPool& cmdPool, VkCommandBuffer& cmdBuf);

    void CreateASemaphore(VkSemaphore& semaphore);
    void DestroyASemaphore(VkSemaphore& semaphore);

    void SetRenderCompleteSemaphore(const VkSemaphore& semaphore);
    void AcquireNextSwapchain(uint32_t& swapChainID);

    void BeginCommandBuffer(const VkCommandBuffer& cmdBfr);
    
    void Submit(const VkCommandBuffer& cmdBfr, const VkQueue& queue,
        const VkSemaphoreList& waitSemaphores, const VkSemaphoreList& signalSemaphores);
    
    void SetLayoutBarrier(const VkCommandBuffer& cmdBfr, const VkImage& image,
        const VkImageLayout& oldLayout, const VkImageLayout& newLayout);

    void BlitColorToImage(const VkCommandBuffer& cmdbfr, const VkImage& src,
        const VkOffset3D& srcOffset, VkImage dst, const VkOffset3D& dstOffset);

    void CopyColorToSwapChain(
        const VkCommandBuffer& cmdbfr, const VkImage& inputColor, const uint32_t& swapChainIndex);

    void BlitColorToSwapChain(const VkCommandBuffer& cmdbfr, const VkImage& inputColor,
        const pxr::GfVec4i& rect, const uint32_t& swapChainIndex);

    void Composite(const VkImage& inputColor, const VkImageLayout& inputColorLayout, const pxr::GfVec4i& rect);
    
    void Composite(hvt::FramePass* framePass);

    void Present(const VkSemaphoreList& waitSemaphores);

    void QueueWaitIdle();

    void CreateSampler();
    void DestroySampler();

    void InitCamera();
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
