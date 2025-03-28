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

#include <hvt/sceneIndex/boundingBoxSceneIndex.h>

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

#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/basisCurvesTopologySchema.h>
#include <pxr/imaging/hd/extentSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/legacyDisplayStyleSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primOriginSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/purposeSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/pxr.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

TfTokenVector _Concat(const TfTokenVector& a, const TfTokenVector& b)
{
    TfTokenVector result;
    result.reserve(a.size() + b.size());
    result.insert(result.end(), a.begin(), a.end());
    result.insert(result.end(), b.begin(), b.end());
    return result;
}

class PrimvarDataSource final : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(PrimvarDataSource);

    TfTokenVector GetNames() override
    {
        return { HdPrimvarSchemaTokens->primvarValue, HdPrimvarSchemaTokens->interpolation,
            HdPrimvarSchemaTokens->role };
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdPrimvarSchemaTokens->primvarValue)
        {
            return _primvarValueSrc;
        }
        else if (name == HdPrimvarSchemaTokens->interpolation)
        {
            return HdPrimvarSchema::BuildInterpolationDataSource(_interpolation);
        }
        else if (name == HdPrimvarSchemaTokens->role)
        {
            return HdPrimvarSchema::BuildRoleDataSource(_role);
        }

        return nullptr;
    }

protected:
    PrimvarDataSource(const HdDataSourceBaseHandle& primvarValueSrc, const TfToken& interpolation,
        const TfToken& role) :
        _primvarValueSrc(primvarValueSrc), _interpolation(interpolation), _role(role)
    {
    }

private:
    HdDataSourceBaseHandle _primvarValueSrc;
    TfToken _interpolation;
    TfToken _role;
};

/// \brief Base class for container data sources providing primvars.
///
/// Provides primvars common to bounding boxes display:
/// - displayColor (computed by querying displayColor from the prim data source).
///
class _PrimvarsDataSource : public HdContainerDataSource
{
public:
    TfTokenVector GetNames() override { return { HdTokens->displayColor }; }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdTokens->displayColor)
        {
            return BuildPrimvarDS(VtValue(VtVec3fArray { { _wireframeColor[0],
                                                      _wireframeColor[1], _wireframeColor[2] } }),
                HdPrimvarSchemaTokens->vertex, HdPrimvarRoleTokens->color);
        }
        return nullptr;
    }

protected:
    _PrimvarsDataSource(
        const HdContainerDataSourceHandle& primSource, const GfVec4f& wireframeColor) :
        _primSource(primSource), _wireframeColor(wireframeColor)
    {
    }

    HdContainerDataSourceHandle _primSource;
    GfVec4f _wireframeColor;
};

/// \brief Base class for prim data sources.
///
/// Provides:
/// - xform (from the given prim data source)
/// - purpose (from the given prim data source)
/// - visibility (from the given prim data source)
/// - displayStyle (constant)
/// - instancedBy
/// - primOrigin (for selection picking to work on usd prims in bounding box display mode)
///
class _PrimDataSource : public HdContainerDataSource
{
public:
    TfTokenVector GetNames() override
    {
        return { HdXformSchemaTokens->xform, HdPurposeSchemaTokens->purpose,
            HdVisibilitySchemaTokens->visibility, HdInstancedBySchemaTokens->instancedBy,
            HdLegacyDisplayStyleSchemaTokens->displayStyle, HdPrimOriginSchemaTokens->primOrigin };
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdXformSchemaTokens->xform || name == HdPurposeSchemaTokens->purpose ||
            name == HdVisibilitySchemaTokens->visibility ||
            name == HdInstancedBySchemaTokens->instancedBy ||
            name == HdPrimOriginSchemaTokens->primOrigin)
        {
            if (_primSource)
            {
                return _primSource->Get(name);
            }
            return nullptr;
        }
        else if (name == HdLegacyDisplayStyleSchemaTokens->displayStyle)
        {
            static const HdDataSourceBaseHandle src =
                HdLegacyDisplayStyleSchema::Builder()
                    .SetCullStyle(HdRetainedTypedSampledDataSource<TfToken>::New(
                        HdCullStyleTokens->nothing)) // No culling
                    .Build();
            return src;
        }
        return nullptr;
    }

