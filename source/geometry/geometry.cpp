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

#include <hvt/geometry/geometry.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/basisCurvesTopologySchema.h>
#include <pxr/imaging/hd/instanceIndicesSchema.h>
#include <pxr/imaging/hd/instanceSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/legacyDisplayStyleSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialConnectionSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialNodeSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/hdSt/material.h>
#include <pxr/imaging/hdSt/materialNetwork.h>
#include <pxr/imaging/hio/glslfx.h>
#include <pxr/imaging/pxOsd/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdr/registry.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

using _TokenDs = HdRetainedTypedSampledDataSource<TfToken>;

// Returns a typed sampled data source for a small number of VtValue types.
HdSampledDataSourceHandle _GetRetainedDataSource(VtValue const& val)
{
    if (val.IsHolding<int>())
    {
        return HdRetainedTypedSampledDataSource<int>::New(val.UncheckedGet<int>());
    }
    if (val.IsHolding<GfVec3f>())
    {
        return HdRetainedTypedSampledDataSource<GfVec3f>::New(val.UncheckedGet<GfVec3f>());
    }
    if (val.IsHolding<GfVec4f>())
    {
        return HdRetainedTypedSampledDataSource<GfVec4f>::New(val.UncheckedGet<GfVec4f>());
    }
    if (val.IsHolding<GfMatrix4f>())
    {
        return HdRetainedTypedSampledDataSource<GfMatrix4f>::New(val.UncheckedGet<GfMatrix4f>());
    }
    if (val.IsHolding<GfMatrix4d>())
    {
        return HdRetainedTypedSampledDataSource<GfMatrix4d>::New(val.UncheckedGet<GfMatrix4d>());
    }
    if (val.IsHolding<VtIntArray>())
    {
        return HdRetainedTypedSampledDataSource<VtIntArray>::New(val.UncheckedGet<VtIntArray>());
    }
    if (val.IsHolding<VtFloatArray>())
    {
        return HdRetainedTypedSampledDataSource<VtFloatArray>::New(
            val.UncheckedGet<VtFloatArray>());
    }
    if (val.IsHolding<VtVec2fArray>())
    {
        return HdRetainedTypedSampledDataSource<VtVec2fArray>::New(
            val.UncheckedGet<VtVec2fArray>());
    }
    if (val.IsHolding<VtVec3fArray>())
    {
        return HdRetainedTypedSampledDataSource<VtVec3fArray>::New(
            val.UncheckedGet<VtVec3fArray>());
    }
    if (val.IsHolding<VtVec4fArray>())
    {
        return HdRetainedTypedSampledDataSource<VtVec4fArray>::New(
            val.UncheckedGet<VtVec4fArray>());
    }
    if (val.IsHolding<VtMatrix4dArray>())
    {
        return HdRetainedTypedSampledDataSource<VtMatrix4dArray>::New(
            val.UncheckedGet<VtMatrix4dArray>());
    }
    if (val.IsHolding<VtMatrix4fArray>())
    {
        return HdRetainedTypedSampledDataSource<VtMatrix4fArray>::New(
            val.UncheckedGet<VtMatrix4fArray>());
    }
    if (val.IsEmpty())
    {
        return HdSampledDataSourceHandle();
    }

    TF_WARN("Unsupported primvar type %s", val.GetTypeName().c_str());
    return HdRetainedTypedSampledDataSource<VtValue>::New(val);
}

HdContainerDataSourceHandle BuildPrimvarDS(
    const VtValue& value, const TfToken& interpolation, const TfToken& role)
{
    static auto emptyArray = HdRetainedTypedSampledDataSource<VtIntArray>::New(VtIntArray());

    // clang-format off
    return HdPrimvarSchema::BuildRetained(
        _GetRetainedDataSource(value), HdSampledDataSourceHandle(), emptyArray,
        HdPrimvarSchema::BuildInterpolationDataSource(interpolation),
        HdPrimvarSchema::BuildRoleDataSource(role)
#if PXR_VERSION > 2502
        , nullptr // elementSize
#endif
    );
    // clang-format on
}

HdContainerDataSourceHandle BuildIndexedPrimvarDS(const VtValue& value,
    const TfToken& interpolation, const TfToken& role, const VtIntArray& indices)
{
    // clang-format off
    return HdPrimvarSchema::BuildRetained(HdSampledDataSourceHandle(),
        _GetRetainedDataSource(value), HdRetainedTypedSampledDataSource<VtIntArray>::New(indices),
        HdPrimvarSchema::BuildInterpolationDataSource(interpolation),
        HdPrimvarSchema::BuildRoleDataSource(role)
#if PXR_VERSION > 2502
        , nullptr // elementSize
#endif
    );
    // clang-format on
}

