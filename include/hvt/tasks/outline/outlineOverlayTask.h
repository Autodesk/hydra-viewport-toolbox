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

/// Blur kernel used when compositing the outline mask.
enum class BlurMode
{
    /// Composite the mask without blur.
    None,
    /// Composite the mask after a 3x3 blur pass in the overlay shader.
    Blur3x3,
    /// Composite the mask after a 5x5 blur pass in the overlay shader.
    Blur5x5
};

/// Parameters used by OutlineOverlayTask.
struct HVT_API OutlineOverlayTaskParams
{
    /// Constructor.
    OutlineOverlayTaskParams()
        : enabled(false)
        , size{ 0, 0 }
        , screenScale{ 1.0f }
        , blurMode(BlurMode::Blur3x3)
        , blurIntensity(1.0f)
    {
    }

    /// Returns true when both parameter sets describe the same overlay operation.
    bool operator==(OutlineOverlayTaskParams const& other) const
    {
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

    /// Returns true when the parameter sets differ.
    bool operator!=(OutlineOverlayTaskParams const& other) const
    {
        return !(*this == other);
    }

    /// Writes the task parameter values to the stream.
    friend std::ostream& operator<<(std::ostream& out, OutlineOverlayTaskParams const& params)
    {
        out << "OutlineOverlayTask Params: enabled=" << (params.enabled ? "YES" : "NO")
            << ", size=" << params.size[0] << "x" << params.size[1]
            << ", screenScale=" << params.screenScale
            << ", blurMode=" << static_cast<int>(params.blurMode)
            << ", blurIntensity=" << params.blurIntensity;

        return out;
    }

    /// Enables compositing of the outline mask onto the color AOV.
    bool enabled;

    /// Size, in pixels, of the target color AOV and mask texture.
    PXR_NS::GfVec2i size;

    /// Scale factor applied around the UV pivot (1, 0); 1.0 covers the full screen.
    float screenScale;

    /// Blur kernel to apply to the outline mask before compositing.
    BlurMode blurMode;
    /// Multiplier applied to the selected blur kernel radius. (default = 1.0)
    float blurIntensity;

    /// Optional image storage description associated with the overlay input.
    std::shared_ptr<PXR_NS::HioImage::StorageSpec> imageSpec = nullptr;
};

/// A task to composite an outline mask texture onto the color AOV.
class HVT_API OutlineOverlayTask : public PXR_NS::HdxTask
{
public:
    /// Constructor.
    /// \param delegate A task delegate to store input parameters in.
    /// \param id The unique identifier for this task.
    OutlineOverlayTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~OutlineOverlayTask() override;

    /// Prepare task resources and cache the render index for execution.
    /// \param ctx The task context holding the names and resources of the AOVs in use.
    /// \param renderIndex The render index holding scene and render resources.
    void Prepare(PXR_NS::HdTaskContext* ctx,
                 PXR_NS::HdRenderIndex* renderIndex) override;

    /// Composite the outline mask texture into the color AOV.
    /// \param ctx The task context containing the color AOV and optional outline mask texture.
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static PXR_NS::TfToken const& GetToken();

protected:
    /// Synchronize task parameters from the scene delegate.
    /// \param delegate The scene delegate that stores task parameters.
    /// \param ctx The task context for the current task graph execution.
    /// \param dirtyBits Dirty state indicating whether parameters changed.
    void _Sync(PXR_NS::HdSceneDelegate* delegate,
               PXR_NS::HdTaskContext* ctx,
               PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    OutlineOverlayTask() = delete;
    OutlineOverlayTask(OutlineOverlayTask const&) = delete;
    OutlineOverlayTask& operator=(OutlineOverlayTask const&) = delete;

    /// Select and compile the fullscreen overlay shader for the current blur mode.
    void _SetProgram();
    /// Return the fallback outline texture used when no mask texture is available.
    PXR_NS::HgiTextureHandle _GetDefaultTexture();
    /// Returns the path to the outline overlay shader source.
    PXR_NS::TfToken _GetShaderFilePath();

    PXR_NS::HdRenderIndex* _renderIndex;
    std::unique_ptr<class PXR_NS::HdxFullscreenShader> _fullscreenShader;

    OutlineOverlayTaskParams _params;
    PXR_NS::HgiTextureHandle _defaultTexture;
};

} // namespace HVT_NS
