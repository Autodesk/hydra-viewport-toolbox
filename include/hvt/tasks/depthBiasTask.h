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
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hgi/hgi.h>

#include <optional>

// clang-format off
#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
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

struct HVT_API DepthBiasTaskParams
{
    bool depthBiasEnable { false };
    float viewSpaceDepthOffset { 0.0f };  // View-space depth offset in world units
    
    /// View properties; clients should not set these values.
    ViewProperties view;
};

/// A task that implements a depth bias.
/// \note To use when there is a 'z-depth fighting' between two frame passes.
class HVT_API DepthBiasTask : public PXR_NS::HdxTask
{
public:
    DepthBiasTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);
    ~DepthBiasTask() override;

    DepthBiasTask()                                = delete;
    DepthBiasTask(const DepthBiasTask&)            = delete;
    DepthBiasTask& operator=(const DepthBiasTask&) = delete;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

    void _CreateIntermediate(PXR_NS::HgiTextureDesc const& desc, PXR_NS::HgiTextureHandle& texHandle);

private:
    DepthBiasTaskParams _params;
    const PXR_NS::HdCamera* _pCamera = nullptr;
    
    PXR_NS::HgiTextureHandle _depthIntermediate;

    /// Defines the uniforms for the depth bias shader.
    struct Uniforms
    {
        PXR_NS::GfVec2f screenSize;
        float viewSpaceDepthOffset { 0.0f };
        int isOrthographic { 0 };
        PXR_NS::GfVec4f clipInfo;  // {zNear * zFar, zNear - zFar, zFar}
    } _uniforms;

    std::unique_ptr<PXR_NS::HdxFullscreenShader> _shader;
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, const DepthBiasTaskParams& param);
HVT_API bool operator==(const DepthBiasTaskParams& lhs, const DepthBiasTaskParams& rhs);
HVT_API bool operator!=(const DepthBiasTaskParams& lhs, const DepthBiasTaskParams& rhs);
/// @}

} // namespace HVT_NS