// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <hvt/sceneIndex/wireFrameSceneIndex.h>

#include <hvt/geometry/geometry.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
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
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

// Handle primsvars:overrideWireframeColor in Storm for wireframe color.
TF_DEFINE_PRIVATE_TOKENS(_primVarsTokens,

    (overrideWireframeColor) // Works in HdStorm to override the wireframe color.
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

const HdRetainedContainerDataSourceHandle refinedWireDisplayStyleDataSource =
    HdRetainedContainerDataSource::New(HdLegacyDisplayStyleSchemaTokens->displayStyle,
        HdRetainedContainerDataSource::New(HdLegacyDisplayStyleSchemaTokens->reprSelector,
            HdRetainedTypedSampledDataSource<VtArray<TfToken>>::New(
                { HdReprTokens->refinedWire, TfToken(), TfToken() })));

const HdDataSourceLocator primvarsOverrideWireframeColorLocator(
    HdPrimvarsSchema::GetDefaultLocator().Append(_primVarsTokens->overrideWireframeColor));

WireFrameSceneIndex::WireFrameSceneIndex(const HdSceneIndexBaseRefPtr& inputScene) :
    HdSingleInputFilteringSceneIndexBase(inputScene)
{
}

HdSceneIndexPrim WireFrameSceneIndex::GetPrim(const SdfPath& primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (_IsExcluded(primPath))
    {
        return prim;
    }

    if (prim.primType != HdPrimTypeTokens->mesh || !prim.dataSource)
    {
        return prim;
    }

    auto edited = HdContainerDataSourceEditor(prim.dataSource);

    edited.Set(primvarsOverrideWireframeColorLocator,
        BuildPrimvarDS(VtValue(VtVec4fArray { _GetColor() }), HdPrimvarSchemaTokens->constant,
            HdPrimvarSchemaTokens->color));

    prim.dataSource =
        HdOverlayContainerDataSource::New({ edited.Finish(), refinedWireDisplayStyleDataSource });

    return prim;
}

SdfPathVector WireFrameSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void WireFrameSceneIndex::_PrimsAdded(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsAdded(entries);
}

void WireFrameSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsRemoved(entries);
}

void WireFrameSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsDirtied(entries);
}

} // namespace hvt
