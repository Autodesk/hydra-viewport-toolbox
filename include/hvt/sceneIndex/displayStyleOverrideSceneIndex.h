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

#include <optional>

#include <hvt/api.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/tf/declarePtrs.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hvt
{

// Forward declarations.
namespace DisplayStyleSceneIndex_Impl
{
struct _StyleInfo;
using _StyleInfoSharedPtr = std::shared_ptr<_StyleInfo>;
} // namespace DisplayStyleSceneIndex_Impl

class DisplayStyleOverrideSceneIndex;
using DisplayStyleOverrideSceneIndexRefPtr = PXR_NS::TfRefPtr<DisplayStyleOverrideSceneIndex>;
using DisplayStyleOverrideSceneIndexConstRefPtr =
    PXR_NS::TfRefPtr<const DisplayStyleOverrideSceneIndex>;

/// \class DisplayStyleOverrideSceneIndex
///
/// A scene index overriding the display style for each prim.
///
class HVT_API DisplayStyleOverrideSceneIndex : public PXR_NS::HdSingleInputFilteringSceneIndexBase
{
public:
    static DisplayStyleOverrideSceneIndexRefPtr New(
        const PXR_NS::HdSceneIndexBaseRefPtr& inputScene);

    /// \name From PXR_NS::HdSceneIndexBase
    /// @{

    PXR_NS::HdSceneIndexPrim GetPrim(const PXR_NS::SdfPath& primPath) const override;

    PXR_NS::SdfPathVector GetChildPrimPaths(const PXR_NS::SdfPath& primPath) const override;

    /// @}

    /// Contains (or not) the refine level value.
    using RefineLevelParams = std::optional<int>;

    /// Sets the refine level (at data source locator displayStyle:refineLevel)
    /// for every prim in the input scene inedx.
    ///
    /// \note If an empty optional value is provided, a null data source will be
    /// returned for the data source locator.
    ///
    void SetRefineLevel(const RefineLevelParams& refineLevel);

protected:
    explicit DisplayStyleOverrideSceneIndex(const PXR_NS::HdSceneIndexBaseRefPtr& inputScene);
    ~DisplayStyleOverrideSceneIndex() override = default;

    /// \name From PXR_NS::HdSingleInputFilteringSceneIndexBase
    /// @{

    void _PrimsAdded(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::AddedPrimEntries& entries) override;

    void _PrimsRemoved(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    void _PrimsDirtied(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

    /// @}

private:
    /// Excludes or not a prim from the display style.
    /// \param primPath The prim to validate.
    /// \return True if the prim is excluded.
    virtual bool _IsExcluded(const PXR_NS::SdfPath& /*primPath*/) const { return false; }

    void _DirtyAllPrims(const PXR_NS::HdDataSourceLocatorSet& locators);

    DisplayStyleSceneIndex_Impl::_StyleInfoSharedPtr const _styleInfo;

    /// Prim overlay data source.
    PXR_NS::HdContainerDataSourceHandle const _overlayDs;
};

} // namespace hvt
