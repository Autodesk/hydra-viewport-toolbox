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

#include <hvt/geometry/geometry.h>

#include <pxr/pxr.h>

#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/basisCurvesTopologySchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/xformSchema.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ---------------------------------------------------------------------------
// BuildPrimvarDS
// ---------------------------------------------------------------------------

TEST(TestGeometry, BuildPrimvarDS_Vec3fArray)
{
    VtVec3fArray points = { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) };

    auto ds = hvt::BuildPrimvarDS(
        VtValue(points), HdPrimvarSchemaTokens->vertex, HdPrimvarSchemaTokens->point);
    ASSERT_TRUE(ds);

    HdPrimvarSchema primvarSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(primvarSchema.GetInterpolation());
    EXPECT_EQ(primvarSchema.GetInterpolation()->GetTypedValue(0), HdPrimvarSchemaTokens->vertex);
    ASSERT_TRUE(primvarSchema.GetRole());
    EXPECT_EQ(primvarSchema.GetRole()->GetTypedValue(0), HdPrimvarSchemaTokens->point);
}

TEST(TestGeometry, BuildPrimvarDS_SingleVec3f)
{
    GfVec3f color(1.0f, 0.0f, 0.0f);

    auto ds = hvt::BuildPrimvarDS(
        VtValue(color), HdPrimvarSchemaTokens->constant, HdPrimvarSchemaTokens->color);
    ASSERT_TRUE(ds);

    HdPrimvarSchema primvarSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(primvarSchema.GetInterpolation());
    EXPECT_EQ(primvarSchema.GetInterpolation()->GetTypedValue(0), HdPrimvarSchemaTokens->constant);
    ASSERT_TRUE(primvarSchema.GetRole());
    EXPECT_EQ(primvarSchema.GetRole()->GetTypedValue(0), HdPrimvarSchemaTokens->color);
}

TEST(TestGeometry, BuildPrimvarDS_IntArray)
{
    VtIntArray indices = { 0, 1, 2, 3 };

    auto ds = hvt::BuildPrimvarDS(VtValue(indices));
    ASSERT_TRUE(ds);

    HdPrimvarSchema primvarSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(primvarSchema.GetInterpolation());
    EXPECT_EQ(primvarSchema.GetInterpolation()->GetTypedValue(0), HdPrimvarSchemaTokens->vertex);
}

TEST(TestGeometry, BuildPrimvarDS_EmptyValue)
{
    auto ds = hvt::BuildPrimvarDS(VtValue());
    ASSERT_TRUE(ds);

    HdPrimvarSchema primvarSchema(HdContainerDataSource::Cast(ds));
    EXPECT_FALSE(primvarSchema.GetPrimvarValue());
}

// ---------------------------------------------------------------------------
// BuildIndexedPrimvarDS
// ---------------------------------------------------------------------------

TEST(TestGeometry, BuildIndexedPrimvarDS_WithIndices)
{
    VtVec3fArray colors = { GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) };
    VtIntArray indices   = { 0, 1, 0, 1 };

    auto ds = hvt::BuildIndexedPrimvarDS(
        VtValue(colors), HdPrimvarSchemaTokens->faceVarying, HdPrimvarSchemaTokens->color, indices);
    ASSERT_TRUE(ds);

    HdPrimvarSchema primvarSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(primvarSchema.GetInterpolation());
    EXPECT_EQ(
        primvarSchema.GetInterpolation()->GetTypedValue(0), HdPrimvarSchemaTokens->faceVarying);

    ASSERT_TRUE(primvarSchema.GetIndexedPrimvarValue());
    ASSERT_TRUE(primvarSchema.GetIndices());

    auto storedIndices = primvarSchema.GetIndices()->GetTypedValue(0);
    EXPECT_EQ(storedIndices.size(), 4u);
    EXPECT_EQ(storedIndices[0], 0);
    EXPECT_EQ(storedIndices[1], 1);
    EXPECT_EQ(storedIndices[2], 0);
    EXPECT_EQ(storedIndices[3], 1);
}

