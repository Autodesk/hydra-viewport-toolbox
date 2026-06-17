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

#include <hvt/engine/renderBufferSettingsProvider.h>

#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

/// The scene-index (SI) based render buffer management implementation.
///
/// The Impl is derived from HdxTaskController. The Impl consolidates the render-buffer related
/// operations which were originally in the Task Controller.
///
/// \note This used to be the nested RenderBufferManager::Impl class. It was extracted into a
/// standalone class so a second (scene-delegate based) implementation can coexist with it.
class RenderBufferManagerSIImpl : public RenderBufferSettingsProvider
{
public:
    explicit RenderBufferManagerSIImpl(PXR_NS::HdRenderIndex* pRenderIndex,
        PXR_NS::HdRetainedSceneIndexRefPtr const& retainedSceneIndex);
    ~RenderBufferManagerSIImpl();

    RenderBufferManagerSIImpl(RenderBufferManagerSIImpl const&)            = delete;
    RenderBufferManagerSIImpl& operator=(RenderBufferManagerSIImpl const&) = delete;

    /// Sets the size of the render buffer and MSAA settings, update render buffer descriptors.
    void SetBufferSizeAndMsaa(
        const PXR_NS::GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled);

    PXR_NS::HdRenderBuffer* GetRenderOutput(
        PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId);

    /// Updates render output parameters and creates new render buffers if needed.
    /// Note: AOV binding values are stored here and consulted later by RenderTasks.
    bool SetRenderOutputs(PXR_NS::TfToken const& outputToVisualize,
        PXR_NS::TfTokenVector const& outputs, RenderBufferBindings const& inputs,
        PXR_NS::GfVec4d const& viewport, PXR_NS::SdfPath const& controllerId);

    /// Get the render outputs.
    PXR_NS::TfTokenVector const& GetRenderOutputs() const { return _aovOutputs; }

    /// Updates the render output clear color.
    /// Note: Clear color values are stored here and consulted later by RenderTasks.
    void SetRenderOutputClearColor(PXR_NS::TfToken const& name, PXR_NS::SdfPath const& controllerId,
        PXR_NS::VtValue const& clearValue);

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(PXR_NS::TfToken const& api, PXR_NS::VtValue const& framebufferHandle)
    {
        _presentParams.windowHandle      = PXR_NS::VtValue();
        _presentParams.api               = api;
        _presentParams.framebufferHandle = framebufferHandle;
    }

    /// Set interop destination handle to present to and composition parameters.
    void SetInteropPresentation(
        PXR_NS::VtValue const& destinationInteropHandle, PXR_NS::VtValue const& composition)
    {
        // NOTE: The underlying type of destinationInteropHandle VtValue is HgiPresentInteropHandle,
        // which is a std::variant. See declaration of HgiPresentInteropHandle for more details.
        _presentParams.windowHandle      = PXR_NS::VtValue();
        _presentParams.framebufferHandle = destinationInteropHandle;
        _presentParams.compositionParams = composition;
    }

    /// Set vsync and window destination handle to present to.
    void SetWindowPresentation(PXR_NS::VtValue const& windowHandle, bool vsync)
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
    void _PrepareBuffersFromInputs(RenderBufferBinding const& colorInput,
        RenderBufferBinding const& depthInput, PXR_NS::HdRenderBufferDescriptor const& desc,
        PXR_NS::SdfPath const& controllerId);

    /// Copy the depth AOV of the input buffer into the output buffer.
    void _PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
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
    ///  - Add and remove Bprims from the retained scene index.
    ///  - Get Bprims from the RenderIndex.
    PXR_NS::SdfPathVector _aovBufferIds;

    /// AOV output cache, for checking if outputs have changed since the last call and only update
    /// render or aovBindings when necessary.
    PXR_NS::TfTokenVector _aovOutputs;

    /// AOV input cache, for checking if inputs have changed since the last call.
    RenderBufferBindings _aovInputs;

    /// Viewport AOV cache to prevent unnecessary execution or dirty states in
    /// SetViewportRenderOutput.
    PXR_NS::TfToken _viewportAov;

    /// Intermediate storage for RenderTask AOV parameters. These values are meant to be set
    /// into RenderTaskParams, but the RenderBufferManager is not responsible for doing it, it only
    /// stores the values.
    AovParams _aovTaskCache;

    /// The presentation parameters. This class holds data relevant to the HdxPresentTask.
    PresentationParams _presentParams;

    /// The RenderIndex, used to create Bprims (buffers).
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };

    /// The retained scene index used for render buffer Bprims.
    PXR_NS::HdRetainedSceneIndexRefPtr _retainedSceneIndex;

    /// The shaders used to copy the contents of the input into the output render buffer.
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShaderNoDepth;
    std::unique_ptr<CopyDepthShader> _copyDepthShader;
};

} // namespace HVT_NS
