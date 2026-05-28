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
#pragma once

#include <hvt/api.h>

#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hdSt/renderBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    HVT_OUTLINE_PRIM_IDS_PARAMS,
    HVT_OUTLINE_PRIM_IDS_RESOURCES,
    HVT_OUTLINE_PRIM_IDS_VALIDATE
);

PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{

PXR_NAMESPACE_USING_DIRECTIVE;

struct HVT_API OutlinePrimIdsTaskParams
{
    OutlinePrimIdsTaskParams()
        : enabled(false)
        , bufferPrefix("Base")
        , size{ 0, 0 }
        , cullStyle(PXR_NS::HdCullStyleNothing)
        , overrideWindowPolicy(CameraUtilDontConform)
    {
    }

    bool operator==(OutlinePrimIdsTaskParams const& other) const
    {
        if (enabled != other.enabled ||
            bufferPrefix != other.bufferPrefix ||
            size != other.size ||
            collection != other.collection ||
            camera != other.camera ||
            cullStyle != other.cullStyle ||
            framing != other.framing ||
            overrideWindowPolicy != other.overrideWindowPolicy ||
            aovBindings.size() != other.aovBindings.size()) {
            return false;
        }

        for (size_t i = 0; i < aovBindings.size(); ++i) {
            if (aovBindings[i].renderBufferId != other.aovBindings[i].renderBufferId) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(OutlinePrimIdsTaskParams const& other) const
    {
        return !(*this == other);
    }

    bool enabled;
    std::string bufferPrefix;
    PXR_NS::GfVec2i size;
    PXR_NS::HdRprimCollection collection;
    PXR_NS::SdfPath camera;
    PXR_NS::HdCullStyle cullStyle;
    CameraUtilFraming framing;
    std::optional<CameraUtilConformWindowPolicy> overrideWindowPolicy;
    PXR_NS::HdRenderPassAovBindingVector aovBindings;
};

/// A task to render outline primIds and depth buffers from an input collection.
class HVT_API OutlinePrimIdsTask : public PXR_NS::HdxTask
{
public:
    OutlinePrimIdsTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);

    ~OutlinePrimIdsTask() override;

    void Prepare(PXR_NS::HdTaskContext* ctx,
                 PXR_NS::HdRenderIndex* renderIndex) override;

    void Execute(PXR_NS::HdTaskContext* ctx) override;

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate,
               PXR_NS::HdTaskContext* ctx,
               PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    bool _InitIfNeeded();
    void _CreateAovBindings();
    void _CleanupAovBindings();
    bool _Enabled() const;

    void _ValidatePrimIdBuffer(PXR_NS::HdRenderPassAovBinding binding, VtValue resource);
    HgiTextureHandle _GetTextureHandleForBinding(size_t bindingIndex) const;

    PXR_NS::TfToken _GetShaderFilePath();

    PXR_NS::HdRenderIndex* _renderIndex;

    // Render pass to render primId and depth buffers for the outline collection
    PXR_NS::HdRenderPassSharedPtr _renderPass;
    PXR_NS::HdRenderPassStateSharedPtr _renderPassState;

    std::vector<std::unique_ptr<PXR_NS::HdStRenderBuffer>> _aovBuffers;

    PXR_NS::HdRenderPassAovBinding _primIdBinding;
    PXR_NS::HdRenderPassAovBindingVector _aovBindings;

    size_t _primIdBindingIndex{0};
    size_t _depthBindingIndex{1};

    OutlinePrimIdsTaskParams _params;

    bool _isStormRenderer{false};
    bool _vpChanged;

    OutlinePrimIdsTask() = delete;
    OutlinePrimIdsTask(OutlinePrimIdsTask const&) = delete;
    OutlinePrimIdsTask& operator=(OutlinePrimIdsTask const&) = delete;
};

} // namespace HVT_NS
