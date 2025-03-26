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
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/tf/hashmap.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <functional>
#include <list>
#include <memory>

namespace hvt
{

class SyncDelegate;
using SyncDelegatePtr = std::shared_ptr<SyncDelegate>;

/// The SyncDelegate holds light, RenderBufferDescriptor and task parameters data.
///
/// This SyncDelegate is used in the following manner :
///
/// - The RenderBufferManager uses the SyncDelegate to update RenderBuffer Descriptors Bprims
///   (buffer primitives).
/// - The LightingManager uses the SyncDelegate to update the light Sprims (state primitives).
/// - The TaskManager uses the SyncDelegate to update task parameters, through the TaskCommitFn.
///
class HVT_API SyncDelegate : public PXR_NS::HdSceneDelegate
{
public:
    /// Constructor.
    SyncDelegate(PXR_NS::SdfPath const& uid, PXR_NS::HdRenderIndex* pRenderIndex);

    /// Returns whether the sync delegate has a value with the specified ID and key.
    bool HasValue(PXR_NS::SdfPath const& id, PXR_NS::TfToken const& key) const;

    /// Gets the value from the sync delegate with the specified ID and key, which must exist.
    PXR_NS::VtValue GetValue(PXR_NS::SdfPath const& id, PXR_NS::TfToken const& key) const;

    /// Sets a new value on the sync delegate with the specified ID and key.
    void SetValue(
        PXR_NS::SdfPath const& id, PXR_NS::TfToken const& key, PXR_NS::VtValue const& value);

    /// Outputs the SyncDelegate content, e.g. task parameters.
    friend HVT_API std::ostream& operator<<(std::ostream& out, SyncDelegate const& syncDelegate);

private:
    PXR_NS::VtValue Get(PXR_NS::SdfPath const& id, PXR_NS::TfToken const& key) override;

    /// Get the transform data associated with the RenderIndex primitive id.
    ///
    /// This data is not task data, but separate data associate to a (likely Sprim) primitive.
    /// Example:
    /// light Sprims (e.g. id=/framePass_Main_0_1/light0 or /framePass_UI1_0_2/light1).
    /// It is called by HdEngine::Execute() -> HdRenderIndex::SyncAll() ... -> HdStLight::Sync()
    PXR_NS::GfMatrix4d GetTransform(PXR_NS::SdfPath const& id) override;

    /// Get the light parameter value associated with the RenderIndex light primitive id.
    ///
    /// This data is not a task param, but separate Sprim data, specific to HdStorm.
    ///
    /// This is used by light Sprims (e.g. id=/framePass_Main_0_1/light0).
    /// Typical param name tokens are: intensity, color, exposure, domeOffset, texture, etc.
    /// It is called by HdEngine::Execute() -> HdRenderIndex::SyncAll() ... -> HdStLight::Sync()
    PXR_NS::VtValue GetLightParamValue(
        PXR_NS::SdfPath const& id, PXR_NS::TfToken const& paramName) override;

    /// Get the material resource value associated with the RenderIndex light primitive id.
    ///
    /// This data is not a task param, but separate Sprim data, specific to HdStorm.
    /// This is not used with HdStorm.
    PXR_NS::VtValue GetMaterialResource(PXR_NS::SdfPath const& id) override;

    /// Get the render buffer descriptor associated with the RenderIndex bprim id.
    ///
    /// This data is not a task param, but separate Bprim data.
    ///
    /// This is used by render buffer Bprims (e.g. id=framePass_Main_0_1/{aov_color or aov_depth}).
    /// It is called by HdEngine::Execute() -> HdRenderIndex::SyncAll() -> HdStRenderBuffer::Sync()
    PXR_NS::HdRenderBufferDescriptor GetRenderBufferDescriptor(PXR_NS::SdfPath const& id) override;

    /// Get the render tags associated with the task.
    ///
    /// This data is special, in a way, because it is associated to a specific task id, but it is
    /// not of type HdxRenderTaskParams.
    PXR_NS::TfTokenVector GetTaskRenderTags(PXR_NS::SdfPath const& taskId) override;

    using ValueMapByKey =
        PXR_NS::TfHashMap<PXR_NS::TfToken, PXR_NS::VtValue, PXR_NS::TfToken::HashFunctor>;
    using ValueMap = PXR_NS::TfHashMap<PXR_NS::SdfPath, ValueMapByKey, PXR_NS::SdfPath::Hash>;
    ValueMap _values;
};

} // namespace hvt