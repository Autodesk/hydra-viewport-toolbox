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

#include <hvt/api.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/usd/sdf/path.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// clang-format on

#include <optional>
#include <vector>

namespace HVT_NS
{

/// Parameters for the Flash-specific pick task.
struct HVT_API FlashPickTaskParams
{
    PXR_NS::HdCullStyle cullStyle = PXR_NS::HdCullStyleNothing;
    PXR_NS::TfTokenVector renderTags;

    /// Camera to use for rendering the ID buffers.
    PXR_NS::SdfPath cameraId;

    /// Framing / viewport information (mirrors HdxPickFromRenderBufferTaskParams).
    PXR_NS::CameraUtilFraming framing;
    std::optional<PXR_NS::CameraUtilConformWindowPolicy> overrideWindowPolicy;

    bool operator==(FlashPickTaskParams const& other) const
    {
        return cullStyle == other.cullStyle && renderTags == other.renderTags &&
            cameraId == other.cameraId && framing == other.framing &&
            overrideWindowPolicy == other.overrideWindowPolicy;
    }
    bool operator!=(FlashPickTaskParams const& other) const { return !(*this == other); }
};

/// \class FlashPickTask
///
/// A Flash-specific pick task that renders to its own ID buffers (primId,
/// instanceId, elementId, depth) and resolves hits using HdxPickResult.
///
/// The task only executes when HdxPickTokens->pickParams is present in the
/// task context (set by FramePass::Pick()), so there is no rendering overhead
/// when selection is not active.
class HVT_API FlashPickTask : public PXR_NS::HdTask
{
public:
    FlashPickTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~FlashPickTask() override;

    static PXR_NS::TfToken const& GetToken();

    void Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;

    void Execute(PXR_NS::HdTaskContext* ctx) override;

    PXR_NS::TfTokenVector const& GetRenderTags() const override;

private:
    void _CreateIdBuffers();
    void _CleanupIdBuffers();
    void _ResizeBuffers(int32_t width, int32_t height);
    PXR_NS::GfMatrix4d _ComputeProjectionMatrix() const;

    FlashPickTaskParams _params;

    PXR_NS::TfTokenVector _renderTags;
    PXR_NS::HdRenderIndex* _index { nullptr };

    PXR_NS::HdRenderPassSharedPtr _renderPass;
    PXR_NS::HdRenderPassStateSharedPtr _renderPassState;

    // Task-owned ID render buffers
    PXR_NS::HdRenderBuffer* _primIdBuffer { nullptr };
    PXR_NS::HdRenderBuffer* _instanceIdBuffer { nullptr };
    PXR_NS::HdRenderBuffer* _elementIdBuffer { nullptr };
    PXR_NS::HdRenderBuffer* _depthBuffer { nullptr };

    PXR_NS::HdRenderPassAovBindingVector _pickAovBindings;

    const PXR_NS::HdCamera* _camera { nullptr };
    bool _isFlashRenderer { false };
    int32_t _bufferWidth { 0 };
    int32_t _bufferHeight { 0 };

    FlashPickTask()                                = delete;
    FlashPickTask(const FlashPickTask&)            = delete;
    FlashPickTask& operator=(const FlashPickTask&) = delete;
};

} // namespace HVT_NS
