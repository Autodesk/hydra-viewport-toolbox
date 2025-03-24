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

#include <hvt/tasks/ambientOcclusion.h>

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hdx/task.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

#include <optional>

namespace hvt
{

/// Properties related to the camera are being used to render the scene, so that the effect can
/// match the view.
struct HVT_API ViewProperties
{
    PXR_NS::SdfPath cameraID;
    PXR_NS::CameraUtilFraming framing;
    std::optional<PXR_NS::CameraUtilConformWindowPolicy> overrideWindowPolicy;
    PXR_NS::GfVec4d viewport;

    /// Compares the view property values.
    bool operator==(ViewProperties const& other) const
    {
        return cameraID == other.cameraID && framing == other.framing &&
            overrideWindowPolicy == other.overrideWindowPolicy && viewport == other.viewport;
    }

    /// Compares the view property values, and negates.
    bool operator!=(ViewProperties const& other) const { return !(*this == other); }

    /// Writes out the view property values to the stream.
    friend std::ostream& operator<<(std::ostream& out, ViewProperties const& pv)
    {
        out << "View Params: " << pv.cameraID;

        return out;
    }
};

/// Properties for controlling the SSAO effect.
struct HVT_API SSAOTaskParams
{
    /// Ambient occlusion properties.
    AmbientOcclusionProperties ao;

    /// View properties; clients should not set these values.
    ViewProperties view;

    /// Operator overloads needed to support utilities in USD's base/vt/value.h.
    ///@{

    /// Compares the task property values.
    bool operator==(SSAOTaskParams const& other) const
    {
        return ao == other.ao && view == other.view;
    }

    /// Compares the task property values, and negates.
    bool operator!=(SSAOTaskParams const& other) const { return !(*this == other); }

    /// Writes out the task property values to the stream.
    friend std::ostream& operator<<(std::ostream& out, SSAOTaskParams const& pv)
    {
        out << "SSAOTask Params: " << pv.ao << " " << pv.view;

        return out;
    }

    ///@}
};

/// A task that implements screen-space ambient occlusion (SSAO).
class HVT_API SSAOTask : public PXR_NS::HdxTask
{
public:
    // *** Lifetime Management ***

    SSAOTask(PXR_NS::HdSceneDelegate* pDelegate, PXR_NS::SdfPath const& id);
    ~SSAOTask() override;

    SSAOTask()                           = delete;
    SSAOTask(SSAOTask const&)            = delete;
    SSAOTask& operator=(SSAOTask const&) = delete;

protected:
    // *** HdTask and HdxTask Functions ***

    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;
    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;
    void Execute(PXR_NS::HdTaskContext* ctx) override;

private:
    // A structure for storing uniform data for the raw shader, with a comparison operator.
    // NOTE: There are special cases defined by the OpenGL std430 layout rules, e.g. a vec3 uniform
    // must use the vec4 (four floats) storage for proper layout.
    struct RawUniforms
    {
        PXR_NS::GfVec4f clipInfo;        // 16 bytes
        PXR_NS::GfVec4f projInfo;        // 16 bytes
        PXR_NS::GfVec2i screenSize;      // 8 bytes
        float amount             = 1.0f; // 4 bytes
        float sampleRadius       = 1.0f; // 4 bytes
        int isScreenSampleRadius = -1;   // 4 bytes: int storage used for bool uniform
        int sampleCount          = 1;    // 4 bytes
        int spiralTurnCount      = 1;    // 4 bytes
        int isBlurEnabled        = -1;   // 4 bytes: int storage used for bool uniform
        int isOrthographic       = -1;   // 4 bytes: int storage used for bool uniform

        bool operator!=(RawUniforms const& other) const
        {
            return clipInfo != other.clipInfo || projInfo != other.projInfo ||
                screenSize != other.screenSize || amount != other.amount ||
                sampleRadius != other.sampleRadius ||
                isScreenSampleRadius != other.isScreenSampleRadius ||
                sampleCount != other.sampleCount || spiralTurnCount != other.spiralTurnCount ||
                isBlurEnabled != other.isBlurEnabled || isOrthographic != other.isOrthographic;
        }
    };

    // A structure for storing uniform data for the blur shader, with a comparison operator.
    struct BlurUniforms
    {
        PXR_NS::GfVec2i screenSize; // 8 bytes
        PXR_NS::GfVec2i offset;     // 8 bytes
        float edgeSharpness = 1.0;  // 4 bytes

        bool operator!=(const BlurUniforms& other) const
        {
            return screenSize != other.screenSize || offset != other.offset ||
                edgeSharpness != other.edgeSharpness;
        }
    };

    // A structure for storing uniform data for the composite shader, with a comparison operator.
    struct CompositeUniforms
    {
        PXR_NS::GfVec2i screenSize; // 8 bytes
        int isShowOnlyEnabled = -1; // 4 bytes: int storage used for bool uniform

        bool operator!=(CompositeUniforms const& other) const
        {
            return screenSize != other.screenSize || isShowOnlyEnabled != other.isShowOnlyEnabled;
        }
    };

    SSAOTaskParams _params;
    const PXR_NS::HdCamera* _pCamera = nullptr;
    RawUniforms _rawUniforms;
    BlurUniforms _blurUniforms;
    CompositeUniforms _compositeUniforms;
    PXR_NS::TfToken _shaderPath;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _pRawShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _pBlurShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _pCompositeShader;
    PXR_NS::HgiTextureHandle _aoTexture1, _aoTexture2;
    PXR_NS::GfVec2i _dimensions;

    void InitTextures(PXR_NS::GfVec2i const& dimensions);
    void InitRawShader();
    void InitBlurShader();
    void InitCompositeShader();
    void ExecuteRawPass(
        PXR_NS::HgiTextureHandle const& inDepthTexture, PXR_NS::HgiTextureHandle const& outTexture);
    void ExecuteBlurPass(PXR_NS::HgiTextureHandle const& inAOTexture,
        PXR_NS::HgiTextureHandle const& outTexture, PXR_NS::GfVec2i const& offset);
    void ExecuteCompositePass(PXR_NS::HgiTextureHandle const& inColorTexture,
        PXR_NS::HgiTextureHandle const& inAOTexture, PXR_NS::HgiTextureHandle const& outTexture);
};

} // namespace hvt
