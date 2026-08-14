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

#include <hvt/engine/engine.h>
#include <hvt/engine/renderIndexProxy.h>
#include <hvt/engine/taskCreationHelpers.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-W#pragma-messages"
#if __clang_major__ > 11
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#else
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#endif
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4267)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif
// clang-format on

#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/taskController.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/selectionSceneIndex.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace HVT_NS
{

class DataSourceRegistry;
class FramePass;
struct FramePassParams;

/// Renderer settings required for creating a render index.
struct HVT_API RendererDescriptor
{
    /// The renderer to use (e.g., Storm, etc.).
    std::string rendererName;
    /// The driver to use (e.g., OpenGL, Metal, etc.).
    PXR_NS::HdDriver* hgiDriver = nullptr;
};

/// Input descriptor used to create a USD based Scene Delegate.
struct HVT_API USDSceneDelegateDescriptor
{
    /// The USD Stage to render
    PXR_NS::UsdStage* stage = nullptr;
    /// The render index to add the scene delegate to
    PXR_NS::HdRenderIndex* renderIndex = nullptr;
    /// An optional list of paths to exclude from this scene delegate.
    PXR_NS::SdfPathVector excludedPrimPaths;
    /// An optional list of paths forced invisible in this scene delegate.
    PXR_NS::SdfPathVector invisedPrimPaths;
};

/// Input descriptor used to create a USD based Scene Index.
struct HVT_API USDSceneIndexDescriptor
{
    /// The USD Stage to render
    PXR_NS::UsdStageRefPtr stage;
    /// The render index to add the scene index to
    PXR_NS::HdRenderIndex* renderIndex = nullptr;
};

/// Input descriptor used to create a FramePass.
struct HVT_API FramePassDescriptor
{
    /// Render index to use (can be shared between render pass instances).
    PXR_NS::HdRenderIndex* renderIndex = nullptr;

    /// Default unique identifier (which can be customized when the render index is shared).
    PXR_NS::SdfPath uid;

    /// Light paths to exclude by render tasks.
    PXR_NS::SdfPathVector excludedLightPaths; // None by default.

    /// Options controlling the default task creation.
    /// \note For now, only the useWbOit option is available 
    /// which controls the transparency technique to use (e.g. linked-list OIT vs WBOIT).
    TaskCreationOptions taskCreationOptions;
};

using SceneDelegatePtr = std::unique_ptr<PXR_NS::UsdImagingDelegate>;
using FramePassPtr     = std::unique_ptr<FramePass>;

namespace ViewportEngine
{
/// Creates (or recreates) the render index.
/// \param renderIndex The render index to create.
/// \param desc The description (i.e., renderer name and Hgi instance).
HVT_API extern void CreateRenderer(
    RenderIndexProxyPtr& renderIndex, RendererDescriptor const& desc);

HVT_API extern void UpdateRendererSettings(RenderIndexProxy* renderIndex);

/// \brief Create a USD based scene delegate.
/// \param sceneDelegate An out param used to return the new scene delegate.
/// \param desc The input descriptor containing creation arguments.
/// \param refineLevelFallback The refineLevel fallback value.
HVT_API extern void CreateUSDSceneDelegate(SceneDelegatePtr& sceneDelegate,
    USDSceneDelegateDescriptor const& desc, int refineLevelFallback = 0);

/// Update a scene delegate and process any changes since the last time it was updated
/// \param sceneDelegate The scene delegate to update
/// \param frame The current frame to sync to
/// \param refineLevelFallback The refineLevel fallback value
HVT_API extern void UpdateSceneDelegate(SceneDelegatePtr& sceneDelegate,
    PXR_NS::UsdTimeCode frame = PXR_NS::UsdTimeCode::EarliestTime(), int refineLevelFallback = 0);

/// Create a USD scene-index hierarchy and insert the root into \p desc.renderIndex.
/// \param sceneIndex An out param used to return the final scene index.
/// \param stageSceneIndex An out param used to return the scene index holding the original USD
/// stage.
/// \param selectionSceneIndex An out param used to return the selection scene index.
/// \param desc The input descriptor containing creation arguments.
/// \note The final scene index is added to the render index by default.
HVT_API extern void CreateUSDSceneIndex(PXR_NS::HdSceneIndexBaseRefPtr& sceneIndex,
    PXR_NS::UsdImagingStageSceneIndexRefPtr& stageSceneIndex,
    PXR_NS::UsdImagingSelectionSceneIndexRefPtr& selectionSceneIndex,
    USDSceneIndexDescriptor const& desc);

/// Empty default helper for the CreateUSDSceneIndex() callback i.e., no scene index filter to add.
/// NOTE: Inline in header; omit HVT_API (MSVC C2491).
inline PXR_NS::HdSceneIndexBaseRefPtr AppendOverridesSceneIndices(
    PXR_NS::HdSceneIndexBaseRefPtr const& inputScene)
{
    return inputScene;
};

/// Create a single root scene index for \p stage, optionally wrapped by \p callback filters.
/// Use this overload when you manage render-index insertion yourself (typical in how-tos).
/// \param stage The USD Stage to render.
/// \param callback Optional chain of scene-index filters (defaults to no filters).
/// \return The root scene index (not yet inserted into a render index).
HVT_API extern PXR_NS::HdSceneIndexBaseRefPtr CreateUSDSceneIndex(PXR_NS::UsdStageRefPtr& stage,
    PXR_NS::UsdImagingCreateSceneIndicesInfo::SceneIndexAppendCallback const& callback =
        AppendOverridesSceneIndices);

/// Create the full UsdImaging scene-index bundle for \p stage (stage, selection, overlays, …).
/// \param stage The USD Stage to render.
/// \param callback Optional chain of scene-index filters applied during creation.
/// \return The UsdImaging scene-indices handle; use when you need selection or multiple roots.
HVT_API extern PXR_NS::UsdImagingSceneIndices const CreateUSDSceneIndices(
    PXR_NS::UsdStageRefPtr& stage,
    PXR_NS::UsdImagingCreateSceneIndicesInfo::SceneIndexAppendCallback const& callback =
        AppendOverridesSceneIndices);

/// \brief Update a USD scene index and process any changes since the last time it was updated.
/// \param sceneIndex The scene index to update.
/// \param frame The current frame to sync to.
HVT_API extern void UpdateUSDSceneIndex(PXR_NS::UsdImagingStageSceneIndexRefPtr& sceneIndex,
    PXR_NS::UsdTimeCode frame = PXR_NS::UsdTimeCode::EarliestTime());

/// Create a frame pass.
/// \param passDesc The frame pass descriptor.
/// \return Returns a frame pass instance.
HVT_API extern FramePassPtr CreateFramePass(FramePassDescriptor const& passDesc);

/// Prepares the selection.
/// \param sceneDelegate If not null the delegate where to find all the prims from the hit paths.
/// \param hitPaths the list of selected paths.
/// \param highlightMode The selection mode.
/// \return Returns a list of selected prims.
HVT_API extern PXR_NS::HdSelectionSharedPtr PrepareSelection(PXR_NS::HdSceneDelegate* sceneDelegate,
    PXR_NS::SdfPathSet const& hitPaths,
    PXR_NS::HdSelection::HighlightMode highlightMode = PXR_NS::HdSelection::HighlightModeSelect);

/// Prepares the selection.
/// \param hitPaths the list of selected paths.
/// \param highlightMode The selection mode.
/// \return Returns a list of selected prims.
HVT_API extern PXR_NS::HdSelectionSharedPtr PrepareSelection(PXR_NS::SdfPathSet const& hitPaths,
    PXR_NS::HdSelection::HighlightMode highlightMode = PXR_NS::HdSelection::HighlightModeSelect,
    PXR_NS::HdSelectionSharedPtr selection = nullptr);

using SelectionFilterFn = std::function<PXR_NS::SdfPathVector(PXR_NS::SdfPath const&)>;

/// Default filter (no filtering) used by PrepareSelection.
/// NOTE: Inline in header; omit HVT_API (MSVC C2491).
inline PXR_NS::SdfPathVector noSelectionFilterFn(PXR_NS::SdfPath const& highlightedPath)
{
    return { highlightedPath };
}

/// Prepares the selection from an arbitrary hit list.
/// \param allHits The arbitrary hit list.
/// \param pickTarget The type of objects present in the hit list.
/// \param highlightMode The selection mode.
/// \note Refer to PXR_NS::HdxPickTokens to have the list of pickTarget values.
/// \return Returns a list of selected objects.
HVT_API extern PXR_NS::HdSelectionSharedPtr PrepareSelection(
    PXR_NS::HdxPickHitVector const& allHits, PXR_NS::TfToken const& pickTarget,
    PXR_NS::HdSelection::HighlightMode highlightMode,
    SelectionFilterFn const& filter = noSelectionFilterFn);

/// Create an in-memory stage.
/// \param stageName The stage name.
/// \return Returns a stage instance.
HVT_API extern PXR_NS::UsdStageRefPtr CreateStage(std::string const& stageName);

/// Create an in-memory stage from a USD scene file.
/// \param fileName The filename of the USD scene file.
/// \return Returns a stage instance.
HVT_API extern PXR_NS::UsdStageRefPtr CreateStageFromFile(std::string const& fileName);

/// Create a grid using a UsdGeomBasisCurves prim.
/// \param stage The stage for the grid.
/// \param path The parent prim path for the grid.
/// \param position The position/location for the grid.
/// \param isVisible Set visibility.
HVT_API extern void CreateGrid(PXR_NS::UsdStageRefPtr& stage, PXR_NS::SdfPath const& path,
    PXR_NS::GfVec3d const& position, bool isVisible);

/// Create a simple canvas using UsdGeomPlane prim.
/// \param stage The stage for the canvas.
/// \param path The parent prim path for the canvas.
/// \param position The position/location for the canvas.
/// \param length The length of canvas see UsdGeomPlane docs for details.
/// \param width The width of canvas see UsdGeomPlane docs for details.
/// \param useYAxis If true  Canvas is along the XY plane perpendicular to camera Z,
///                        if false Canvas is along the XZ axis perpendicular to camera Y.
/// \param isVisible Set visibility.
HVT_API extern void CreateCanvas(PXR_NS::UsdStageRefPtr& stage, PXR_NS::SdfPath const& path,
    PXR_NS::GfVec3d const& position, float length, float width, bool useYAxis, bool isVisible);

/// Creates a 1x1 basis curve square (outline) located at (0, 0), (1, -1) and adds it to the given
/// stage.
/// \param stage The stage to add the box.
/// \param selectBoxPath The path name for the box.
/// \param isVisible The initial visibility setting.
HVT_API extern void CreateSelectBox(
    PXR_NS::UsdStageRefPtr& stage, PXR_NS::SdfPath selectBoxPath, bool isVisible);

HVT_API extern void CreateAxisTripod(PXR_NS::UsdStageRefPtr& stage, PXR_NS::SdfPath path,
    PXR_NS::GfVec3d const& position, float scale, bool isVisible);

/// Updates a USD xformable prim's visibility, translation, rotation, scale, and optional child
/// visibility overrides.
///
/// \param stage The USD stage containing the prim. Returns immediately if null.
/// \param path The path to the prim to update. Returns immediately if invalid.
/// \param position Translation written to the prim's existing \c TypeTranslate xform op.
/// \param rotation Rotation written to the prim's existing \c TypeOrient xform op (identity skips
/// the missing-op error).
/// \param scale Scale factor written to the prim's existing \c TypeScale xform op when \p scale >
/// 0. The value stored is \c scale * 0.5 (historical convention for gizmo geometry).
/// \param isVisible Visibility of the root prim (\c MakeVisible / \c MakeInvisible).
/// \param visibilityOverrides When \p isVisible is true, all descendants are made visible first,
/// then each path in the map with value \c false is hidden. Ignored when \p isVisible is false.
///
/// \note The prim must be \c UsdGeomXformable with the expected xform ops already authored.
/// Missing translate/scale ops log \c TF_RUNTIME_ERROR; missing orient logs an error only when
/// \p rotation is non-identity.
HVT_API extern void UpdatePrim(PXR_NS::UsdStageRefPtr& stage, PXR_NS::SdfPath const& path,
    PXR_NS::GfVec3d const& position, PXR_NS::GfRotation const& rotation, float scale,
    bool isVisible, std::map<PXR_NS::SdfPath, bool> const& visibilityOverrides = {});

/// Returns the registry singleton for data sources.
/// \todo Implement DataSourceRegistry.
HVT_API extern DataSourceRegistry& GetDataSourceRegistry();

} // namespace ViewportEngine
} // namespace HVT_NS