HdContainerDataSourceHandle BuildMeshDS(const VtArray<int>& vertexCounts,
    const VtArray<int>& faceIndices, const VtArray<int>& holeIndices,
    const TfToken& orientation, bool doubleSided)
{
    return HdMeshSchema::Builder()
        .SetTopology(HdMeshTopologySchema::BuildRetained(
            HdRetainedTypedSampledDataSource<VtIntArray>::New(vertexCounts),
            HdRetainedTypedSampledDataSource<VtIntArray>::New(faceIndices),
            HdRetainedTypedSampledDataSource<VtIntArray>::New(holeIndices),
            _TokenDs::New(orientation)))
        .SetDoubleSided(HdRetainedTypedSampledDataSource<bool>::New(doubleSided))
        .Build();
}

HdContainerDataSourceHandle BuildBasisCurvesDS(const VtArray<int>& vertexCounts,
    const VtArray<int>& curveIndices, const TfToken& basis, const TfToken& type,
    const TfToken& wrap)
{
    const HdContainerDataSourceHandle topoDs =
        HdBasisCurvesTopologySchema::Builder()
            .SetCurveVertexCounts(HdRetainedTypedSampledDataSource<VtIntArray>::New(vertexCounts))
            .SetCurveIndices(HdRetainedTypedSampledDataSource<VtIntArray>::New(curveIndices))
            .SetBasis(_TokenDs::New(basis)) // bspline, catmullRom
            .SetType(_TokenDs::New(type))   // cubic
            .SetWrap(_TokenDs::New(wrap))   // pinned, periodic, nonperiodic
            .Build();

    return HdBasisCurvesSchema::Builder().SetTopology(topoDs).Build();
}

template <typename T>
void CreatePrimvarsImp(const GeometryDescriptorBase<T>* desc, std::vector<TfToken>& primvarNames,
    std::vector<HdDataSourceBaseHandle>& primvarDataSources, HdDataSourceBaseHandle& displayStyle,
    int refineLevel)
{
    // Add the points.

    primvarNames.push_back(HdTokens->points);
    primvarDataSources.push_back(BuildPrimvarDS(
        VtValue(desc->getPoints()), HdPrimvarSchemaTokens->vertex, HdPrimvarSchemaTokens->point));

    // Add the normals if explicitly declared.

    if (desc->getNormals().size() > 0)
    {
        if (desc->getNormals().size() == 1)
        {
            // constant interpolation
            primvarNames.push_back(HdTokens->normals);
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getNormals()[0]),
                HdPrimvarSchemaTokens->constant, HdPrimvarSchemaTokens->normal));
        }
        else
        {
            // per vertex interpolation
            primvarNames.push_back(HdTokens->normals);
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getNormals()),
                HdPrimvarSchemaTokens->vertex, HdPrimvarSchemaTokens->normal));
        }
    }

    // Add the display color if explicitly declared.

    if (desc->getDisplayColor().size() > 0)
    {
        if (desc->getDisplayColor().size() == 1)
        {
            // constant interpolation
            primvarNames.push_back(HdTokens->displayColor);
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getDisplayColor()),
                HdPrimvarSchemaTokens->constant, HdPrimvarSchemaTokens->color));
        }
        else
        {
            // per vertex interpolation
            primvarNames.push_back(HdTokens->displayColor);
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getDisplayColor()),
                HdPrimvarSchemaTokens->vertex, HdPrimvarSchemaTokens->color));
        }
    }

    // Add the opacity if explicitly declared.

    if (desc->getOpacity().size() > 0)
    {
        if (desc->getOpacity().size() == 1)
        {
            // constant interpolation
            primvarNames.push_back(HdTokens->displayOpacity);
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getOpacity()),
                HdPrimvarSchemaTokens->constant, HdPrimvarRoleTokens->color));
        }
        else
        {
            // per vertex interpolation
            primvarNames.push_back(HdTokens->displayOpacity);
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getOpacity()),
                HdPrimvarSchemaTokens->vertex, HdPrimvarRoleTokens->color));
        }
    }

    // Add the uv if explicitly declared.
    // Don't use faceVarying for interpolation, which will cause crash in USD.

    if (desc->getTexCoord().size() > 0)
    {
        if (desc->getTexCoord().size() == 1)
        {
            // constant interpolation
            primvarNames.push_back(TfToken("st"));
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getTexCoord()),
                HdPrimvarSchemaTokens->constant, HdPrimvarSchemaTokens->textureCoordinate));
        }
        else
        {
            // per vertex interpolation
            primvarNames.push_back(TfToken("st"));
            primvarDataSources.push_back(BuildPrimvarDS(VtValue(desc->getTexCoord()),
                HdPrimvarSchemaTokens->vertex, HdPrimvarSchemaTokens->textureCoordinate));
        }
    }

    // Add the rest of the primvars.

    for (auto primvar : desc->getPrimvars())
    {
        // need to increase the refinement level if widths are supplied to trigger widening
        if (refineLevel == 0 && primvar.first == HdTokens->widths)
            refineLevel = 1;

        primvarNames.push_back(primvar.first);
        primvarDataSources.push_back(primvar.second);
    }

    // TODO: Other display styles can be set here in the future.
    displayStyle = HdLegacyDisplayStyleSchema::Builder()
                       .SetRefineLevel(HdRetainedTypedSampledDataSource<int>::New(refineLevel))
                       .Build();
}

