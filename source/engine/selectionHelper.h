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

#include <hvt/engine/selectionSettingsProvider.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <memory>

namespace hvt
{

using SelectionHelperPtr = std::shared_ptr<class SelectionHelper>;

/// A helper class that holds selection and picking related data.
///
/// This helper class is used both as a storage location and as an accessor for tasks to pull their
/// settings data through the SelectionSettingsProvider interface.
/// It also acts as a helper to validate data and can hold other entities responsible for
/// picking and selection.
class SelectionHelper : public SelectionSettingsProvider
{
public:
    SelectionHelper(PXR_NS::SdfPath const& taskManagerUid);

    /// Destructor.
    ~SelectionHelper() override = default;

    /// Sets the selection.
    virtual void SetSelection(PXR_NS::HdSelectionSharedPtr selection);

    /// Sets the buffer paths for use with the selection and picking tasks.
    virtual void SetVisualizeAOV(PXR_NS::TfToken const& name);

    /// Sets the selection state in the task context data.
    virtual void SetSelectionContextData(PXR_NS::HdEngine* engine);

    /// Gets the paths to the selection buffers.
    /// @{
    SelectionBufferPaths const& GetBufferPaths() const override;
    virtual SelectionBufferPaths& GetBufferPaths();
    /// @}

    /// Gets the selection settings shared by multiple tasks.
    /// @{
    SelectionSettings const& GetSettings() const override;
    virtual SelectionSettings& GetSettings();
    /// @}

    /// Gets the SelectionTracker.
    /// @{
    PXR_NS::HdxSelectionTrackerSharedPtr const& GetSelectionTracker() const;
    PXR_NS::HdxSelectionTrackerSharedPtr GetSelectionTracker();
    /// @}

protected:
    /// The parent Id used to construct the name of selection buffers.
    const PXR_NS::SdfPath _taskManagerUid;

    /// The selection tracker.
    PXR_NS::HdxSelectionTrackerSharedPtr _selectionTracker;

    /// Selection settings shared by picking and selection tasks.
    SelectionSettings _settings;

    /// Used by HdxColorizeSelectionTaskParams / HdxPickFromRenderBufferTaskParams
    SelectionBufferPaths _bufferPaths;
};

} // namespace hvt