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
#endif
// clang-format on

#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/pxr.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// clang-format on

namespace HVT_NS
{

/// Clear buffer task params.
struct HVT_API ClearBufferTaskParams
{
    ClearBufferTaskParams() {}

    /// Compares the task property values.
    bool operator==(ClearBufferTaskParams const& other) const
    {
        return clearColor == other.clearColor && 
               clearDepth == other.clearDepth && 
               aovBindings == other.aovBindings;
    }

    /// Compares the task property values, and negates.
    bool operator!=(ClearBufferTaskParams const& other) const { return !(*this == other); }

    PXR_NS::GfVec4f clearColor { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearDepth { 1.0f };
    PXR_NS::HdRenderPassAovBindingVector aovBindings;
};

/// \class ClearBufferTask
///
/// A simple task that clears the color and depth AOV buffers.
/// This task can be used at the beginning of a render pass to ensure
/// buffers are in a known state.
class HVT_API ClearBufferTask : public PXR_NS::HdTask
{
public:
    ClearBufferTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);

    ~ClearBufferTask() override;

    static PXR_NS::TfToken const& GetToken();

    /// Sync the task resources
    void Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

    /// Prepare the task
    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;

    /// Execute the task - performs the actual clear operation
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    PXR_NS::TfTokenVector const& GetRenderTags() const override;

private:
    ClearBufferTaskParams _params;
    PXR_NS::TfTokenVector _renderTags;
    PXR_NS::HdRenderIndex* _index;
    PXR_NS::Hgi* _hgi;

    ClearBufferTask()                                      = delete;
    ClearBufferTask(const ClearBufferTask&)                = delete;
    ClearBufferTask& operator=(const ClearBufferTask&)     = delete;
};

} // namespace HVT_NS