protected:
    explicit _PrimDataSource(const HdContainerDataSourceHandle& primSource) :
        _primSource(primSource)
    {
    }

    HdContainerDataSourceHandle _primSource;
};

/// \brief Data source for primvars:points:primvarValue
///
/// Computes 8 vertices of a box determined by extent of a given prim data source.
///
class _BoundsPointsPrimvarValueDataSource final : public HdVec3fArrayDataSource
{
public:
    HD_DECLARE_DATASOURCE(_BoundsPointsPrimvarValueDataSource);

    VtValue GetValue(HdSampledDataSource::Time shutterOffset) override
    {
        return VtValue(GetTypedValue(shutterOffset));
    }

    VtVec3fArray GetTypedValue(HdSampledDataSource::Time shutterOffset) override
    {
        // Get extent from given prim source.
        HdExtentSchema extentSchema = HdExtentSchema::GetFromParent(_primSource);

        GfVec3f exts[2]     = { GfVec3f(0.0f), GfVec3f(0.0f) };
        bool extentMinFound = false;
        if (HdVec3dDataSourceHandle src = extentSchema.GetMin())
        {
            exts[0]        = GfVec3f(src->GetTypedValue(shutterOffset));
            extentMinFound = true;
        }
        bool extentMaxFound = false;
        if (HdVec3dDataSourceHandle src = extentSchema.GetMax())
        {
            exts[1]        = GfVec3f(src->GetTypedValue(shutterOffset));
            extentMaxFound = true;
        }

        if (!extentMinFound || !extentMaxFound)
        {
            // If extent is not given, no bounding box will be displayed
            return VtVec3fArray();
        }

        /// Compute 8 points on box.
        VtVec3fArray pts(8);
        size_t i = 0;
        for (size_t j0 = 0; j0 < 2; j0++)
        {
            for (size_t j1 = 0; j1 < 2; j1++)
            {
                for (size_t j2 = 0; j2 < 2; j2++)
                {
                    pts[i] = { exts[j0][0], exts[j1][1], exts[j2][2] };
                    ++i;
                }
            }
        }

        return pts;
    }

    bool GetContributingSampleTimesForInterval(HdSampledDataSource::Time startTime,
        HdSampledDataSource::Time endTime,
        std::vector<HdSampledDataSource::Time>* outSampleTimes) override
    {
        HdExtentSchema extentSchema = HdExtentSchema::GetFromParent(_primSource);

        HdSampledDataSourceHandle srcs[] = { extentSchema.GetMin(), extentSchema.GetMax() };

        return HdGetMergedContributingSampleTimesForInterval(
            TfArraySize(srcs), srcs, startTime, endTime, outSampleTimes);
    }

private:
    explicit _BoundsPointsPrimvarValueDataSource(const HdContainerDataSourceHandle& primSource) :
        _primSource(primSource)
    {
    }

    HdContainerDataSourceHandle _primSource;
};

/// Data source for primvars.
///
/// Provides (on top of the base class):
/// - points (using the above data source)
///
class _BoundsPrimvarsDataSource final : public _PrimvarsDataSource
{
public:
    HD_DECLARE_DATASOURCE(_BoundsPrimvarsDataSource)

    TfTokenVector GetNames() override
    {
        static const TfTokenVector result =
            _Concat(_PrimvarsDataSource::GetNames(), { HdPrimvarsSchemaTokens->points });
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdPrimvarsSchemaTokens->points)
        {
            return PrimvarDataSource::New(_BoundsPointsPrimvarValueDataSource::New(_primSource),
                HdPrimvarSchemaTokens->vertex, HdPrimvarSchemaTokens->point);
        }
        return _PrimvarsDataSource::Get(name);
    }

private:
    _BoundsPrimvarsDataSource(
        const HdContainerDataSourceHandle& primSource, const GfVec4f& wireframeColor) :
        _PrimvarsDataSource(primSource, wireframeColor)
    {
    }
};

