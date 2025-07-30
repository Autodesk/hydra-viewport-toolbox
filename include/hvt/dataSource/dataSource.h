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
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4201)
#pragma warning(disable : 4244)
#pragma warning(disable : 4251)
#pragma warning(disable : 4305)
#pragma warning(disable : 4324)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <memory>

namespace HVT_NS
{

/// Represents whether we're optimized for viewing, or capable of editing.
enum HVT_API ViewingMode
{
    PerformantViewing = 0,
    Editable
};

/// Generic camera.
struct HVT_API CameraSettings
{
    PXR_NS::GfVec3f position;
    PXR_NS::GfVec3f target;
    PXR_NS::GfVec3f up;
    float aspect       = 0.0f;
    float fov          = 0.0f;
    float orthoScale   = 0.0f;
    bool isPerspective = false;

    bool operator==(const CameraSettings& settings) const
    {
        return position == settings.position && target == settings.target && up == settings.up &&
            aspect == settings.aspect && fov == settings.fov && orthoScale == settings.orthoScale &&
            isPerspective == settings.isPerspective;
    }
};

/// Scene input used when updating a scene before draw
struct HVT_API SceneContext
{
    bool interactive           = false;
    bool fullSync              = true;
    float timePressure         = 1.0f;
    PXR_NS::GfVec2i dimensions = { 800, 600 };
    bool material              = false;
    bool progressiveFrame      = false;
};

/// Generic Hydra scene data source interface.
class HVT_API SceneDataSource
{
public:
    using Ptr = std::shared_ptr<SceneDataSource>;

    /// Returns true if the data source is 2D.
    virtual bool is2D() const { return false; }

    /// Returns true if the Z-axis is up (otherwise the Y-axis is up).
    virtual bool isZAxisUp() const { return false; }

    /// Gets initial camera settings, if available.
    virtual bool getCameraSettings(CameraSettings&) const { return false; }

    /// \brief Update the scene if needed.
    /// \param frustums The collection of viewing frustums for all cameras viewing the model.
    /// \param context A collection of arguments that hint at how the scene should be updated.
    /// \return returns true if the scene update was finished.  Otherwise false if more update
    /// iterations are needed.
    virtual bool update(
        const std::vector<PXR_NS::GfFrustum>& /*frustums*/, const SceneContext& /*context*/)
    {
        return true;
    }

    /// flush out any data that might be held but no longer required.
    virtual void flush() {}

    /// Returns scene bounds.
    virtual PXR_NS::GfRange3d bounds() const { return PXR_NS::GfRange3d(); }

    /// Sets a world matrix for the entire scene.
    virtual void setWorldMatrix(const PXR_NS::GfMatrix4d& /*worldMatrix*/) {}

    /// Returns the world matrix.
    virtual const PXR_NS::GfMatrix4d& worldMatrix() const;

    /// Sets the properties for the DataSource.
    virtual void setProperties(const PXR_NS::VtDictionary& /*properties*/) {}

    /// Returns the set of properties for the DataSource, with current settings.
    virtual const PXR_NS::VtDictionary& properties() const;

    /// Gets world bounds for the primitive at the specified path..
    virtual PXR_NS::GfBBox3d getWorldBounds(const PXR_NS::SdfPath& primPath) const;

    /// @brief Creates or finds the material in the scene and binds it to the primitive..
    /// @param primPath Primitive path for the geometry to which the material is to be assigned.
    /// @param mtlxDocument The MaterialX document. The binding type can be one of: filepath, buffer
    /// or document ptr
    /// @return True on success.
    virtual bool bindMaterial(const PXR_NS::SdfPath& primPath, const PXR_NS::VtValue& mtlxDocument);

    /// @brief Update the value of specified material and property.
    /// @param matPrimPath Path of the material prim.
    /// @param prop Token of property and relationship
    /// @param newPropValue  New value of the given material property
    /// @return True on success.
    virtual bool updateMaterial(const PXR_NS::SdfPath& matPrimPath, const PXR_NS::TfToken& prop, const PXR_NS::VtValue& newPropValue);

    /// Returns true if the path is a primitive in the scene.
    /// \param path The primitive path.
    virtual bool isPrimitive(const PXR_NS::SdfPath& path) const;

    /// Transforms the primitives as specified.
    /// \param pathSet The set of primitives to transform..
    /// \param translate Translation vector.
    /// \param scale Scale factors
    /// \return True if successful.
    virtual bool transformPrimitives(const PXR_NS::SdfPathSet& pathSet,
        const PXR_NS::GfVec3d& translate, const PXR_NS::GfVec3d& scale);

    enum FeatureFlags
    {
        NoFeatures               = 0x00,
        PrimitiveTransformations = 0x01,
        PrimitiveDeletion        = 0x02
    };

    /// Returns the set of supported features.
    virtual unsigned int featureFlags() const { return NoFeatures; }

    /// Erases the primitives in the set.
    /// \param path The set of primitive paths.
    /// \return True if successful.
    virtual bool erasePrimitives(const PXR_NS::SdfPathSet&) { return false; }

    virtual void setRefineLevelFallback(int refineLevelFallback)
    {
        _refineLevelFallback = refineLevelFallback;
    }

    virtual int refineLevelFallback() const { return _refineLevelFallback; }

protected:
    SceneDataSource()          = default;
    virtual ~SceneDataSource() = default;

private:
    int _refineLevelFallback = 0;
};

/// DataSource type registry
class HVT_API DataSourceRegistry
{
public:
    /// Creates a data source from the file and insert it into the render index at the delegateID.
    using DataSourceCreator = std::function<SceneDataSource::Ptr(PXR_NS::HdRenderIndex* parentIndex,
        PXR_NS::SdfPath const& delegateID, const std::string& filepath,
        const ViewingMode& viewingMode)>;

    struct FileTypesDesc
    {
        std::set<std::string> extensions;
        DataSourceCreator creator;
    };

    /// Returns the number of file types descriptors the asset importer supports.
    virtual size_t fileTypesDescCount() const = 0;

    /// Returns the file type desc for the index.
    virtual const FileTypesDesc& getFileTypesDesc(size_t index) const = 0;

    /// Returns the file type desc for the file type.
    virtual bool getFileTypesDesc(const std::string& fileType, FileTypesDesc& desc) const = 0;

    /// Returns true if the file type is supported.
    virtual bool isSupportedFileType(const std::string& fileType) const = 0;

    /// Register new file types.
    virtual bool registerFileTypes(const FileTypesDesc& desc) = 0;

    /// Registry singleton.
    static DataSourceRegistry& registry();

protected:
    DataSourceRegistry()          = default;
    virtual ~DataSourceRegistry() = default;
};

} // namespace HVT_NS