template <typename T>
HdRetainedContainerDataSourceHandle CreatePolylineImp(const PolylineDescriptorBase<T>& desc)
{
    // create the topology
    HdDataSourceBaseHandle bcs = BuildBasisCurvesDS(desc.getVertexCounts(), desc.getIndices());
    std::vector<TfToken> primvarNames;
    std::vector<HdDataSourceBaseHandle> primvarDataSources;
    HdDataSourceBaseHandle displayStyle;

    int refineLevel = desc.getRefineLevel();

    // add the line width if explicitly declared
    if (desc.getWidths().size() > 0)
    {
        // need to increase the refinement level if widths are supplied to trigger tessellation
        if (refineLevel == 0)
            refineLevel = 1;

        if (desc.getWidths().size() == 1)
        {
            // constant interpolation
            primvarNames.push_back(HdTokens->widths);
            primvarDataSources.push_back(BuildPrimvarDS(
                VtValue(desc.getWidths()[0]), HdPrimvarSchemaTokens->constant, HdTokens->widths));
        }
        else
        {
            // per vertex interpolation
            primvarNames.push_back(HdTokens->widths);
            primvarDataSources.push_back(BuildPrimvarDS(
                VtValue(desc.getWidths()), HdPrimvarSchemaTokens->vertex, HdTokens->widths));
        }
    }

    CreatePrimvarsImp(&desc, primvarNames, primvarDataSources, displayStyle, refineLevel);

    std::vector<TfToken> dataNames;
    std::vector<HdDataSourceBaseHandle> dataSources;
    if (!desc.getMaterialId().IsEmpty())
    {
        static const TfToken purposes[] = { HdMaterialBindingsSchemaTokens->allPurpose };
        HdDataSourceBaseHandle materialBindingSources[] = { HdMaterialBindingSchema::Builder()
                .SetPath(HdRetainedTypedSampledDataSource<SdfPath>::New(desc.getMaterialId()))
                .Build() };

        dataNames.push_back(HdMaterialBindingsSchema::GetSchemaToken());
        dataSources.push_back(HdMaterialBindingsSchema::BuildRetained(
            TfArraySize(purposes), purposes, materialBindingSources));
    }

    HdDataSourceBaseHandle primvarsDs = HdRetainedContainerDataSource::New(
        primvarNames.size(), primvarNames.data(), primvarDataSources.data());

    dataNames.push_back(HdBasisCurvesSchemaTokens->basisCurves);
    dataSources.push_back(bcs);

    dataNames.push_back(HdPrimvarsSchemaTokens->primvars);
    dataSources.push_back(primvarsDs);

    dataNames.push_back(HdLegacyDisplayStyleSchemaTokens->displayStyle);
    dataSources.push_back(displayStyle);

    HdRetainedContainerDataSourceHandle curveData =
        HdRetainedContainerDataSource::New(dataNames.size(), dataNames.data(), dataSources.data());

    return curveData;
}

