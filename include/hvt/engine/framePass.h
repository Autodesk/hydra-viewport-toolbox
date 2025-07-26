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

#include <hvt/engine/basicLayerParams.h>
#include <hvt/engine/lightingSettingsProvider.h>
#include <hvt/engine/renderBufferSettingsProvider.h>
#include <hvt/engine/selectionSettingsProvider.h>
#include <hvt/engine/syncDelegate.h>
#include <hvt/engine/taskManager.h>
#include <hvt/engine/viewportEngine.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/imaging/hdx/shadowTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
{

using RenderBufferManagerPtr = std::shared_ptr<class RenderBufferManager>;
using LightingManagerPtr     = std::shared_ptr<class LightingManager>;
using SelectionHelperPtr     = std::shared_ptr<class SelectionHelper>;

/// Default frame pass identifier.
static const std::string defaultFramePassIdentifier { "/framePass" };

/// Input parameters for a render pipeline update.
struct HVT_API ModelParams
{
    /// Default constructor.
    ModelParams() = default;

    /// Default destructor.
    ~ModelParams() = default;

    /// Stores the world extent of the model.
    PXR_NS::GfRange3d worldExtent;
};

/// Input parameters for a render pipeline update.
struct HVT_API ViewParams
{
    /// Default constructor.
    ViewParams() = default;

    /// Default destructor.
    ~ViewParams() = default;

    /// \name Setup around the view.
    /// @{

    PXR_NS::GfVec3d cameraPosition;

    /// Defines the framing.
    PXR_NS::CameraUtilFraming framing;

    /// Helper to get a default framing.
    static PXR_NS::CameraUtilFraming GetDefaultFraming(int width, int height)
    {
        /// \note This is to display all the render buffer content into the screen.
        return { { { 0, 0 }, { static_cast<float>(width), static_cast<float>(height) } },
            { { 0, 0 }, { width, height } }, 1.0f };
    }

    /// Helper to get a default framing.
    static PXR_NS::CameraUtilFraming GetDefaultFraming(int posX, int posY, int width, int height)
    {
        /// \note This is to display all the render buffer content into the screen potentially
        /// moving its origin and resizing it.
        return PXR_NS::CameraUtilFraming(
            PXR_NS::GfRect2i(PXR_NS::GfVec2i(posX, posY), width, height));
    }

    bool isOrtho { false };
    double cameraDistance { 0.0 };
    PXR_NS::GfVec3d focalPoint { 0.0, 0.0, 0.0 };
    double fov { 0.0 };
    bool initialized { false };

    PXR_NS::GfMatrix4d viewMatrix;
    PXR_NS::GfMatrix4d projectionMatrix;

    bool is3DCamera { true };

    /// @}

    /// \name Setup around the light(s).
    /// @{

    PXR_NS::GlfSimpleLightVector lights;
    PXR_NS::GlfSimpleMaterial material;
    PXR_NS::GfVec4f ambient { PXR_NS::GfVec4f(0.0f, 0.0f, 0.0f, 0.0f) };

    /// @}
};

/// Input parameters for a FramePass. This structure extends the BasicLayerParams with additional
/// parameters specific to the FramePass.
struct HVT_API FramePassParams : public BasicLayerParams
{
    /// View, model and world settings.
    /// @{
    ViewParams viewInfo;
    ModelParams modelInfo;
    /// @}

    /// Color settings.
    /// @{
    bool enableColorCorrection { true };
    PXR_NS::GfVec4f backgroundColor { 0.025f, 0.025f, 0.025f, 1.0f };
    float backgroundDepth { 1.0f };
    bool clearBackground { true };
    bool clearBackgroundDepth { false };
    /// @}

    /// MSAA settings.
    /// @{
    bool enableMultisampling { true };
    size_t msaaSampleCount { 4 };
    /// @}
};

/// A FramePass is used to render or select from a collection of Prims using a set of HdTasks and
/// settable input parameters.
class HVT_API FramePass
{
public:
    using Ptr = std::shared_ptr<FramePass>;

    /// A list of frame passes paired with the Hydra tasks used to implement them.
    using RenderTasks = std::vector<std::pair<FramePass*, PXR_NS::HdTaskSharedPtrVector>>;

    /// Constructor
    /// \param name An identifier.
    /// \note An easy identifier can be a short description of the frame pass purpose.
    /// \note By default, a unique identifier is built.
    explicit FramePass(std::string const& name);

    /// Constructor
    /// \param name An identifier.
    /// \param uid The unique identifier of this frame pass instance.
    /// \note An easy identifier can be a short description of the frame pass purpose.
    FramePass(std::string const& name, PXR_NS::SdfPath const& uid);

    /// Destructor
    virtual ~FramePass();

    /// Returns the name.
    inline const std::string& GetName() const { return _name; }

    /// Returns the unique identifier.
    inline const PXR_NS::SdfPath& GetPath() const { return _uid; }

    /// Initializes the instance.
    /// \param frameDesc The parameters used to initialize this frame pass.
    void Initialize(FramePassDescriptor const& frameDesc);

    /// Uninitializes the instance.
    virtual void Uninitialize();

    /// Returns true if the frame pass was successfully initialized.
    bool IsInitialized() const;

    enum class PresetTaskLists
    {
        Default, ///< Mimic the list of tasks created to PXR_NS::TaskController
        Minimal
    };

    /// Creates the default list of tasks.
    /// \return The list of created task & render task paths.
    std::tuple<PXR_NS::SdfPathVector, PXR_NS::SdfPathVector> CreatePresetTasks(
        PresetTaskLists listType);

    /// Updates the underlying scene.
    /// \param frame The time code of the frame to display.
    virtual void UpdateScene(PXR_NS::UsdTimeCode frame = PXR_NS::UsdTimeCode::EarliestTime());

    /// \brief Prepare and return the default list of render tasks.
    ///
    /// Before returning the list of render tasks to execute, the method first calls the underlying
    /// TaksController to go through all the registered tasks to create them in the render index and
    /// second, it finalizes their initialization by enabling or not a specific render task, by
    /// settings the render buffers when shared, adding (or not) the color correction step, etc.
    ///
    /// \param inputs The collection of aov render buffers to use.
    /// \note The list of AOV render buffers provides a way to reuse the same render buffers between
    /// frame pass instances. It then avoids intermediate (and useless) display to screen steps
    /// (i.e., PXR_NS::HdxPresentTask for example).
    /// \return The list of render tasks provided for a default frame pass.
    PXR_NS::HdTaskSharedPtrVector GetRenderTasks(RenderBufferBindings const& inputAOVs = {});

    /// Gets the render buffer associated to a specific AOV.
    /// \param aovToken The AOV token.
    /// \returns A pointer to the associated render buffer or null if not found.
    PXR_NS::HdRenderBuffer* GetRenderBuffer(PXR_NS::TfToken const& aovToken) const;

    /// Gets the texture handle associated to a specific AOV.
    /// \param aovToken The AOV token.
    /// \return An handle to the associated render texture or null if not found.
    PXR_NS::HgiTextureHandle GetRenderTexture(PXR_NS::TfToken const& aovToken) const;

    /// It holds the token (e.g., color, depth) and its corresponding texture handle.
    struct RenderOutput
    {
        /// The AOV tag i.e., color or depth.
        PXR_NS::TfToken aovToken;
        /// The corresponding render texture handle.
        PXR_NS::HgiTextureHandle aovTextureHandle;
    };
    using RenderOutputs = std::vector<RenderOutput>;

    /// Return the render index used by this frame pass.
    PXR_NS::HdRenderIndex* GetRenderIndex() const;

    /// Render the scene defined by the renderIndex using the frame and render parameters set on the
    /// FramePass and the default render tasks.
    /// \return An estimate of the percent complete if not converged or 100% if fully
    /// converged.
    virtual unsigned int Render();

    /// Render the scene defined by the renderIndex using the frame and render parameters set on the
    /// FramePass and a collection of render tasks.
    /// \param renderTasks The list of render tasks to render with
    /// \returns An estimate of the percent complete if not converged or 100% if fully converged.
    unsigned int Render(PXR_NS::HdTaskSharedPtrVector const& renderTasks);

    /// \name Selection Helpers.
    /// @{

    /// Gets the default pick parameters.
    /// \note The projection matrix is used as a pick matrix to define the pick region.
    /// \return The default values of the pick parameters.
    PXR_NS::HdxPickTaskContextParams GetDefaultPickParams() const;

    /// Picks some objects by performing a rectangular search based on the current view and
    /// projection matrices.
    /// \param pickParams The pick parameters to used.
    /// \note To ease the use of the method GetDefaultPickParams() provides all the default
    /// parameter values.
    /// \note That's usually for selection or rollover highlighting using SetSelection().
    void Pick(const PXR_NS::HdxPickTaskContextParams& pickParams);

    /// Picks some specific objects.
    /// \param pickTarget The pick target defines the type of objects to search e.g., prims, edges,
    /// etc.
    /// \param resolveMode The resolve mode. Default is resolveNearestToCenter.
    /// \param filter An Optional filter function can be supplied to modify the selection.
    /// For instance to select a whole group when a child of the group is selected.
    /// \return The list of picked objects.
    /// \note That's usually for selection or rollover highlighting using SetSelection().
    /// \note Refer to PXR_NS::HdxPickTokens to have the list of pickTarget values.
    [[nodiscard]] PXR_NS::HdSelectionSharedPtr Pick(PXR_NS::TfToken const& pickTarget,
        PXR_NS::TfToken const& resolveMode = PXR_NS::HdxPickTokens->resolveNearestToCenter,
        ViewportEngine::SelectionFilterFn const& filter = ViewportEngine::noSelectionFilterFn);

    /// Selects a collection of objects.
    /// \param selection The selection to highlight in this frame pass.
    void SetSelection(PXR_NS::HdSelectionSharedPtr const& selection);

    /// Gets the selection stored for the provided highlight mode.
    /// \param highlightMode The highlight mode to get the selection for.
    /// \note This can be used to logically pass the selection to other HdTasks who may want to
    /// treat a set of objects differently, but may not explicitly want to assume the set is the
    /// selection. (as acquired from the the render context)
    PXR_NS::SdfPathVector GetSelection(PXR_NS::HdSelection::HighlightMode highlightMode) const;

    /// @}

    /// \brief Some progressive renderers use multiple frames to converge on a final output.
    /// This reports whether the rendering is complete or needs additional draw calls to complete.
    /// \return true if fully converged otherwise false.
    inline bool IsConverged() const { return _taskManager ? _taskManager->IsConverged() : true; }

    /// Accessor for the input parameters for this frame pass.
    /// \return A collection of parameters that can be set for this frame pass.
    inline FramePassParams& params() { return _passParams; }

    /// Accessor for the input parameters for this frame pass.
    /// \return A collection of parameters that can be set for this frame pass.
    inline const FramePassParams& params() const { return _passParams; }

    /// Gets the viewport positions & dimensions.
    virtual const PXR_NS::GfRange2f GetViewport() const
    {
        return params().viewInfo.framing.displayWindow;
    }

    /// \name Shadows
    /// @{
    /// Turns the shadow task on or off.
    void SetEnableShadows(bool enable);
    /// Get the 'shadow' parameters.
    [[nodiscard]] PXR_NS::HdxShadowTaskParams GetShadowParams() const;
    /// Set the 'shadow' parameters.
    void SetShadowParams(const PXR_NS::HdxShadowTaskParams& params);
    /// @}

    /// Sets some arbitrary context data to a specific task.
    /// \param id The task token.
    /// \param data The data to provide.
    void SetTaskContextData(const PXR_NS::TfToken& id, const PXR_NS::VtValue& data);

    /// The frame pass needs a depth buffer.
    bool needDepth { true };

    /// Outputs the content of the FramePass's SyncDelegate.
    friend HVT_API std::ostream& operator<<(std::ostream& out, const FramePass& framePass)
    {
        return out << *framePass._syncDelegate;
    }

    /// Returns the task manager.
    inline TaskManagerPtr& GetTaskManager() { return _taskManager; }

    /// Returns the default lighting manager.
    LightingSettingsProviderWeakPtr GetLightingAccessor() const;

    /// Returns the default render buffer manager.
    RenderBufferSettingsProviderWeakPtr GetRenderBufferAccessor() const;

    /// Returns the default selection setting.
    SelectionSettingsProviderWeakPtr GetSelectionSettingsAccessor() const;

protected:
    /// \brief Build a frame pass unique identifier.
    ///
    /// The frame pass identifier appends to the default frame pass identifier the short
    /// identifier plus the customPart if any. It makes a custom frame pass identifier different
    /// from other frame pass identifiers but it remains human readable.
    ///
    /// \note When sharing the same render index instance (in the share model case), all the render
    /// tasks from the task controllers are stored in the shared render index. It's then mandatory
    /// to have a unique render path for each render tasks to avoid conflicts between task
    /// controllers (e.g., a render task from the task controller A rendered when rendering render
    /// tasks from the task controller B). On the other hand, having human readable paths is
    /// critical when debugging the share model case.
    ///
    /// \param name The frame pass name.
    /// \param customPart An optional part to make the unique identifier unique.
    /// \return A path being the unique identifier.
    static PXR_NS::SdfPath _BuildUID(std::string const& name, std::string const& customPart);

private:
    /// \brief Short identifier.
    /// \note It should never be changed.
    const std::string _name;

    /// The unique identifier of the instance.
    PXR_NS::SdfPath _uid { defaultFramePassIdentifier };

    FramePassParams _passParams;

    /// The task manager i.e., manages the list of tasks to render.
    TaskManagerPtr _taskManager;

    /// The render buffer (memory buffers and textures) manager.
    RenderBufferManagerPtr _bufferManager;

    /// The lighting (render index light primitives) manager.
    LightingManagerPtr _lightingManager;

    /// This manages the selection and picking data needed for task execution.
    SelectionHelperPtr _selectionHelper;

    /// The scene delegate i.e., holder of all properties.
    SyncDelegatePtr _syncDelegate;

    /// The camera delegate adding a camera prim to the given render index.
    std::unique_ptr<class PXR_NS::HdxFreeCameraSceneDelegate> _cameraDelegate;

    /// @}

    EnginePtr _engine;
};

} // namespace HVT_NS
