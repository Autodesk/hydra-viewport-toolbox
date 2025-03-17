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
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-W#pragma-messages"
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

namespace hvt
{

using PrimvarDescriptors = std::vector<std::pair<PXR_NS::TfToken, PXR_NS::HdDataSourceBaseHandle>>;

/// Geometry descriptor interface.
/// Client objects can implement this and be used with the geometry creation utilities without
/// actual descriptors.
template <typename T>
class GeometryDescriptorBase
{
public:
    virtual ~GeometryDescriptorBase() = default;

    virtual const T& getPoints() const                                      = 0;
    virtual const PXR_NS::VtArray<int>& getVertexCounts() const             = 0;
    virtual const PXR_NS::VtArray<int>& getIndices() const                  = 0;
    virtual const PrimvarDescriptors& getPrimvars() const                   = 0;
    virtual const PXR_NS::VtArray<PXR_NS::GfVec3f>& getNormals() const      = 0;
    virtual const PXR_NS::VtArray<PXR_NS::GfVec3f>& getDisplayColor() const = 0;
    virtual const PXR_NS::VtArray<float>& getOpacity() const                = 0;
    virtual const PXR_NS::VtArray<PXR_NS::GfVec2f>& getTexCoord() const     = 0;
    virtual const PXR_NS::SdfPath& getMaterialId() const                    = 0;
    virtual const PXR_NS::HdContainerDataSourceHandle& getMaterial() const  = 0;
    virtual const int& getRefineLevel() const                               = 0;
};

/// Mesh descriptor interface.
template <typename T>
class MeshDescriptorBase : public GeometryDescriptorBase<T>
{
public:
};

/// Polyline descriptor interface.
template <typename T>
class PolylineDescriptorBase : public GeometryDescriptorBase<T>
{
public:
    virtual const PXR_NS::VtFloatArray& getWidths() const = 0;
};

#define GEOMETRY_DESC_IMPL(Typ)                                                                    \
    const T& getPoints() const override                                                            \
    {                                                                                              \
        return points;                                                                             \
    }                                                                                              \
    const PXR_NS::VtArray<int>& getVertexCounts() const override                                   \
    {                                                                                              \
        return vertexCounts;                                                                       \
    }                                                                                              \
    const PXR_NS::VtArray<int>& getIndices() const override                                        \
    {                                                                                              \
        return indices;                                                                            \
    }                                                                                              \
    const PrimvarDescriptors& getPrimvars() const override                                         \
    {                                                                                              \
        return primvars;                                                                           \
    }                                                                                              \
    const PXR_NS::VtArray<PXR_NS::GfVec3f>& getNormals() const override                            \
    {                                                                                              \
        return normals;                                                                            \
    }                                                                                              \
    const PXR_NS::VtArray<PXR_NS::GfVec3f>& getDisplayColor() const override                       \
    {                                                                                              \
        return displayColor;                                                                       \
    }                                                                                              \
    const PXR_NS::VtArray<float>& getOpacity() const override                                      \
    {                                                                                              \
        return opacity;                                                                            \
    }                                                                                              \
    const PXR_NS::VtArray<PXR_NS::GfVec2f>& getTexCoord() const override                           \
    {                                                                                              \
        return texcoord;                                                                           \
    }                                                                                              \
    const PXR_NS::SdfPath& getMaterialId() const override                                          \
    {                                                                                              \
        return materialId;                                                                         \
    }                                                                                              \
    const PXR_NS::HdContainerDataSourceHandle& getMaterial() const override                        \
    {                                                                                              \
        return material;                                                                           \
    }                                                                                              \
    const int& getRefineLevel() const override                                                     \
    {                                                                                              \
        return refineLevel;                                                                        \
    }                                                                                              \
    Typ points;                                                                                    \
    PXR_NS::VtArray<int> vertexCounts;                                                             \
    PXR_NS::VtArray<int> indices;                                                                  \
    PrimvarDescriptors primvars;                                                                   \
    PXR_NS::VtArray<PXR_NS::GfVec3f> normals;                                                      \
    PXR_NS::VtArray<PXR_NS::GfVec3f> displayColor;                                                 \
    PXR_NS::VtArray<float> opacity;                                                                \
    PXR_NS::VtArray<PXR_NS::GfVec2f> texcoord;                                                     \
    PXR_NS::SdfPath materialId;                                                                    \
    PXR_NS::HdContainerDataSourceHandle material;                                                  \
    int refineLevel = 0;

template <typename T>
class MeshDescriptor : public MeshDescriptorBase<T>
{
public:
    GEOMETRY_DESC_IMPL(T)
};

/// 3D mesh descriptor.
using MeshDescriptor3d = MeshDescriptor<PXR_NS::VtVec3fArray>;

/// 2D mesh descriptor.
using MeshDescriptor2d = MeshDescriptor<PXR_NS::VtVec2fArray>;

template <typename T>
class PolylineDescriptor : public PolylineDescriptorBase<T>
{
public:
    GEOMETRY_DESC_IMPL(T)

    const PXR_NS::VtFloatArray& getWidths() const override { return widths; }

