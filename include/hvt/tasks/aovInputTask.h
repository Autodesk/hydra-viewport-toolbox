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
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hgi/texture.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

/// A task for taking input AOV data comming from a render buffer that was
/// filled by render tasks and converting it to a HgiTexture.
/// The aov render buffer can be a GPU or CPU buffer, while the resulting output
/// HgiTexture will always be a GPU texture.
///
/// The HgiTexture is placed in the shared task context so that following tasks
/// maybe operate on this HgiTexture without having to worry about converting
/// the aov data from CPU to GPU.
///
class HVT_API AovInputTask : public PXR_NS::HdxTask
{
public:
    AovInputTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~AovInputTask() override;

    AovInputTask()                               = delete;
    AovInputTask(const AovInputTask&)            = delete;
    AovInputTask& operator=(const AovInputTask&) = delete;

    /// Hooks for progressive rendering.
    bool IsConverged() const override;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;

    void Execute(PXR_NS::HdTaskContext* ctx) override;

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    void _UpdateTexture(PXR_NS::HdTaskContext* ctx, PXR_NS::HgiTextureHandle& texture,
        PXR_NS::HdRenderBuffer* buffer, PXR_NS::HgiTextureUsageBits usage);

    void _UpdateIntermediateTexture(
        PXR_NS::HgiTextureHandle& texture, PXR_NS::HdRenderBuffer* buffer);

    bool _converged;

    PXR_NS::HdRenderBuffer* _aovBuffer { nullptr };
    PXR_NS::HdRenderBuffer* _depthBuffer { nullptr };

    PXR_NS::HgiTextureHandle _aovTexture;
    PXR_NS::HgiTextureHandle _depthTexture;
    PXR_NS::HgiTextureHandle _aovTextureIntermediate;
};

/// AovInput parameters.
struct HVT_API AovInputTaskParams
{
    PXR_NS::SdfPath aovBufferPath;
    PXR_NS::SdfPath depthBufferPath;
    PXR_NS::HdRenderBuffer* aovBuffer { nullptr};
    PXR_NS::HdRenderBuffer* depthBuffer { nullptr};
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, AovInputTaskParams const& pv);
HVT_API bool operator==(AovInputTaskParams const& lhs, AovInputTaskParams const& rhs);
HVT_API bool operator!=(AovInputTaskParams const& lhs, AovInputTaskParams const& rhs);
/// @}

} // namespace HVT_NS
