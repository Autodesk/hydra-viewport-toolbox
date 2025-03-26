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
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hdx/task.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <memory>

namespace hvt
{

/// The parameters for the compose task holding information of the source render texture.
struct HVT_API ComposeTaskParams
{
    /// The render texture token.
    PXR_NS::TfToken aovToken;
    /// The corresponding render texture handle.
    PXR_NS::HgiTextureHandle aovTextureHandle;

    /// Operator overloads.
    /// @{

    /// Writes out the task property values to the stream.
    friend std::ostream& operator<<(std::ostream& out, const ComposeTaskParams& pv);

    /// Compares the task property values.
    bool operator==(const ComposeTaskParams& rhs) const;
    /// Compares the task property values.
    bool operator!=(const ComposeTaskParams& rhs) const;

    /// @}
};

/// The task composes a source color render texture with the current color render texture.
///
/// \note The composition is the current color texture on top of the source color texture, with
/// standard alpha blending, and the result is stored in the current color texture. The source color
/// texture is *not* modified.
class HVT_API ComposeTask : public PXR_NS::HdxTask
{
public:
    explicit ComposeTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);

    ComposeTask(const ComposeTask&)            = delete;
    ComposeTask& operator=(const ComposeTask&) = delete;
    ~ComposeTask() override                    = default;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    ComposeTaskParams _params;

    std::unique_ptr<PXR_NS::HdxFullscreenShader> _shader;
};

} // namespace hvt