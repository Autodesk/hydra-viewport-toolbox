// Copyright 2026 Autodesk, Inc.
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

#include <pxr/pxr.h>

#include <pxr/base/gf/vec3d.h>
#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/extentSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// The BoundingBoxSceneIndex converts meshes/basisCurves to a synthetic wireframe box. These tests
// exercise its notice plumbing directly (no GPU/rendering): that _PrimsAdded announces the same
// prim type GetPrim actually returns, and that dirtying `extent` also dirties `primvars:points`.

namespace
{

/// Records the notices emitted by the scene index it observes.
class RecordingObserver : public HdSceneIndexObserver
{
public:
    AddedPrimEntries added;
    RemovedPrimEntries removed;
    DirtiedPrimEntries dirtied;

    void PrimsAdded(HdSceneIndexBase const& /*sender*/, AddedPrimEntries const& entries) override
    {
        added.insert(added.end(), entries.begin(), entries.end());
    }
    void PrimsRemoved(
        HdSceneIndexBase const& /*sender*/, RemovedPrimEntries const& entries) override
    {
        removed.insert(removed.end(), entries.begin(), entries.end());
    }
    void PrimsDirtied(
        HdSceneIndexBase const& /*sender*/, DirtiedPrimEntries const& entries) override
    {
        dirtied.insert(dirtied.end(), entries.begin(), entries.end());
    }
    void PrimsRenamed(HdSceneIndexBase const& sender, RenamedPrimEntries const& entries) override
    {
        ConvertPrimsRenamedToRemovedAndAdded(sender, entries, &removed, &added);
    }

