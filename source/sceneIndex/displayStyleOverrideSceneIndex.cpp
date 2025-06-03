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

#include <hvt/sceneIndex/displayStyleOverrideSceneIndex.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#endif
// clang-format on

#include <pxr/imaging/hd/containerDataSourceEditor.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/legacyDisplayStyleSchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPrimView.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace DisplayStyleSceneIndex_Impl
{

using OptionalValue = DisplayStyleOverrideSceneIndex::RefineLevelParams;

struct _StyleInfo
{
    OptionalValue refineLevel;
    /// Retained data source storing refineLevel (or null ptr if empty optional
    /// value) to avoid allocating a data source for every prim.
    HdDataSourceBaseHandle refineLevelDs;
};

/// Data source for locator displayStyle.
class _DisplayStyleDataSource final : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_DisplayStyleDataSource)

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdLegacyDisplayStyleSchemaTokens->refineLevel)
        {
            return _styleInfo->refineLevelDs;
        }
        return nullptr;
    }

    TfTokenVector GetNames() override
    {
        static const TfTokenVector names = { HdLegacyDisplayStyleSchemaTokens->refineLevel };

        return names;
    }

private:
    explicit _DisplayStyleDataSource(_StyleInfoSharedPtr const& styleInfo) : _styleInfo(styleInfo)
    {
    }

    _StyleInfoSharedPtr _styleInfo;
};

} // namespace DisplayStyleSceneIndex_Impl

DisplayStyleOverrideSceneIndexRefPtr DisplayStyleOverrideSceneIndex::New(
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    return TfCreateRefPtr(new DisplayStyleOverrideSceneIndex(inputSceneIndex));
}

DisplayStyleOverrideSceneIndex::DisplayStyleOverrideSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex) :
    HdSingleInputFilteringSceneIndexBase(inputSceneIndex),
    _styleInfo(std::make_shared<DisplayStyleSceneIndex_Impl::_StyleInfo>()),
    _overlayDs(HdRetainedContainerDataSource::New(HdLegacyDisplayStyleSchemaTokens->displayStyle,
        DisplayStyleSceneIndex_Impl::_DisplayStyleDataSource::New(_styleInfo)))
{
}

HdSceneIndexPrim DisplayStyleOverrideSceneIndex::GetPrim(const SdfPath& primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (prim.dataSource && prim.primType == HdPrimTypeTokens->mesh && !_IsExcluded(primPath))
    {
        prim.dataSource = HdOverlayContainerDataSource::New(_overlayDs, prim.dataSource);
    }
    return prim;
}

SdfPathVector DisplayStyleOverrideSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void DisplayStyleOverrideSceneIndex::SetRefineLevel(const RefineLevelParams& refineLevel)
{
    if (refineLevel == _styleInfo->refineLevel)
    {
        return;
    }

    _styleInfo->refineLevel = refineLevel;
    _styleInfo->refineLevelDs =
        refineLevel ? HdRetainedTypedSampledDataSource<int>::New(*refineLevel) : nullptr;

    static const HdDataSourceLocatorSet locators(
        HdLegacyDisplayStyleSchema::GetDefaultLocator().Append(
            HdLegacyDisplayStyleSchemaTokens->refineLevel));

    _DirtyAllPrims(locators);
}

void DisplayStyleOverrideSceneIndex::_DirtyAllPrims(const HdDataSourceLocatorSet& locators)
{
    if (!_IsObserved())
    {
        return;
    }

    HdSceneIndexObserver::DirtiedPrimEntries entries;
    for (const SdfPath& path : HdSceneIndexPrimView(_GetInputSceneIndex()))
    {
        entries.push_back({ path, locators });
    }

    _SendPrimsDirtied(entries);
}

void DisplayStyleOverrideSceneIndex::_PrimsAdded(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsAdded(entries);
}

void DisplayStyleOverrideSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsRemoved(entries);
}

void DisplayStyleOverrideSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsDirtied(entries);
}

} // namespace HVT_NS