// ---------------------------------------------------------------------------
// BuildMeshDS
// ---------------------------------------------------------------------------

TEST(TestGeometry, BuildMeshDS_SingleQuad)
{
    VtIntArray vertexCounts = { 4 };
    VtIntArray faceIndices  = { 0, 1, 2, 3 };

    auto ds = hvt::BuildMeshDS(vertexCounts, faceIndices);
    ASSERT_TRUE(ds);

    HdMeshSchema meshSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(meshSchema.GetTopology());

    auto topoSchema = meshSchema.GetTopology();
    ASSERT_TRUE(topoSchema.GetFaceVertexCounts());
    ASSERT_TRUE(topoSchema.GetFaceVertexIndices());

    auto storedCounts = topoSchema.GetFaceVertexCounts()->GetTypedValue(0);
    ASSERT_EQ(storedCounts.size(), 1u);
    EXPECT_EQ(storedCounts[0], 4);

    auto storedIndices = topoSchema.GetFaceVertexIndices()->GetTypedValue(0);
    ASSERT_EQ(storedIndices.size(), 4u);
    EXPECT_EQ(storedIndices[0], 0);
    EXPECT_EQ(storedIndices[3], 3);
}

TEST(TestGeometry, BuildMeshDS_DoubleSided)
{
    VtIntArray vertexCounts = { 3 };
    VtIntArray faceIndices  = { 0, 1, 2 };

    auto ds =
        hvt::BuildMeshDS(vertexCounts, faceIndices, VtIntArray(),
            HdMeshTopologySchemaTokens->rightHanded, hvt::SidedMode::DoubleSided);
    ASSERT_TRUE(ds);

    HdMeshSchema meshSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(meshSchema.GetDoubleSided());
    EXPECT_TRUE(meshSchema.GetDoubleSided()->GetTypedValue(0));
}

TEST(TestGeometry, BuildMeshDS_SingleSided)
{
    VtIntArray vertexCounts = { 3 };
    VtIntArray faceIndices  = { 0, 1, 2 };

    auto ds = hvt::BuildMeshDS(vertexCounts, faceIndices);
    ASSERT_TRUE(ds);

    HdMeshSchema meshSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(meshSchema.GetDoubleSided());
    EXPECT_FALSE(meshSchema.GetDoubleSided()->GetTypedValue(0));
}

TEST(TestGeometry, BuildMeshDS_TwoTriangles)
{
    VtIntArray vertexCounts = { 3, 3 };
    VtIntArray faceIndices  = { 0, 1, 2, 2, 3, 0 };

    auto ds = hvt::BuildMeshDS(vertexCounts, faceIndices);
    ASSERT_TRUE(ds);

    HdMeshSchema meshSchema(HdContainerDataSource::Cast(ds));
    auto topoSchema = meshSchema.GetTopology();

    auto storedCounts = topoSchema.GetFaceVertexCounts()->GetTypedValue(0);
    ASSERT_EQ(storedCounts.size(), 2u);
    EXPECT_EQ(storedCounts[0], 3);
    EXPECT_EQ(storedCounts[1], 3);

    auto storedIndices = topoSchema.GetFaceVertexIndices()->GetTypedValue(0);
    ASSERT_EQ(storedIndices.size(), 6u);
}

// ---------------------------------------------------------------------------
// BuildBasisCurvesDS
// ---------------------------------------------------------------------------