    void Clear()
    {
        added.clear();
        removed.clear();
        dirtied.clear();
    }
};

/// Builds a minimal prim data source carrying only an extent (enough for the bounding box).
HdContainerDataSourceHandle _MakePrimSourceWithExtent(GfVec3d const& mn, GfVec3d const& mx)
{
    HdContainerDataSourceHandle extentDs =
        HdExtentSchema::Builder()
            .SetMin(HdRetainedTypedSampledDataSource<GfVec3d>::New(mn))
            .SetMax(HdRetainedTypedSampledDataSource<GfVec3d>::New(mx))
            .Build();
    return HdRetainedContainerDataSource::New(HdExtentSchemaTokens->extent, extentDs);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// _PrimsAdded announces the same type GetPrim converts to.
// ---------------------------------------------------------------------------

TEST(TestBoundingBoxSceneIndex, MeshAnnouncedAsBasisCurvesMatchesGetPrim)
{
    auto input = HdRetainedSceneIndex::New();
    auto bbox  = hvt::BoundingBoxSceneIndex::New(input);

    RecordingObserver obs;
    bbox->AddObserver(HdSceneIndexObserverPtr(&obs));

    const SdfPath meshPath("/mesh");
    input->AddPrims({ { meshPath, HdPrimTypeTokens->mesh,
        _MakePrimSourceWithExtent(GfVec3d(-1), GfVec3d(1)) } });

    // The added notice must announce basisCurves (the type GetPrim converts a mesh to).
    ASSERT_EQ(obs.added.size(), 1u);
    EXPECT_EQ(obs.added[0].primPath, meshPath);
    EXPECT_EQ(obs.added[0].primType, HdPrimTypeTokens->basisCurves);

    // GetPrim must agree with the announced type...
    HdSceneIndexPrim prim = bbox->GetPrim(meshPath);
    EXPECT_EQ(prim.primType, HdPrimTypeTokens->basisCurves);
    ASSERT_TRUE(prim.dataSource);

    // ...and provide real basisCurves topology + points, so the announced type is not dangling.
    HdBasisCurvesSchema curves = HdBasisCurvesSchema::GetFromParent(prim.dataSource);
    EXPECT_TRUE(curves.GetTopology().IsDefined());
    HdPrimvarsSchema primvars = HdPrimvarsSchema::GetFromParent(prim.dataSource);
    EXPECT_TRUE(primvars.GetPrimvar(HdPrimvarsSchemaTokens->points).IsDefined());
}

TEST(TestBoundingBoxSceneIndex, NonConvertiblePrimForwardedUnchanged)
{
    auto input = HdRetainedSceneIndex::New();
    auto bbox  = hvt::BoundingBoxSceneIndex::New(input);

    RecordingObserver obs;
    bbox->AddObserver(HdSceneIndexObserverPtr(&obs));

    static const TfToken kScope("scope");
    const SdfPath path("/scope");
    input->AddPrims({ { path, kScope, _MakePrimSourceWithExtent(GfVec3d(-1), GfVec3d(1)) } });

    // A non-mesh/non-basisCurves prim (even with a valid data source) must pass through unchanged.
    ASSERT_EQ(obs.added.size(), 1u);
    EXPECT_EQ(obs.added[0].primType, kScope);
    EXPECT_EQ(bbox->GetPrim(path).primType, kScope);
}

// ---------------------------------------------------------------------------
// _PrimsDirtied remaps `extent` invalidation to also invalidate `primvars:points`.
// ---------------------------------------------------------------------------

TEST(TestBoundingBoxSceneIndex, DirtyingExtentAlsoDirtiesPoints)
{
    auto input = HdRetainedSceneIndex::New();
    auto bbox  = hvt::BoundingBoxSceneIndex::New(input);

    RecordingObserver obs;
    bbox->AddObserver(HdSceneIndexObserverPtr(&obs));

    const SdfPath meshPath("/mesh");
    input->AddPrims({ { meshPath, HdPrimTypeTokens->mesh,
        _MakePrimSourceWithExtent(GfVec3d(-1), GfVec3d(1)) } });
    obs.Clear();

    const HdDataSourceLocator extentLocator = HdExtentSchema::GetDefaultLocator();
    input->DirtyPrims({ { meshPath, extentLocator } });

    ASSERT_EQ(obs.dirtied.size(), 1u);
    const HdDataSourceLocatorSet& locators = obs.dirtied[0].dirtyLocators;

    const HdDataSourceLocator pointsLocator =
        HdPrimvarsSchema::GetDefaultLocator().Append(HdPrimvarsSchemaTokens->points);

    EXPECT_TRUE(locators.Intersects(extentLocator))
        << "The original extent invalidation must be preserved.";
    EXPECT_TRUE(locators.Intersects(pointsLocator))
        << "Dirtying extent must also dirty the derived primvars:points.";
}

TEST(TestBoundingBoxSceneIndex, DirtyingUnrelatedLocatorDoesNotDirtyPoints)
{
    auto input = HdRetainedSceneIndex::New();
    auto bbox  = hvt::BoundingBoxSceneIndex::New(input);

    RecordingObserver obs;
    bbox->AddObserver(HdSceneIndexObserverPtr(&obs));

    const SdfPath meshPath("/mesh");
    input->AddPrims({ { meshPath, HdPrimTypeTokens->mesh,
        _MakePrimSourceWithExtent(GfVec3d(-1), GfVec3d(1)) } });
    obs.Clear();

    const HdDataSourceLocator xformLocator = HdXformSchema::GetDefaultLocator();
    input->DirtyPrims({ { meshPath, xformLocator } });

    ASSERT_EQ(obs.dirtied.size(), 1u);
    const HdDataSourceLocatorSet& locators = obs.dirtied[0].dirtyLocators;

    const HdDataSourceLocator pointsLocator =
        HdPrimvarsSchema::GetDefaultLocator().Append(HdPrimvarsSchemaTokens->points);

    EXPECT_TRUE(locators.Intersects(xformLocator));
    EXPECT_FALSE(locators.Intersects(pointsLocator))
        << "Only extent invalidation should be remapped to primvars:points.";
}