template <typename T, typename M>
HdRetainedContainerDataSourceHandle CreateMeshImp(
    const MeshDescriptorBase<T>& desc, const M* transform, const SdfPath& instancerId, 
    bool doubleSided = false)
{
    // Create the topology.
    HdDataSourceBaseHandle meshesDS = BuildMeshDS(
        desc.getVertexCounts(),
        desc.getIndices(),
        pxr::VtIntArray(),
        HdMeshTopologySchemaTokens->rightHanded,
        doubleSided);
    std::vector<TfToken> primvarNames;
    std::vector<HdDataSourceBaseHandle> primvarDataSources;
    HdDataSourceBaseHandle displayStyle;

    CreatePrimvarsImp(&desc, primvarNames, primvarDataSources, displayStyle, desc.getRefineLevel());

    HdDataSourceBaseHandle primvarsDS = HdRetainedContainerDataSource::New(
        primvarNames.size(), primvarNames.data(), primvarDataSources.data());

    std::vector<TfToken> dataNames;
    std::vector<HdDataSourceBaseHandle> dataSources;

    // Add meshes and primvars.

    dataNames.push_back(HdMeshSchemaTokens->mesh);
    dataSources.push_back(meshesDS);

    dataNames.push_back(HdPrimvarsSchemaTokens->primvars);
    dataSources.push_back(primvarsDS);

    dataNames.push_back(HdLegacyDisplayStyleSchemaTokens->displayStyle);
    dataSources.push_back(displayStyle);

    if (transform)
    {
        HdDataSourceBaseHandle xform =
            HdXformSchema::Builder()
                .SetMatrix(
                    HdRetainedTypedSampledDataSource<GfMatrix4d>::New(GfMatrix4d(*transform)))
                .Build();

        dataNames.push_back(HdXformSchema::GetSchemaToken());
        dataSources.push_back(xform);
    }

    if (!instancerId.IsEmpty())
    {
        HdDataSourceBaseHandle instancedByData =
            HdInstancedBySchema::Builder()
                .SetPaths(HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(
                    VtArray<SdfPath>({ instancerId })))
                .Build();

        dataNames.push_back(HdInstancedBySchema::GetSchemaToken());
        dataSources.push_back(instancedByData);
    }

    if (!desc.getMaterialId().IsEmpty())
    {
        static const TfToken purposes[] = { HdMaterialBindingsSchemaTokens->allPurpose };
        HdDataSourceBaseHandle materialBindingSources[] = { HdMaterialBindingSchema::Builder()
                .SetPath(HdRetainedTypedSampledDataSource<SdfPath>::New(desc.getMaterialId()))
                .Build() };

        dataNames.push_back(HdMaterialBindingsSchema::GetSchemaToken());
        dataSources.push_back(HdMaterialBindingsSchema::BuildRetained(
            TfArraySize(purposes), purposes, materialBindingSources));
    }

    HdRetainedContainerDataSourceHandle triangleData =
        HdRetainedContainerDataSource::New(dataNames.size(), dataNames.data(), dataSources.data());

    return triangleData;
}

HdRetainedContainerDataSourceHandle CreatePrimvars(const GeometryDescriptorBase<VtVec2fArray>* desc)
{
    std::vector<TfToken> primvarNames;
    std::vector<HdDataSourceBaseHandle> primvarDataSources;
    HdDataSourceBaseHandle displayStyle;

    CreatePrimvarsImp(desc, primvarNames, primvarDataSources, displayStyle, desc->getRefineLevel());

    return HdRetainedContainerDataSource::New(
        primvarNames.size(), primvarNames.data(), primvarDataSources.data());
}

HdRetainedContainerDataSourceHandle CreatePrimvars(const GeometryDescriptorBase<VtVec3fArray>* desc)
{
    std::vector<TfToken> primvarNames;
    std::vector<HdDataSourceBaseHandle> primvarDataSources;
    HdDataSourceBaseHandle displayStyle;

    CreatePrimvarsImp(desc, primvarNames, primvarDataSources, displayStyle, desc->getRefineLevel());

    return HdRetainedContainerDataSource::New(
        primvarNames.size(), primvarNames.data(), primvarDataSources.data());
}

HdRetainedContainerDataSourceHandle CreateMeshWithTransform(
    const MeshDescriptorBase<VtVec3fArray>& desc, const GfMatrix4d& transform,
    const SdfPath& instancerId, bool doubleSided)
{
    return CreateMeshImp(desc, &transform, instancerId, doubleSided);
}

HdRetainedContainerDataSourceHandle CreateMeshWithTransform(
    const MeshDescriptorBase<VtVec3fArray>& desc, const GfMatrix4f& transform,
    const SdfPath& instancerId, bool doubleSided)
{
    return CreateMeshImp(desc, &transform, instancerId, doubleSided);
}

