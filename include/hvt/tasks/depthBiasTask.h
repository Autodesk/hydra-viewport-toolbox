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

#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hgi/hgi.h>

// clang-format off
#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
{

struct HVT_API DepthBiasTaskParams
{
    bool depthBiasEnable { false };
    float depthBiasConstantFactor { 0.0f };
    float depthBiasSlopeFactor { 1.0f };
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
    
    PXR_NS::HgiTextureHandle _depthIntermediate;

    /// Defines the uniforms for the depth bias shader.
    struct Uniforms
    {
        PXR_NS::GfVec2f screenSize;
        float depthConstFactor { 0.0f };
        float depthSlope { 1.0f };
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