TEST(TestGeometry, BuildBasisCurvesDS_Linear)
{
    VtIntArray vertexCounts = { 4 };
    VtIntArray curveIndices = { 0, 1, 2, 3 };

    auto ds = hvt::BuildBasisCurvesDS(
        vertexCounts, curveIndices, HdTokens->bezier, HdTokens->linear, HdTokens->nonperiodic);
    ASSERT_TRUE(ds);

    HdBasisCurvesSchema curvesSchema(HdContainerDataSource::Cast(ds));
    ASSERT_TRUE(curvesSchema.GetTopology());

    auto topoSchema = curvesSchema.GetTopology();
    ASSERT_TRUE(topoSchema.GetCurveVertexCounts());

    auto storedCounts = topoSchema.GetCurveVertexCounts()->GetTypedValue(0);
    ASSERT_EQ(storedCounts.size(), 1u);
    EXPECT_EQ(storedCounts[0], 4);

    ASSERT_TRUE(topoSchema.GetBasis());
    EXPECT_EQ(topoSchema.GetBasis()->GetTypedValue(0), HdTokens->bezier);

    ASSERT_TRUE(topoSchema.GetType());
    EXPECT_EQ(topoSchema.GetType()->GetTypedValue(0), HdTokens->linear);

    ASSERT_TRUE(topoSchema.GetWrap());
    EXPECT_EQ(topoSchema.GetWrap()->GetTypedValue(0), HdTokens->nonperiodic);
}

// ---------------------------------------------------------------------------
// CreateMesh (from descriptor)
// ---------------------------------------------------------------------------

TEST(TestGeometry, CreateMesh_SimpleTriangle)
{
    hvt::MeshDescriptor3d desc;
    desc.points       = { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) };
    desc.vertexCounts = { 3 };
    desc.indices      = { 0, 1, 2 };
    desc.displayColor = { GfVec3f(1, 0, 0) };

    auto ds = hvt::CreateMesh(desc);
    ASSERT_TRUE(ds);

    auto meshDs = HdContainerDataSource::Cast(ds)->Get(HdMeshSchemaTokens->mesh);
    ASSERT_TRUE(meshDs);

    HdMeshSchema meshSchema(HdContainerDataSource::Cast(meshDs));
    ASSERT_TRUE(meshSchema.GetTopology());

    auto primvarsDs = HdContainerDataSource::Cast(ds)->Get(HdPrimvarsSchemaTokens->primvars);
    ASSERT_TRUE(primvarsDs);

    HdPrimvarsSchema primvarsSchema(HdContainerDataSource::Cast(primvarsDs));
    ASSERT_TRUE(primvarsSchema.GetPrimvar(HdTokens->points));
    ASSERT_TRUE(primvarsSchema.GetPrimvar(HdTokens->displayColor));
}

TEST(TestGeometry, CreateMesh_DoubleSided)
{
    hvt::MeshDescriptor3d desc;
    desc.points       = { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) };
    desc.vertexCounts = { 3 };
    desc.indices      = { 0, 1, 2 };

    auto ds = hvt::CreateMesh(desc, SdfPath(), hvt::SidedMode::DoubleSided);
    ASSERT_TRUE(ds);

    auto meshDs = HdContainerDataSource::Cast(ds)->Get(HdMeshSchemaTokens->mesh);
    HdMeshSchema meshSchema(HdContainerDataSource::Cast(meshDs));
    ASSERT_TRUE(meshSchema.GetDoubleSided());
    EXPECT_TRUE(meshSchema.GetDoubleSided()->GetTypedValue(0));
}

// ---------------------------------------------------------------------------
// CreateMeshWithTransform
// ---------------------------------------------------------------------------

TEST(TestGeometry, CreateMeshWithTransform_Identity)
{
    hvt::MeshDescriptor3d desc;
    desc.points       = { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) };
    desc.vertexCounts = { 3 };
    desc.indices      = { 0, 1, 2 };

    GfMatrix4d identity(1);
    auto ds = hvt::CreateMeshWithTransform(desc, identity);
    ASSERT_TRUE(ds);

    auto xformDs = HdContainerDataSource::Cast(ds)->Get(HdXformSchema::GetSchemaToken());
    ASSERT_TRUE(xformDs);

    HdXformSchema xformSchema(HdContainerDataSource::Cast(xformDs));
    ASSERT_TRUE(xformSchema.GetMatrix());
    auto mat = xformSchema.GetMatrix()->GetTypedValue(0);
    EXPECT_EQ(mat, identity);
}

