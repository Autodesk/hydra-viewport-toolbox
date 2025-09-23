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

#include <hvt/engine/renderBufferSettingsProvider.h>

#include <hvt/engine/syncDelegate.h>

#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

using RenderBufferManagerPtr = std::shared_ptr<class RenderBufferManager>;

/// A class that maintains render buffers (targets) associated with a render index and provides AOV
/// settings for tasks that use render buffers.
///
/// This class is NOT responsible for directly setting Task parameters. Task Parameters IDs
/// are neither known, received or manipulated by this class (only RenderBuffer BPrim IDs are
/// known).
///
/// Although this class does not directly set Task parameters, it does store shared AOV input and
/// AOV binding data settings. These AOV settings can be consulted by Tasks to update their own
/// data, indirectly.
///
class RenderBufferManager : public RenderBufferSettingsProvider
{
public:
    /// Constructor.
    /// \param taskManagerUid The associated TaskManager unique identifier.
    /// \param pRenderIndex The HdRenderIndex used to create render buffer Bprims.
    /// \param syncDelegate The scene delegate instance to use.
    RenderBufferManager(PXR_NS::SdfPath const& taskManagerUid, PXR_NS::HdRenderIndex* pRenderIndex,
        SyncDelegatePtr& syncDelegate);

    /// Destructor.
    ~RenderBufferManager();

    /// Gets the dimensions of the render buffers.
    /// \return Returns the render buffer dimensions.
    inline PXR_NS::GfVec2i const& GetRenderBufferDimensions() const { return _size; }

    /// Get the AOV texture handle by its token e.g., color or depth.
    /// \param token The identifier of the render texture.
    /// \return The associated render texture or null if not found.
    PXR_NS::HgiTextureHandle GetAovTexture(
        PXR_NS::TfToken const& token, PXR_NS::HdEngine* engine) const;

    /// Get the render buffer by its name.
    PXR_NS::HdRenderBuffer* GetRenderOutput(PXR_NS::TfToken const& name);

    /// Update multisample state and buffer size of Bprim descriptors, mark it dirty if changed.
    void SetBufferSizeAndMsaa(
        PXR_NS::GfVec2i const& newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled);

    /// Set the render outputs.
    /// It does NOT update any RenderTaskParams, but updates the AovParamCache and the viewport AOV.
    bool SetRenderOutputs(const PXR_NS::TfTokenVector& names, const RenderBufferBindings& inputs,
        const PXR_NS::GfVec4d& viewport);

    /// Set the render output clear color in the AovParamCache.
    void SetRenderOutputClearColor(PXR_NS::TfToken const& name, PXR_NS::VtValue const& clearValue);

    /// Set the viewport AOV render output (color or buffer visualization).
    void SetViewportRenderOutput(PXR_NS::TfToken const& name, PXR_NS::HdRenderBuffer* aovBuffer,
        PXR_NS::HdRenderBuffer* depthBuffer);

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(PXR_NS::TfToken const& api, PXR_NS::VtValue const& framebuffer);


    // VtValue destinationInteropHandle : actual type is HgiPresentInteropHandle
    //                        Which can have the following variant types:
    //                             using HgiPresentInteropHandle = std::variant<
    //                                  HgiPresentNullInteropHandle,
    //                                  HgiPresentGLInteropHandle,
    //                                  HgiPresentDX11InteropHandle,
    //                                  HgiPresentDX12InteropHandle ...>
    // 
    // VtValue compositionParams : actual type is HgiPresentCompositionParams
    //                        Which can have the following variant types:
    //                             using HgiPresentInteropHandle = std::variant<
    //                                  HgiPresentNullInteropHandle,
    //                                  HgiPresentGLInteropHandle,
    //                                  HgiPresentDX11InteropHandle,
    //                                  HgiPresentDX12InteropHandle ...>
    // 
    // Usage examples:
    // [DX]
    // HgiPresentDX12InteropHandle interop { [this]() { return getOffscreenTxColor(); } };
    // pController->EnableInteropPresentation(interop);
    // [GL]
    // HgiPresentInteropHandle interop = HgiPresentGLInteropHandle{
    // static_cast<uint32_t>(m_OffscreenFbo) };
    // pController->EnableInteropPresentation(interop);
    //
    void EnableInteropPresentation(
        PXR_NS::VtValue const& destinationInteropHandle, PXR_NS::VtValue const& compositionParams);

    // VtValue windowHandle : actual type is HgiPresentWindowHandle 
    //                        Which can have the following variant types:
    //                             using HgiPresentWindowHandle = std::variant<
    //                                         HgiPresentNullWindowHandle,
    //                                         HgiPresentAndroidWindowHandle,
    //                                         HgiPresentMetalWindowHandle,
    //                                         HgiPresentWin32WindowHandle ...>
    // Usage examples:
    // [DX]
    // pController->EnableWindowPresentation(HgiPresentWin32WindowHandle{ GetModuleHandle(nullptr), getWindow() });

    void EnableWindowPresentation(PXR_NS::VtValue const& windowHandle, bool vsync);

    /// Returns true if AOVs (RenderBuffer Bprim type) are supported by the Render Index.
    bool IsAovSupported() const override;

    /// Returns true if progressive rendering is enabled.
    bool IsProgressiveRenderingEnabled() const override;

    /// Get the AOV token associated with the viewport.
    PXR_NS::TfToken const& GetViewportAov() const override;

    /// Get the render buffer size.
    PXR_NS::GfVec2i const& GetRenderBufferSize() const override;

    /// Get the AOV parameters cache, which contains data transfered to the  TaskManager before
    /// executing tasks.
    AovParams const& GetAovParamCache() const override;

    PresentationParams const& GetPresentationParams() const override;

private:
    /// The RenderBufferManager identifier.
    const PXR_NS::SdfPath _taskManagerUid;

    /// The RenderIndex, used to create Bprims (buffers).
    PXR_NS::HdRenderIndex* _pRenderIndex { nullptr };

    /// The render texture dimensions.
    PXR_NS::GfVec2i _size { 0, 0 };

    /// The render buffer management code, extracted from the HdxTaskController.
    /// \note This class uses the pimpl idiom only to hide the implementation details, the goal
    /// is NOT to have multiple different implementations for various platforms or backends.
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace HVT_NS