HdContainerDataSourceHandle _ComputeBoundsTopology()
{
    // Is for a bounding box
    //  Segments: CCW bottom face starting at (-x, -y, -z)
    //            CCW top face starting at (-x, -y, z)
    //            CCW vertical edges, starting at (-x, -y)
    const VtIntArray curveIndices { /* bottom face */ 0, 4, 4, 6, 6, 2, 2, 0,
        /* top face */ 1, 5, 5, 7, 7, 3, 3, 1,
        /* edge pairs */ 0, 1, 4, 5, 6, 7, 2, 3 };
    const VtIntArray curveVertexCounts { static_cast<int>(curveIndices.size()) };

    return HdBasisCurvesTopologySchema::Builder()
        .SetCurveVertexCounts(HdRetainedTypedSampledDataSource<VtIntArray>::New(curveVertexCounts))
        .SetCurveIndices(HdRetainedTypedSampledDataSource<VtIntArray>::New(curveIndices))
        .SetBasis(HdRetainedTypedSampledDataSource<TfToken>::New(HdTokens->bezier))
        .SetType(HdRetainedTypedSampledDataSource<TfToken>::New(HdTokens->linear))
        .SetWrap(HdRetainedTypedSampledDataSource<TfToken>::New(HdTokens->segmented))
        .Build();
}

/// Prim data source.
///
/// Provides (on top of the base class):
/// - basisCurves (constant using above topology)
/// - primvars (using above data source)
/// - extent (from the original prim source)
///
class _BoundsPrimDataSource : public _PrimDataSource
{
public:
    HD_DECLARE_DATASOURCE(_BoundsPrimDataSource)

    TfTokenVector GetNames() override
    {
        static const TfTokenVector result = _Concat(_PrimDataSource::GetNames(),
            { HdBasisCurvesSchemaTokens->basisCurves, HdPrimvarsSchemaTokens->primvars,
                HdExtentSchemaTokens->extent });
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdBasisCurvesSchemaTokens->basisCurves)
        {
            static const HdDataSourceBaseHandle basisCurvesSrc =
                HdBasisCurvesSchema::Builder().SetTopology(_ComputeBoundsTopology()).Build();
            return basisCurvesSrc;
        }
        else if (name == HdPrimvarsSchemaTokens->primvars)
        {
            return _BoundsPrimvarsDataSource::New(_primSource, _wireframeColor);
        }
        else if (name == HdExtentSchemaTokens->extent)
        {
            return _primSource ? _primSource->Get(name) : nullptr;
        }
        return _PrimDataSource::Get(name);
    }

private:
    _BoundsPrimDataSource(
        const HdContainerDataSourceHandle& primSource, const GfVec4f& wireframeColor) :
        _PrimDataSource(primSource), _wireframeColor(wireframeColor)
    {
    }

    GfVec4f _wireframeColor;
};

BoundingBoxSceneIndex::BoundingBoxSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex) :
    HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
}

HdSceneIndexPrim BoundingBoxSceneIndex::GetPrim(const SdfPath& primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (prim.dataSource && !_IsExcluded(primPath) &&
        ((prim.primType == HdPrimTypeTokens->mesh) ||
            (prim.primType == HdPrimTypeTokens->basisCurves)))
    {
        // Converts mesh to basisCurve type for displaying a bounding box.
        prim.primType = HdPrimTypeTokens->basisCurves;
        // Builds the new datasource.
        prim.dataSource = _BoundsPrimDataSource::New(prim.dataSource, _GetColor());
    }

    return prim;
}
SdfPathVector BoundingBoxSceneIndex::GetChildPrimPaths(const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void BoundingBoxSceneIndex::_PrimsAdded(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::AddedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    HdSceneIndexObserver::AddedPrimEntries newEntries;

    for (const HdSceneIndexObserver::AddedPrimEntry& entry : entries)
    {
        const SdfPath& path   = entry.primPath;
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(path);
        if (prim.primType == HdPrimTypeTokens->mesh)
        {
            // Converts meshes to basisCurve to display a bounding box.
            newEntries.push_back({ path, HdPrimTypeTokens->basisCurves });
        }
        else
        {
            newEntries.push_back(entry);
        }
    }

    _SendPrimsAdded(newEntries);
}

void BoundingBoxSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsRemoved(entries);
}

void BoundingBoxSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase& /*sender*/, const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    if (!_IsObserved())
    {
        return;
    }

    _SendPrimsDirtied(entries);
}

} // namespace hvt