TEST(TestGeometry, CreateMeshWithTransform_Translation)
{
    hvt::MeshDescriptor3d desc;
    desc.points       = { GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) };
    desc.vertexCounts = { 3 };
    desc.indices      = { 0, 1, 2 };

    GfMatrix4d transform(1);
    transform.SetTranslate(GfVec3d(5.0, 10.0, -3.0));

    auto ds = hvt::CreateMeshWithTransform(desc, transform);
    ASSERT_TRUE(ds);

    auto xformDs = HdContainerDataSource::Cast(ds)->Get(HdXformSchema::GetSchemaToken());
    HdXformSchema xformSchema(HdContainerDataSource::Cast(xformDs));
    auto mat = xformSchema.GetMatrix()->GetTypedValue(0);
    EXPECT_EQ(mat, transform);
}

// ---------------------------------------------------------------------------
// CreatePolyline
// ---------------------------------------------------------------------------

TEST(TestGeometry, CreatePolyline_SimpleLine)
{
    hvt::PolylineDescriptor3d desc;
    desc.points       = { GfVec3f(0, 0, 0), GfVec3f(1, 1, 1) };
    desc.vertexCounts = { 2 };
    desc.indices      = { 0, 1 };
    desc.displayColor = { GfVec3f(0, 1, 0) };

    auto ds = hvt::CreatePolyline(desc);
    ASSERT_TRUE(ds);

    auto curvesDs =
        HdContainerDataSource::Cast(ds)->Get(HdBasisCurvesSchemaTokens->basisCurves);
    ASSERT_TRUE(curvesDs);

    auto primvarsDs = HdContainerDataSource::Cast(ds)->Get(HdPrimvarsSchemaTokens->primvars);
    ASSERT_TRUE(primvarsDs);

    HdPrimvarsSchema primvarsSchema(HdContainerDataSource::Cast(primvarsDs));
    ASSERT_TRUE(primvarsSchema.GetPrimvar(HdTokens->points));
    ASSERT_TRUE(primvarsSchema.GetPrimvar(HdTokens->displayColor));
}

// ---------------------------------------------------------------------------
// CreateWireframeBox
// ---------------------------------------------------------------------------

TEST(TestGeometry, CreateWireframeBox_UnitBox)
{
    GfRange3d bounds(GfVec3d(0, 0, 0), GfVec3d(1, 1, 1));
    GfVec3f color(0, 1, 0);

    auto ds = hvt::CreateWireframeBox(bounds, color);
    ASSERT_TRUE(ds);

    auto curvesDs =
        HdContainerDataSource::Cast(ds)->Get(HdBasisCurvesSchemaTokens->basisCurves);
    ASSERT_TRUE(curvesDs);

    auto primvarsDs = HdContainerDataSource::Cast(ds)->Get(HdPrimvarsSchemaTokens->primvars);
    ASSERT_TRUE(primvarsDs);

    HdPrimvarsSchema primvarsSchema(HdContainerDataSource::Cast(primvarsDs));
    ASSERT_TRUE(primvarsSchema.GetPrimvar(HdTokens->points));
    ASSERT_TRUE(primvarsSchema.GetPrimvar(HdTokens->displayColor));
}

TEST(TestGeometry, CreateWireframeBox_NegativeBounds)
{
    GfRange3d bounds(GfVec3d(-5, -5, -5), GfVec3d(-1, -1, -1));

    auto ds = hvt::CreateWireframeBox(bounds);
    ASSERT_TRUE(ds);

    auto curvesDs =
        HdContainerDataSource::Cast(ds)->Get(HdBasisCurvesSchemaTokens->basisCurves);
    ASSERT_TRUE(curvesDs);
}

