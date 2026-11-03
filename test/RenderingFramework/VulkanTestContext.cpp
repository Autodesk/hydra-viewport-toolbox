// Copyright 2026 Autodesk, Inc.
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

#include "VulkanTestContext.h"

#if _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4244)
#endif

#include <pxr/imaging/hgiVulkan/commandQueue.h>
#include <pxr/imaging/hgiVulkan/device.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/hgiInstance.h>

#if _MSC_VER
#pragma warning(pop)
#endif

#include <stdexcept>
#include <string>

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

VulkanRendererContext::VulkanRendererContext(int width, int height) :
    HydraRendererContext(width, height)
{
    // FramePass color AOV is Y-flipped (OpenGL-style, row 0 = bottom). Flip on PNG write
    // so saved images match OpenGL and display orientation.
    _imageCapture.flipVertically(true);

    // The tests render offscreen and read the color AOV back, they never present. Enabling the
    // presentation task would involve the interop-present task, which for the Vulkan backend
    // copies a Vulkan image to OpenGL before presenting to an OpenGL context.
    _presentationEnabled = false;

    createHGI(pxr::HgiTokens->Vulkan);
}

VulkanRendererContext::~VulkanRendererContext()
{
    destroyHGI();
}

void VulkanRendererContext::shutdown()
{
    // Nothing to release: there is no window, surface or swapchain. The Hgi is released by the
    // destructor.
    waitForGPUIdle();
}

void VulkanRendererContext::run(std::function<bool()> render, hvt::FramePass* framePass)
{
    bool moreFrames = true;
    while (moreFrames)
    {
        try
        {
            moreFrames = render();
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

    captureColorTexture(framePass);
}

void VulkanRendererContext::waitForGPUIdle()
{
    auto* hgiVulkan = static_cast<PXR_INTERNAL_NS::HgiVulkan*>(_hgi.get());
    if (!hgiVulkan)
    {
        return;
    }

    PXR_INTERNAL_NS::HgiVulkanDevice* device = hgiVulkan->GetPrimaryDevice();
    if (!device)
    {
        throw std::runtime_error("waitForGPUIdle - HgiVulkanDevice not found");
    }

#if PXR_VERSION > 2511
    // From USD v0.26.x, Hgi defers command buffer submissions. Drain the deferred queue so
    // that all recorded work is actually submitted before waiting for the device to be idle.
    if (PXR_INTERNAL_NS::HgiVulkanCommandQueue* queue = device->GetCommandQueue())
    {
        queue->Flush(pxr::HgiSubmitWaitTypeNoWait);
    }
#endif

    device->WaitForIdle();
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
    _sceneFilepath = TestHelpers::ResolveAssetPath("usd/test_fixed.usda").string();

    // Create the renderer context required for Hydra.
    _backend = std::make_shared<TestHelpers::VulkanRendererContext>(_width, _height);
    if (!_backend)
    {
        throw std::runtime_error("Failed to initialize the unit test backend!");
    }

    // The presentation task would copy a Vulkan image to an OpenGL context (interop-present),
    // which is not wanted: the tests only read the color AOV back.
    _usePresentationTask = false;

    _enableFrameCancellation = true;
}

} // namespace TestHelpers
