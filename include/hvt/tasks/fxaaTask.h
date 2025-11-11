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
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hdx/task.h>

#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

struct HVT_API FXAATaskParams
{
    PXR_NS::GfVec2f pixelToUV = { 0.01f, 0.01f }; // The pixel to UV conversion factor. Should be 1.0 / dimension.
};

/// A task that implements FXAA antialiasing
class HVT_API FXAATask : public PXR_NS::HdxTask
{
public:
    FXAATask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);
    ~FXAATask() override = default;

    FXAATask()                           = delete;
    FXAATask(const FXAATask&)            = delete;
    FXAATask& operator=(const FXAATask&) = delete;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    FXAATaskParams _params;

    std::unique_ptr<PXR_NS::HdxFullscreenShader> _shader;
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, FXAATaskParams const& pv);
HVT_API bool operator==(FXAATaskParams const& lhs, FXAATaskParams const& rhs);
HVT_API bool operator!=(FXAATaskParams const& lhs, FXAATaskParams const& rhs);
/// @}

} // namespace HVT_NS