// ---------------------------------------------------------------------------
// CreateWireframeBoxes (multiple)
// ---------------------------------------------------------------------------

TEST(TestGeometry, CreateWireframeBoxes_MultipleBounds)
{
    std::vector<GfRange3d> bounds = {
        GfRange3d(GfVec3d(0, 0, 0), GfVec3d(1, 1, 1)),
        GfRange3d(GfVec3d(2, 2, 2), GfVec3d(4, 4, 4)),
        GfRange3d(GfVec3d(-1, -1, -1), GfVec3d(0, 0, 0)),
    };

    auto ds = hvt::CreateWireframeBoxes(bounds);
    ASSERT_TRUE(ds);

    auto curvesDs =
        HdContainerDataSource::Cast(ds)->Get(HdBasisCurvesSchemaTokens->basisCurves);
    ASSERT_TRUE(curvesDs);

    HdBasisCurvesSchema curvesSchema(HdContainerDataSource::Cast(curvesDs));
    ASSERT_TRUE(curvesSchema.GetTopology());

    auto topoSchema   = curvesSchema.GetTopology();
    auto vertexCounts = topoSchema.GetCurveVertexCounts()->GetTypedValue(0);

    // Each box contributes 6 curve segments: {5, 5, 2, 2, 2, 2}
    EXPECT_EQ(vertexCounts.size(), 18u);
}

TEST(TestGeometry, CreateWireframeBoxes_SingleBox)
{
    std::vector<GfRange3d> bounds = {
        GfRange3d(GfVec3d(0, 0, 0), GfVec3d(1, 1, 1)),
    };

    auto ds = hvt::CreateWireframeBoxes(bounds);
    ASSERT_TRUE(ds);

    auto curvesDs =
        HdContainerDataSource::Cast(ds)->Get(HdBasisCurvesSchemaTokens->basisCurves);
    ASSERT_TRUE(curvesDs);

    HdBasisCurvesSchema curvesSchema(HdContainerDataSource::Cast(curvesDs));
    auto topoSchema   = curvesSchema.GetTopology();
    auto vertexCounts = topoSchema.GetCurveVertexCounts()->GetTypedValue(0);
    EXPECT_EQ(vertexCounts.size(), 6u);

    auto curveIndices = topoSchema.GetCurveIndices()->GetTypedValue(0);
    EXPECT_EQ(curveIndices.size(), 18u);
}

// ---------------------------------------------------------------------------
// CreateInstancer
// ---------------------------------------------------------------------------

TEST(TestGeometry, CreateInstancer_Float)
{
    SdfPath prototypeId("/prototype");
    VtIntArray prototypeIndices = { 0, 0, 0 };
    VtMatrix4fArray matrices    = {
        GfMatrix4f(1), GfMatrix4f(1), GfMatrix4f(1)
    };

    auto ds = hvt::CreateInstancer(prototypeId, prototypeIndices, matrices);
    ASSERT_TRUE(ds);

    auto topologyDs = HdContainerDataSource::Cast(ds)->Get(
        HdInstancerTopologySchema::GetSchemaToken());
    ASSERT_TRUE(topologyDs);

    auto primvarsDs = HdContainerDataSource::Cast(ds)->Get(
        HdPrimvarsSchema::GetSchemaToken());
    ASSERT_TRUE(primvarsDs);
}

TEST(TestGeometry, CreateInstancer_Double)
{
    SdfPath prototypeId("/prototype");
    VtIntArray prototypeIndices = { 0, 0 };
    VtMatrix4dArray matrices    = { GfMatrix4d(1), GfMatrix4d(1) };

    auto ds = hvt::CreateInstancer(prototypeId, prototypeIndices, matrices);
    ASSERT_TRUE(ds);

    auto topologyDs = HdContainerDataSource::Cast(ds)->Get(
        HdInstancerTopologySchema::GetSchemaToken());
    ASSERT_TRUE(topologyDs);
}
