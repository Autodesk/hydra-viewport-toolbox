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
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/tf/declarePtrs.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <optional>

namespace HVT_NS
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
/// NOTE: We have found that putting the export symbol (HVT_API) at the class level causes a
/// build failure with certain OpenUSD versions, on subclasses of
/// HdSingleInputFilteringSceneIndexBase. To avoid this, we specify the export symbol on the public
/// functions.
class DisplayStyleOverrideSceneIndex : public PXR_NS::HdSingleInputFilteringSceneIndexBase
{
public:
    HVT_API
    static DisplayStyleOverrideSceneIndexRefPtr New(
        PXR_NS::HdSceneIndexBaseRefPtr const& inputScene);

    /// \name From PXR_NS::HdSceneIndexBase
    /// @{

    HVT_API
    PXR_NS::HdSceneIndexPrim GetPrim(PXR_NS::SdfPath const& primPath) const override;

    HVT_API
    PXR_NS::SdfPathVector GetChildPrimPaths(PXR_NS::SdfPath const& primPath) const override;

    /// @}

    /// Contains (or not) the refine level value.
    using RefineLevelParams = std::optional<int>;

    /// Sets the refine level (at data source locator displayStyle:refineLevel)
    /// for every prim in the input scene inedx.
    ///
    /// \note If an empty optional value is provided, a null data source will be
    /// returned for the data source locator.
    ///
    HVT_API
    void SetRefineLevel(RefineLevelParams const& refineLevel);

protected:
    HVT_API
    explicit DisplayStyleOverrideSceneIndex(PXR_NS::HdSceneIndexBaseRefPtr const& inputScene);

    HVT_API
    ~DisplayStyleOverrideSceneIndex() override = default;

    /// \name From PXR_NS::HdSingleInputFilteringSceneIndexBase
    /// @{

    HVT_API
    void _PrimsAdded(PXR_NS::HdSceneIndexBase const& sender,
        PXR_NS::HdSceneIndexObserver::AddedPrimEntries const& entries) override;

    HVT_API
    void _PrimsRemoved(PXR_NS::HdSceneIndexBase const& sender,
        PXR_NS::HdSceneIndexObserver::RemovedPrimEntries const& entries) override;

    HVT_API
    void _PrimsDirtied(PXR_NS::HdSceneIndexBase const& sender,
        PXR_NS::HdSceneIndexObserver::DirtiedPrimEntries const& entries) override;

    /// @}

private:
    /// Excludes or not a prim from the display style.
    /// \param primPath The prim to validate.
    /// \return True if the prim is excluded.
    HVT_API
    virtual bool _IsExcluded(PXR_NS::SdfPath const& /*primPath*/) const { return false; }

    HVT_API    
    void _DirtyAllPrims(PXR_NS::HdDataSourceLocatorSet const& locators);

    DisplayStyleSceneIndex_Impl::_StyleInfoSharedPtr const _styleInfo;

    /// Prim overlay data source.
    PXR_NS::HdContainerDataSourceHandle const _overlayDs;
};

} // namespace HVT_NS
