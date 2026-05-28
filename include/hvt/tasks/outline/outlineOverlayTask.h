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

#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hio/image.h>

namespace HVT_NS
{

PXR_NAMESPACE_USING_DIRECTIVE;

enum class HVT_API BlurMode
{
    None,
    Blur3x3,
    Blur5x5
};

struct HVT_API OutlineOverlayTaskParams
{
    OutlineOverlayTaskParams()
        : enabled(false)
        , size{ 0, 0 }
        , screenScale{ 1.0f }
        , blurMode(BlurMode::Blur3x3)
        , blurIntensity(1.0f)
    {
    }

    bool operator==(OutlineOverlayTaskParams const& other) const
    {
        // renderTaskParams is excluded: it is not consumed by Execute/_Sync and
        // HioImage::StorageSpec lacks operator==, which breaks VtValue compatibility.
        if (enabled != other.enabled ||
            size != other.size ||
            screenScale != other.screenScale ||
            blurMode != other.blurMode ||
            blurIntensity != other.blurIntensity ||
            imageSpec != other.imageSpec) {
            return false;
        }

        return true;
    }

    bool operator!=(OutlineOverlayTaskParams const& other) const
    {
        return !(*this == other);
    }

    PXR_NS::HdxRenderTaskParams renderTaskParams;

    bool enabled;
    PXR_NS::GfVec2i size;
    float screenScale; // Scale factor w.r.t uv (1, 0) as pivot. 1.0 = full screen

    // Gaussian Blur parameters
    BlurMode blurMode;
    float blurIntensity; // Blur intensity multiplier (default = 1.0)

    std::shared_ptr<PXR_NS::HioImage::StorageSpec> imageSpec = nullptr;
};

/// A task to composite an outline mask texture onto the color AOV.
class HVT_API OutlineOverlayTask : public PXR_NS::HdxTask
{
public:
    OutlineOverlayTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);

    ~OutlineOverlayTask() override;

    void Prepare(PXR_NS::HdTaskContext* ctx,
                 PXR_NS::HdRenderIndex* renderIndex) override;

    void Execute(PXR_NS::HdTaskContext* ctx) override;

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate,
               PXR_NS::HdTaskContext* ctx,
               PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    void _SetProgram();
    HgiTextureHandle _GetDefaultTexture();
    PXR_NS::TfToken _GetShaderFilePath();

    PXR_NS::HdRenderIndex* _renderIndex;
    std::unique_ptr<class PXR_NS::HdxFullscreenShader> _fullscreenShader;

    OutlineOverlayTaskParams _params;
    HgiTextureHandle _userTexture;
    HgiTextureHandle _defaultTexture;

    OutlineOverlayTask() = delete;
    OutlineOverlayTask(OutlineOverlayTask const&) = delete;
    OutlineOverlayTask& operator=(OutlineOverlayTask const&) = delete;
};

template <class T>
struct HVT_API OutlineOverlayConfig final {};

inline constexpr auto outlineOverlayConfig = OutlineOverlayConfig<OutlineOverlayTaskParams const*>{};

} // namespace HVT_NS