HdRetainedContainerDataSourceHandle CreateMesh(
    const MeshDescriptorBase<VtVec2fArray>& desc, const SdfPath& instancerId, bool doubleSided)
{
    return CreateMeshImp(desc, static_cast<GfMatrix4f*>(nullptr), instancerId, doubleSided);
}

HdRetainedContainerDataSourceHandle CreateMesh(
    const MeshDescriptorBase<VtVec3fArray>& desc, const SdfPath& instancerId, bool doubleSided)
{
    return CreateMeshImp(desc, static_cast<GfMatrix4f*>(nullptr), instancerId, doubleSided);
}

HdRetainedContainerDataSourceHandle CreatePolyline(const PolylineDescriptorBase<VtVec2fArray>& desc)
{
    return CreatePolylineImp(desc);
}

HdRetainedContainerDataSourceHandle CreatePolyline(const PolylineDescriptorBase<VtVec3fArray>& desc)
{
    return CreatePolylineImp(desc);
}

class _InstanceIndicesDataSource : public HdVectorDataSource
{
public:
    HD_DECLARE_DATASOURCE(_InstanceIndicesDataSource)

    size_t GetNumElements() override { return 1; }

    HdDataSourceBaseHandle GetElement(size_t) override
    {
        return HdRetainedTypedSampledDataSource<VtArray<int>>::New(_indices);
    }

private:
    _InstanceIndicesDataSource(VtArray<int> indices) : _indices(indices) {}

    VtArray<int> const _indices;
};

template <typename T>
HdRetainedContainerDataSourceHandle CreateInstancer_Imp(
    const SdfPath& prototypeId, const VtIntArray& prototypeIndices, const T& matrices)
{
    auto instanceIndices = _InstanceIndicesDataSource::New(std::move(prototypeIndices));

    HdDataSourceBaseHandle instancerTopologyData =
        HdInstancerTopologySchema::Builder()
            .SetPrototypes(HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New({ prototypeId }))
            .SetInstanceIndices(instanceIndices)
            .Build();

    HdDataSourceBaseHandle primvarData = BuildPrimvarDS(
        VtValue(matrices), HdPrimvarSchemaTokens->instance, HdInstancerTokens->instanceTransforms);

    HdDataSourceBaseHandle primvarsDs =
        HdRetainedContainerDataSource::New(HdInstancerTokens->instanceTransforms, primvarData);

    HdRetainedContainerDataSourceHandle instancerData =
        HdRetainedContainerDataSource::New(HdInstancerTopologySchema::GetSchemaToken(),
            instancerTopologyData, HdPrimvarsSchema::GetSchemaToken(), primvarsDs);

    return instancerData;
}

HdRetainedContainerDataSourceHandle CreateInstancer(
    const SdfPath& prototypeId, const VtIntArray& prototypeIndices, const VtMatrix4fArray& matrices)
{
    return CreateInstancer_Imp(prototypeId, prototypeIndices, matrices);
}

HdRetainedContainerDataSourceHandle CreateInstancer(
    const SdfPath& prototypeId, const VtIntArray& prototypeIndices, const VtMatrix4dArray& matrices)
{
    return CreateInstancer_Imp(prototypeId, prototypeIndices, matrices);
}

