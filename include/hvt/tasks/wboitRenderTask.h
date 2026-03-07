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

#include <pxr/imaging/hdSt/renderBuffer.h>
#include <pxr/imaging/hdx/renderTask.h>

#if __clang__
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <memory>
#include <vector>

namespace HVT_NS
{

/// A task for rendering transparent geometry using Weighted Blended Order-Independent
/// Transparency (McGuire & Bavoil, 2013 -- https://jcgt.org/published/0002/02/09/).
/// Its companion task, WbOitResolveTask, blends the accumulation buffers to screen.
class HVT_API WbOitRenderTask : public PXR_NS::HdxRenderTask
{
public:
    WbOitRenderTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~WbOitRenderTask() override;

    WbOitRenderTask()                                = delete;
    WbOitRenderTask(const WbOitRenderTask&)          = delete;
    WbOitRenderTask& operator=(const WbOitRenderTask&) = delete;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Returns the associated token.
    static const PXR_NS::TfToken& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    bool _InitTextures(PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdRenderPassStateSharedPtr const& renderPassState);

    std::shared_ptr<PXR_NS::HdStRenderPassShader> _renderPassShader;
    PXR_NS::HdRenderPassAovBindingVector _wboitAovBindings;
    std::vector<std::unique_ptr<PXR_NS::HdStRenderBuffer>> _wboitBuffers;

    PXR_NS::HdRenderIndex* _renderIndex { nullptr };
};

} // namespace HVT_NS
