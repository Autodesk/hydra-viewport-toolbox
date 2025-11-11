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
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-W#pragma-messages"
#if __clang_major__ > 11
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#else
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#endif
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4127)
#pragma warning(disable : 4100)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
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

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

/// Enumeration for mesh sided mode to improve readability over boolean parameters.
enum class SidedMode
{
    SingleSided, ///< Render only front faces
    DoubleSided  ///< Render both front and back faces
};

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
    GeometryDescriptorBase<PXR_NS::VtVec2fArray> const* desc);

/// \brief Creates the primvars.
/// \param desc The description of the primvars to create.
/// \return Returns the primvars datasource.
[[nodiscard]] HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePrimvars(
    GeometryDescriptorBase<PXR_NS::VtVec3fArray> const* desc);

/// \brief Creates a 3D mesh with transformation matrix.
/// \param desc The mesh descriptor containing geometry data.
/// \param transform The transformation matrix to apply (default: identity matrix).
/// \param instancerId Optional instancer ID for instanced rendering.
/// \param sidedMode Specifies whether to render single-sided or double-sided mesh.
/// \note DoubleSided mode may impact render performance as it disables
///       backface culling and renders both front and back faces.
/// \return Returns the mesh data source handle.
HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMeshWithTransform(
    MeshDescriptorBase<PXR_NS::VtVec3fArray> const& desc,
    PXR_NS::GfMatrix4d const& transform = PXR_NS::GfMatrix4d(1),
    PXR_NS::SdfPath const& instancerId  = PXR_NS::SdfPath(),
    SidedMode sidedMode                 = SidedMode::SingleSided);

/// \brief Creates a 3D mesh with transformation matrix (float precision).
/// \param desc The mesh descriptor containing geometry data.
/// \param transform The transformation matrix to apply (default: identity matrix).
/// \param instancerId Optional instancer ID for instanced rendering.
/// \param sidedMode Specifies whether to render single-sided or double-sided mesh.
/// \note DoubleSided mode may impact render performance as it disables
///       backface culling and renders both front and back faces.
/// \return Returns the mesh data source handle.
HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMeshWithTransform(
    MeshDescriptorBase<PXR_NS::VtVec3fArray> const& desc,
    PXR_NS::GfMatrix4f const& transform = PXR_NS::GfMatrix4f(1),
    PXR_NS::SdfPath const& instancerId  = PXR_NS::SdfPath(),
    SidedMode sidedMode                 = SidedMode::SingleSided);

/// \brief Creates a 3D mesh without transformation.
/// \param desc The mesh descriptor containing geometry data.
/// \param instancerId Optional instancer ID for instanced rendering.
/// \param sidedMode Specifies whether to render single-sided or double-sided mesh.
/// \note DoubleSided mode may impact render performance as it disables
///       backface culling and renders both front and back faces.
/// \return Returns the mesh data source handle.
HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMesh(
    MeshDescriptorBase<PXR_NS::VtVec3fArray> const& desc,
    PXR_NS::SdfPath const& instancerId = PXR_NS::SdfPath(),
    SidedMode sidedMode                = SidedMode::SingleSided);

/// \brief Creates a 2D mesh without transformation.
/// \param desc The mesh descriptor containing geometry data.
/// \param instancerId Optional instancer ID for instanced rendering.
/// \param sidedMode Specifies whether to render single-sided or double-sided mesh.
/// \note DoubleSided mode may impact render performance as it disables
///       backface culling and renders both front and back faces.
/// \return Returns the mesh data source handle.
HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateMesh(
    MeshDescriptorBase<PXR_NS::VtVec2fArray> const& desc,
    PXR_NS::SdfPath const& instancerId = PXR_NS::SdfPath(),
    SidedMode sidedMode                = SidedMode::SingleSided);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePolyline(
    PolylineDescriptorBase<PXR_NS::VtVec3fArray> const& desc);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreatePolyline(
    PolylineDescriptorBase<PXR_NS::VtVec2fArray> const& desc);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateInstancer(
    PXR_NS::SdfPath const& prototypeId, PXR_NS::VtIntArray const& prototypeIndices,
    PXR_NS::VtMatrix4fArray const& matrices);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateInstancer(
    PXR_NS::SdfPath const& prototypeId, PXR_NS::VtIntArray const& prototypeIndices,
    PXR_NS::VtMatrix4dArray const& matrices);

HVT_API extern PXR_NS::HdContainerDataSourceHandle Create2DMaterial(
    PXR_NS::SdfPath const& id, PXR_NS::HdRetainedSceneIndexRefPtr& retainedScene);

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateWireframeBox(
    PXR_NS::GfRange3d const& bounds,
    PXR_NS::GfVec3f const color = PXR_NS::GfVec3f(0.0f, 1.0f, 0.0f));

HVT_API extern PXR_NS::HdRetainedContainerDataSourceHandle CreateWireframeBoxes(
    std::vector<PXR_NS::GfRange3d> const& bounds,
    PXR_NS::GfVec3f const color = PXR_NS::GfVec3f(0.0f, 1.0f, 0.0f));

/// Data source builders.
HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildPrimvarDS(PXR_NS::VtValue const& value,
    PXR_NS::TfToken const& interpolation = PXR_NS::HdPrimvarSchemaTokens->vertex,
    PXR_NS::TfToken const& role          = PXR_NS::HdPrimvarSchemaTokens->point);

HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildIndexedPrimvarDS(
    PXR_NS::VtValue const& value,
    PXR_NS::TfToken const& interpolation = PXR_NS::HdPrimvarSchemaTokens->vertex,
    PXR_NS::TfToken const& role          = PXR_NS::HdPrimvarSchemaTokens->point,
    PXR_NS::VtIntArray const& indices    = PXR_NS::VtIntArray());

/// \brief Builds a mesh topology data source.
/// \param vertexCounts Array of vertex counts per face.
/// \param faceIndices Array of face vertex indices.
/// \param holeIndices Optional array of hole indices for faces with holes.
/// \param orientation Face orientation (default: right-handed).
/// \param sidedMode Specifies whether to render single-sided or double-sided mesh.
/// \note DoubleSided mode may impact render performance as it disables
///       backface culling and renders both front and back faces.
/// \return Returns the mesh topology data source handle.
HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildMeshDS(
    PXR_NS::VtArray<int> const& vertexCounts, PXR_NS::VtArray<int> const& faceIndices,
    PXR_NS::VtArray<int> const& holeIndices = PXR_NS::VtIntArray(),
    PXR_NS::TfToken const& orientation      = PXR_NS::HdMeshTopologySchemaTokens->rightHanded,
    SidedMode sidedMode                     = SidedMode::SingleSided);

HVT_API extern PXR_NS::HdContainerDataSourceHandle BuildBasisCurvesDS(
    PXR_NS::VtArray<int> const& vertexCounts, PXR_NS::VtArray<int> const& curveIndices,
    PXR_NS::TfToken const& basis = PXR_NS::HdTokens->bezier,
    PXR_NS::TfToken const& type  = PXR_NS::HdTokens->linear,
    PXR_NS::TfToken const& wrap  = PXR_NS::HdTokens->nonperiodic);

} // namespace HVT_NS
