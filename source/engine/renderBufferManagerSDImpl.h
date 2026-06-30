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

#include "copyDepthShader.h"
#include "renderBufferManagerImpl.h"

#include <hvt/engine/renderBufferSettingsProvider.h>
#include <hvt/engine/syncDelegate.h>

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

/// The scene-delegate (SD) based render buffer management implementation.
///
/// This is the implementation used before the migration of the TaskManager to Hydra 2.0 scene
/// indices (commit 7bfc0f1). It maintains render buffer Bprims directly in the render index, backed
/// by a SyncDelegate.
///
/// \note This used to be the nested RenderBufferManager::Impl class. It was extracted into a
/// standalone class so it can coexist with the scene-index based implementation, selectable at
/// runtime.
class RenderBufferManagerSDImpl : public RenderBufferManagerImpl
{
public:
    explicit RenderBufferManagerSDImpl(
        PXR_NS::HdRenderIndex* pRenderIndex, SyncDelegatePtr const& syncDelegate);
    ~RenderBufferManagerSDImpl() override;

    RenderBufferManagerSDImpl(RenderBufferManagerSDImpl const&)            = delete;
    RenderBufferManagerSDImpl& operator=(RenderBufferManagerSDImpl const&) = delete;

    /// Sets the size of the render buffer and MSAA settings, update render buffer descriptors.
    void SetBufferSizeAndMsaa(const PXR_NS::GfVec2i newRenderBufferSize, size_t msaaSampleCount,
        bool msaaEnabled) override;

    PXR_NS::HdRenderBuffer* GetRenderOutput(
        PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId) override;

    /// Updates render output parameters and creates new render buffers if needed.
    /// Note: AOV binding values are stored here and consulted later by RenderTasks.
    bool SetRenderOutputs(PXR_NS::TfToken const& outputToVisualize,
        PXR_NS::TfTokenVector const& outputs, RenderBufferBindings const& inputs,
        PXR_NS::GfVec4d const& viewport, PXR_NS::SdfPath const& controllerId) override;

    /// Get the render outputs.
    PXR_NS::TfTokenVector const& GetRenderOutputs() const override { return _aovOutputs; }

    /// Updates the render output clear color.
    /// Note: Clear color values are stored here and consulted later by RenderTasks.
    void SetRenderOutputClearColor(PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId,
        PXR_NS::VtValue const& clearValue) override;

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(
        PXR_NS::TfToken const& api, PXR_NS::VtValue const& framebufferHandle) override
    {
        _presentParams.windowHandle      = PXR_NS::VtValue();
        _presentParams.api               = api;
        _presentParams.framebufferHandle = framebufferHandle;
    }

    /// Set interop destination handle to present to and composition parameters.
    void SetInteropPresentation(PXR_NS::VtValue const& destinationInteropHandle,
        PXR_NS::VtValue const& composition) override
    {
        // NOTE: The underlying type of destinationInteropHandle VtValue is HgiPresentInteropHandle,
        // which is a std::variant. See declaration of HgiPresentInteropHandle for more details.
        _presentParams.windowHandle      = PXR_NS::VtValue();
        _presentParams.framebufferHandle = destinationInteropHandle;
        _presentParams.compositionParams = composition;
    }

    /// Set vsync and window destination handle to present to.
    void SetWindowPresentation(PXR_NS::VtValue const& windowHandle, bool vsync) override
    {
        // NOTE: The underlying type of windowHandle VtValue is HgiPresentWindowHandle,
        // which is a std::variant. See declaration of HgiPresentWindowHandle.
        _presentParams.windowHandle      = windowHandle;
        _presentParams.windowVsync       = vsync;
        _presentParams.framebufferHandle = PXR_NS::VtValue();
    }

    /// Returns true if AOVs (RenderBuffer Bprims) are supported by the render delegate.
    bool IsAovSupported() const override;

    /// Returns true if progressive rendering is enabled.
    bool IsProgressiveRenderingEnabled() const override { return _isProgressiveRenderingEnabled; }

    /// Returns the name of the AOV to be used for the viewport.
    PXR_NS::TfToken const& GetViewportAov() const override { return _viewportAov; }

    /// Get the size of the render buffers.
    PXR_NS::GfVec2i const& GetRenderBufferSize() const override;

    /// Returns the AOV parameter cache, containing data required to update RenderTask AOV binding
    /// parameters.
    AovParams const& GetAovParamCache() const override { return _aovTaskCache; }

    // Returns the presentation parameters, containing data relevant to the HdxPresentTask.
    PresentationParams const& GetPresentationParams() const override { return _presentParams; }

private:
    /// Copy the color & depth AOVs of the input buffers into the output buffers.
    void PrepareBuffersFromInputs(RenderBufferBinding const& colorInput,
        RenderBufferBinding const& depthInput, PXR_NS::HdRenderBufferDescriptor const& desc,
        PXR_NS::SdfPath const& controllerId);

    /// Copy the depth AOV of the input buffer into the output buffer.
    void PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
        PXR_NS::HdRenderBufferDescriptor const& desc, PXR_NS::SdfPath const& controllerId);

    /// Sets the viewport render output (color or buffer visualization).
    void SetViewportRenderOutput(const PXR_NS::TfToken& name, const PXR_NS::SdfPath& controllerId);

    /// The render texture dimensions.
    PXR_NS::GfVec2i _renderBufferSize { 0, 0 };

    /// Multisampling enabled or not.
    bool _enableMultisampling { true };

    /// Number of samples for multisampling.
    size_t _msaaSampleCount { 4 };

    bool _isProgressiveRenderingEnabled { false };

    /// List of Bprim IDs. These IDs are used to:
    ///  - Insert and remove Bprims from the RenderIndex.
    ///  - Get Bprims from the RenderIndex.
    ///  - Get and Set parameters in the SyncDelegate.
    PXR_NS::SdfPathVector _aovBufferIds;

    /// AOV output cache, for checking if outputs have changed since the last call and only update
    /// render or aovBindings when necessary.
    PXR_NS::TfTokenVector _aovOutputs;

    /// AOV input cache, for checking if inputs have changed since the last call.
    RenderBufferBindings _aovInputs;

    /// Viewport AOV cache to prevent unnecessary execution or dirty states in
    /// SetViewportRenderOutput.
    PXR_NS::TfToken _viewportAov;

    /// Intermediate storage for RenderTask AOV parameters.
    AovParams _aovTaskCache;

    /// The presentation parameters. This class holds data relevant to the HdxPresentTask.
    PresentationParams _presentParams;

    /// The RenderIndex, used to create Bprims (buffers).
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };

    /// The SyncDelegate used to create RenderBufferDescriptor data for use by the render index.
    SyncDelegatePtr _syncDelegate;

    /// The shaders used to copy the contents of the input into the output render buffer.
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShaderNoDepth;
    std::unique_ptr<CopyDepthShader> _copyDepthShader;
};

} // namespace HVT_NS