HdContainerDataSourceHandle Create2DMaterial(
    const SdfPath& materialId, HdRetainedSceneIndexRefPtr& retainedScene)
{
    // clang-format off
    std::string const source(
        "-- glslfx version 0.1 \n"
        "-- configuration \n"
        "{\n"
        "\"metadata\" : { \"materialTag\" : \"draworder\" }, \n"
        "\"techniques\": {\n"
        "    \"default\": {\n"
        "        \"surfaceShader\": {\n"
        "            \"source\": [ \"Surface.Fallback\" ]\n"
        "        }\n"
        "    }\n"
        "}\n\n"
        "}\n"

        "-- glsl Surface.Fallback \n\n"
        "vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord) {\n"
        "    return color;\n"
        "}\n");
    // clang-format on

    SdrRegistry& shaderReg = SdrRegistry::GetInstance();
    SdrShaderNodeConstPtr sdrSurfaceNode =
#if PXR_VERSION >= 2505
        shaderReg.GetShaderNodeFromSourceCode(source, HioGlslfxTokens->glslfx, SdrTokenMap());
#else
        shaderReg.GetShaderNodeFromSourceCode(source, HioGlslfxTokens->glslfx, NdrTokenMap());
#endif

    auto terminalsDs = HdRetainedContainerDataSource::New(
        HdMaterialTerminalTokens->surface, HdMaterialConnectionSchema::Builder().Build());

    auto materialNode = HdMaterialNodeSchema::Builder()
                            .SetNodeIdentifier(HdRetainedTypedSampledDataSource<TfToken>::New(
                                sdrSurfaceNode->GetIdentifier()))
                            .Build();
    std::vector<TfToken> nodeNames { HdMaterialTerminalTokens->surface };
    std::vector<HdDataSourceBaseHandle> nodeValues { materialNode };

    auto nodesDs =
        HdRetainedContainerDataSource::New(nodeNames.size(), nodeNames.data(), nodeValues.data());

    // clang-format off
    auto network = HdMaterialNetworkSchema::BuildRetained(nodesDs, terminalsDs, nullptr
#if PXR_VERSION >= 2502
        , nullptr // HdContainerDataSourceHandle
#if defined(ADSK_OPENUSD_PENDING)
        , nullptr // HdTokenVectorMapDataSourceHandle
#endif //ADSK_OPENUSD_PENDING
#endif //PXR_VERSION >= 2502
        );
    // clang-format on
    retainedScene->AddPrims({ { materialId, HdPrimTypeTokens->material, network } });

    HdDataSourceBaseHandle bindingPath = HdRetainedTypedSampledDataSource<SdfPath>::New(materialId);
    std::vector<TfToken> tokens        = { HdMaterialBindingsSchemaTokens->allPurpose };
    HdContainerDataSourceHandle bindings =
        HdMaterialBindingsSchema::BuildRetained(tokens.size(), tokens.data(), &bindingPath);

    return bindings;
}

HdRetainedContainerDataSourceHandle CreateWireframeBox(const GfRange3d& bounds, const GfVec3f color)
{
    PolylineDescriptor<VtVec3fArray> polylineDesc;
    polylineDesc.vertexCounts = { 5, 5, 2, 2, 2, 2 };
    polylineDesc.indices      = { 0, 1, 3, 2, 0, 4, 5, 7, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7 };
    polylineDesc.points       = { GfVec3f(bounds.GetCorner(0)), GfVec3f(bounds.GetCorner(1)),
              GfVec3f(bounds.GetCorner(2)), GfVec3f(bounds.GetCorner(3)), GfVec3f(bounds.GetCorner(4)),
              GfVec3f(bounds.GetCorner(5)), GfVec3f(bounds.GetCorner(6)), GfVec3f(bounds.GetCorner(7)) };

    polylineDesc.displayColor = { color };

    return CreatePolyline(polylineDesc);
}

HdRetainedContainerDataSourceHandle CreateWireframeBoxes(
    const std::vector<GfRange3d>& bounds, const GfVec3f color)
{
    static const VtIntArray oneBoxVertexCounts = { 5, 5, 2, 2, 2, 2 };
    static const VtIntArray oneBoxIndices = { 0, 1, 3, 2, 0, 4, 5, 7, 6, 4, 0, 4, 1, 5, 2, 6, 3,
        7 };
    VtIntArray vertexCounts;
    vertexCounts.reserve(bounds.size() * 6);
    VtIntArray indices;
    indices.reserve(bounds.size() * 18);
    VtVec3fArray vertices;
    vertices.reserve(bounds.size() * 8);

    for (auto box : bounds)
    {
        // positions
        size_t vertexStart = vertices.size();
        vertices.resize(vertexStart + 8);
        for (int i = 0; i < 8; i++)
            vertices[vertexStart + i] = GfVec3f(box.GetCorner(i));

        // vertex counts
        auto start = vertexCounts.size();
        vertexCounts.resize(start + 6);
        std::copy(oneBoxVertexCounts.cbegin(), oneBoxVertexCounts.cend(), &vertexCounts[start]);

        // indices
        start         = indices.size();
        auto newStart = 0;
        indices.resize(start + 18);
        std::transform(&(indices.AsConst()[start]), indices.cend(), &(indices[start]),
            [&](int /* v */) { return oneBoxIndices[newStart++] + (int)vertexStart; });
    }

    PolylineDescriptor<VtVec3fArray> polylineDesc;
    polylineDesc.vertexCounts = vertexCounts;
    polylineDesc.indices      = indices;
    polylineDesc.points       = vertices;
    polylineDesc.displayColor = { color };

    return CreatePolyline(polylineDesc);
}

} // namespace HVT_NS