    PXR_NS::VtFloatArray widths;
};

/// 3D polyline descriptor.
using PolylineDescriptor3d = PolylineDescriptor<PXR_NS::VtVec3fArray>;

/// 2D polyline descriptor.
using PolylineDescriptor2d = PolylineDescriptor<PXR_NS::VtVec2fArray>;

/// \brief Creates the primvars.
/// \param desc The description of the primvars to create.
/// \return Returns the primvars datasource.
[[nodiscard]] HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePrimvars(
    const GeometryDescriptorBase<PXR_NS::VtVec2fArray>* desc);

/// \brief Creates the primvars.
/// \param desc The description of the primvars to create.
/// \return Returns the primvars datasource.
[[nodiscard]] HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePrimvars(
    const GeometryDescriptorBase<PXR_NS::VtVec3fArray>* desc);

/// Geometry and material creation.
HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMeshWithTransform(
    const MeshDescriptorBase<PXR_NS::VtVec3fArray>& desc,
    const PXR_NS::GfMatrix4d& transform = PXR_NS::GfMatrix4d(1),
    const PXR_NS::SdfPath& instancerId  = PXR_NS::SdfPath());

/// Geometry and material creation.
HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMeshWithTransform(
    const MeshDescriptorBase<PXR_NS::VtVec3fArray>& desc,
    const PXR_NS::GfMatrix4f& transform = PXR_NS::GfMatrix4f(1),
    const PXR_NS::SdfPath& instancerId  = PXR_NS::SdfPath());

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMesh(
    const MeshDescriptorBase<PXR_NS::VtVec3fArray>& desc,
    const PXR_NS::SdfPath& instancerId = PXR_NS::SdfPath());

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMesh(
    const MeshDescriptorBase<PXR_NS::VtVec2fArray>& desc,
    const PXR_NS::SdfPath& instancerId = PXR_NS::SdfPath());

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePolyline(
    const PolylineDescriptorBase<PXR_NS::VtVec3fArray>& desc);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePolyline(
    const PolylineDescriptorBase<PXR_NS::VtVec2fArray>& desc);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateInstancer(
    const PXR_NS::SdfPath& prototypeId, const PXR_NS::VtIntArray& prototypeIndices,
    const PXR_NS::VtMatrix4fArray& matrices);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateInstancer(
    const PXR_NS::SdfPath& prototypeId, const PXR_NS::VtIntArray& prototypeIndices,
    const PXR_NS::VtMatrix4dArray& matrices);

HVT_API extern PXR_NS::HdContainerDataSourceHandle Create2DMaterial(
    const PXR_NS::SdfPath& id, PXR_NS::HdRetainedSceneIndexRefPtr& retainedScene);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateWireframeBox(
    const PXR_NS::GfRange3d& bounds,
    const PXR_NS::GfVec3f color = PXR_NS::GfVec3f(0.0f, 1.0f, 0.0f));

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateWireframeBoxes(
    const std::vector<PXR_NS::GfRange3d>& bounds,
    const PXR_NS::GfVec3f color = PXR_NS::GfVec3f(0.0f, 1.0f, 0.0f));

/// Data source builders.
HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildPrimvarDS(const PXR_NS::VtValue& value,
    const PXR_NS::TfToken& interpolation = PXR_NS::HdPrimvarSchemaTokens->vertex,
    const PXR_NS::TfToken& role          = PXR_NS::HdPrimvarSchemaTokens->point);

HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildIndexedPrimvarDS(
    const PXR_NS::VtValue& value,
    const PXR_NS::TfToken& interpolation = PXR_NS::HdPrimvarSchemaTokens->vertex,
    const PXR_NS::TfToken& role          = PXR_NS::HdPrimvarSchemaTokens->point,
    const PXR_NS::VtIntArray& indices    = PXR_NS::VtIntArray());

HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildMeshDS(
    const PXR_NS::VtArray<int>& vertexCounts, const PXR_NS::VtArray<int>& faceIndices,
    const PXR_NS::VtArray<int>& holeIndices = PXR_NS::VtIntArray(),
    const PXR_NS::TfToken& orientation      = PXR_NS::HdMeshTopologySchemaTokens->rightHanded);

HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildBasisCurvesDS(
    const PXR_NS::VtArray<int>& vertexCounts, const PXR_NS::VtArray<int>& curveIndices,
    const PXR_NS::TfToken& basis = PXR_NS::HdTokens->bezier,
    const PXR_NS::TfToken& type  = PXR_NS::HdTokens->linear,
    const PXR_NS::TfToken& wrap  = PXR_NS::HdTokens->nonperiodic,
#ifdef ENABLE_ADSK_OPENUSD
    const PXR_NS::TfToken& style = PXR_NS::HdTokens->none, // adsk line style
// linestyles not in origin/dev
#else
    const PXR_NS::TfToken& style = PXR_NS::HdTokens->linear, // dummy value - is ifdef'd out in .cpp
#endif
    bool hasPixelScale = false); // line style

} // namespace hvt
