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

#include <hvt/engine/framePass.h>

#include <hvt/engine/renderBufferManager.h>
#include <hvt/engine/taskCreationHelpers.h>
#include <hvt/engine/taskUtils.h>
#include <hvt/engine/viewportEngine.h>

#include "lightingManager.h"
#include "selectionHelper.h"

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/base/gf/plane.h>
#include <pxr/base/trace/trace.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hdx/pickTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,

    (meshPoints)
    (pickables)

    // tasks
    (shadowTask)
    (selectionTask)
    (colorizeSelectionTask)    
    (colorCorrectionTask)
    (visualizeAovTask)
);

#if __clang__
#pragma clang diagnostic pop
#endif
// clang-format on

// Default values for the FramePass parameters.
void defaultFramePassParams(FramePassParams& params)
{
    params.renderParams.enableLighting       = true;
    params.renderParams.overrideColor        = GfVec4f(0.0f, 0.0f, 0.0f, 0.0f);
    params.renderParams.wireframeColor       = GfVec4f(1.0f, 1.0f, 1.0f, 1.0f);
    params.renderParams.depthBiasUseDefault  = true;
    params.renderParams.depthFunc            = HdCmpFuncLEqual;
    params.renderParams.cullStyle            = HdCullStyleBackUnlessDoubleSided;
    params.renderParams.alphaThreshold       = 0.05f;
    params.renderParams.enableSceneMaterials = true;
    params.renderParams.enableSceneLights    = true;
    params.renderParams.enableClipping       = false;
    params.renderParams.viewport             = kDefaultViewport;
    params.renderParams.overrideWindowPolicy =
        std::optional<CameraUtilConformWindowPolicy>(CameraUtilFit);
    params.visualizeAOV = HdAovTokens->color;
}

template <typename T>
void SetParameter(SyncDelegatePtr& delegate, SdfPath const& id, TfToken const& key, T const& value)
{
    delegate->SetValue(id, key, VtValue(value));
}

template <typename T>
T GetParameter(const SyncDelegatePtr& delegate, SdfPath const& id, TfToken const& key)
{
    VtValue vParams = delegate->GetValue(id, key);
    return vParams.Get<T>();
}

void SetFraming(HdxRenderTaskParams& renderParams, CameraUtilFraming const& framing,
    [[maybe_unused]] HdRenderIndex* pRenderIndex)
{
    renderParams.framing = framing;

    if (!framing.IsValid())
    {
        TF_CODING_ERROR("Invalid Camera Framing.");
    }

// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
    pRenderIndex->SetFraming(framing);
#endif
}

bool SelectionEnabled(TaskManagerPtr const& taskManager)
{
    SdfPathVector renderTasks;
    taskManager->GetTaskPaths(TaskFlagsBits::kRenderTaskBit, true, renderTasks);
    return !renderTasks.empty();
}

bool ColorizeSelectionEnabled(RenderBufferManagerPtr const& bufferManager, FramePass const* framePass)
{
    return bufferManager->GetViewportAov() == HdAovTokens->color &&
        (!IsStormRenderDelegate(framePass->GetRenderIndex()) ||
            framePass->params().enableOutline);
}

bool ColorCorrectionEnabled(FramePassParams const& passParams)
{
    return passParams.enableColorCorrection && !passParams.colorspace.IsEmpty() &&
        passParams.colorspace != HdxColorCorrectionTokens->disabled;
}

} // anonymous namespace

FramePass::FramePass(std::string const& name) :
    _name(name.empty() ? "Main" : name), _uid(_BuildUID(_name, ""))
{
}

FramePass::FramePass(std::string const& name, SdfPath const& uid) :
    _name(name.empty() ? "Main" : name), _uid(uid.IsEmpty() ? _BuildUID(_name, "") : uid)
{
}

SdfPath FramePass::_BuildUID(std::string const& name, std::string const& customPart)
{
    const std::string tmpName { name.empty() ? "Main" : name };

    const std::string uid =
        tmpName[0] == '/' ? tmpName : std::string(defaultFramePassIdentifier + "_" + tmpName);

    static size_t index = 0;
    const std::string tmpCustom { customPart.empty() ? std::to_string(++index) : customPart };

    return SdfPath(uid + "_" + tmpCustom);
}

FramePass::~FramePass()
{
    Uninitialize();
}

void FramePass::Initialize(FramePassDescriptor const& frameDesc)
{
    FramePass::Uninitialize();

    // Update to default values.
    defaultFramePassParams(_passParams);

    // Using the same paths for different task controller will result in
    // conflicting camera ids between different HdxFreeCameraSceneDelegates.

    _uid = frameDesc.uid;

    // Creates the engine.
    _engine = std::make_unique<HdEngine>();

    // Creates the camera scene delegate.
    _cameraDelegate = std::make_unique<HdxFreeCameraSceneDelegate>(frameDesc.renderIndex, _uid);

    /// Creates the scene delegate i.e., holder of all properties.
    _syncDelegate = std::make_shared<SyncDelegate>(_uid, frameDesc.renderIndex);

    // Creates the selection helper, to encapsulate selection-related operations and data.
    _selectionHelper = std::make_shared<SelectionHelper>(_uid);

    // Creates the render buffer (memory buffers and textures) manager.
    _bufferManager =
        std::make_unique<RenderBufferManager>(_uid, frameDesc.renderIndex, _syncDelegate);

    // Creates the task manager i.e., manages the list of tasks to render.
    _taskManager = std::make_unique<TaskManager>(_uid, frameDesc.renderIndex, _syncDelegate);

    // Creates the lighting (render index light primitives) manager.

    const bool isHighQualityRenderer = !IsStormRenderDelegate(frameDesc.renderIndex);

    _lightingManager = std::make_unique<LightingManager>(
        _uid, frameDesc.renderIndex, _syncDelegate, isHighQualityRenderer);
    _lightingManager->SetExcludedLights(frameDesc.excludedLightPaths);
}

void FramePass::Uninitialize()
{
    _taskManager     = nullptr;
    _lightingManager = nullptr;
    _bufferManager   = nullptr;
    _selectionHelper = nullptr;
    _syncDelegate    = nullptr;
    _cameraDelegate  = nullptr;
    _engine          = nullptr;
}

bool FramePass::IsInitialized() const
{
    return (bool)_taskManager;
}

std::tuple<SdfPathVector, SdfPathVector> FramePass::CreatePresetTasks(PresetTaskLists listType)
{
    const auto getLayerSettings = [this]() -> BasicLayerParams const*
    { return &this->_passParams; };

    const auto [taskIds, renderTaskIds] = (listType == PresetTaskLists::Default)
        ? CreateDefaultTasks(
              _taskManager, _bufferManager, _lightingManager, _selectionHelper, getLayerSettings)
        : CreateMinimalTasks(_taskManager, _bufferManager, _lightingManager, getLayerSettings);

    if (!IsStormRenderDelegate(GetRenderIndex()) && _bufferManager->IsAovSupported())
    {
        // Set the buffer paths for use with the selection and picking tasks.
        _selectionHelper->SetVisualizeAOV(_bufferManager->GetViewportAov());
    }

    return { taskIds, renderTaskIds };
}

hvt::RenderBufferBindings FramePass::GetRenderBufferBindingsForNextPass(
    std::vector<pxr::TfToken> const& aovs, bool copyContents)
{
    std::string renderName = GetRenderIndex()->GetRenderDelegate()->GetRendererDisplayName();

    hvt::RenderBufferBindings inputAOVs;
    for (auto& aov : aovs)
    {
        pxr::HgiTextureHandle aovTexture;
        if (copyContents)
            aovTexture = GetRenderTexture(aov);

        pxr::HdRenderBuffer* aovBuffer   = GetRenderBuffer(aov);
        inputAOVs.push_back({ aov, aovTexture, aovBuffer, renderName });
    }

    return inputAOVs;
}

void FramePass::UpdateScene(UsdTimeCode /*frame*/) {}

HdTaskSharedPtrVector FramePass::GetRenderTasks(RenderBufferBindings const& inputAOVs)
{
    HD_TRACE_FUNCTION();

    _bufferManager->SetBufferSizeAndMsaa(
        _passParams.renderBufferSize, _passParams.msaaSampleCount, _passParams.enableMultisampling);

    // Sets the framing.
    // Note: Do not set the viewport as it's deprecated.
    SetFraming(_passParams.renderParams, _passParams.viewInfo.framing, GetRenderIndex());

    // Set the specified AOV as the one to visualize using the color output. By default this is
    // the color AOV, with no special transformation performed. For any other AOV, the AOV data is
    // transformed to something that can be displayed as a color output, e.g. depth is transformed
    // to a grayscale value normalized by the depth range of the buffer.
    // Additionally add the ID AOVs if needed.
    //
    // NOTE: This must be done *after* setting the frame dimensions (above), since this function
    // initializes buffers based on the dimensions.

    TfTokenVector renderOutputs;
    if (_passParams.visualizeAOV != HdAovTokens->color)
    {
        renderOutputs = { _passParams.visualizeAOV };
    }
    else
    {
        if (!IsStormRenderDelegate(GetRenderIndex()) || params().enableOutline)
            renderOutputs = { HdAovTokens->color, HdAovTokens->depth, HdAovTokens->primId,
                HdAovTokens->elementId, HdAovTokens->instanceId};
        else
            renderOutputs = { HdAovTokens->color, HdAovTokens->depth};

        if (_passParams.enableNeyeRenderOutput)
        {
            renderOutputs.push_back(HdAovTokens->Neye);
        }
    }

    _bufferManager->SetRenderOutputs(renderOutputs, inputAOVs, {});

    // Some selection tasks needs to update their buffer paths.
    _selectionHelper->SetVisualizeAOV(_passParams.visualizeAOV);

    // set the camera
    _cameraDelegate->SetMatrices(
        _passParams.viewInfo.viewMatrix, _passParams.viewInfo.projectionMatrix);

    // Only set clip planes if section planes are available.
    std::vector<GfVec4f> clipPlanes;
    if (!_passParams.viewInfo.sectionPlanes.empty())
    {
        GfMatrix4d const& viewMatrix = _passParams.viewInfo.viewMatrix;
        for (const auto& worldSpacePlane : _passParams.viewInfo.sectionPlanes)
        {
            // Transform section plane from world space to view space.
            GfPlane viewSpacePlane = worldSpacePlane;
            viewSpacePlane.Transform(viewMatrix);

            // Get the equation for the camera clip planes.
            GfVec4d planeEquation = viewSpacePlane.GetEquation();
            clipPlanes.push_back(GfVec4f(planeEquation));
        }
    }
    _cameraDelegate->SetClipPlanes(clipPlanes);

// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
    GetRenderIndex()->SetCameraPath(_cameraDelegate->GetCameraId());
#endif

    // Setup the lighting.
    _lightingManager->SetLighting(_passParams.viewInfo.lights, _passParams.viewInfo.material,
        _passParams.viewInfo.ambient, _cameraDelegate.get(), _passParams.modelInfo.worldExtent);

    // Setup the clear parameters for color and depth. Empty VtValue() disables clearing the buffer.
    _bufferManager->SetRenderOutputClearColor(HdAovTokens->color,
        _passParams.clearBackgroundColor ? VtValue(_passParams.backgroundColor) : VtValue());
    _bufferManager->SetRenderOutputClearColor(HdAovTokens->depth,
        _passParams.clearBackgroundDepth ? VtValue(_passParams.backgroundDepth) : VtValue());
    
    _selectionHelper->GetSettings().enableSelection = _passParams.enableSelection;
    _selectionHelper->GetSettings().enableOutline   = _passParams.enableOutline;
    _selectionHelper->GetSettings().selectionColor  = _passParams.selectionColor;
    _selectionHelper->GetSettings().locateColor     = _passParams.locateColor;

    // Update the task manager enabled/disabled state.
    _taskManager->EnableTask(_tokens->shadowTask, _lightingManager->GetShadowsEnabled());
    _taskManager->EnableTask(_tokens->selectionTask, SelectionEnabled(_taskManager));
    _taskManager->EnableTask(
        _tokens->colorizeSelectionTask, ColorizeSelectionEnabled(_bufferManager, this));
    _taskManager->EnableTask(_tokens->colorCorrectionTask, ColorCorrectionEnabled(_passParams));
    _taskManager->EnableTask(
        _tokens->visualizeAovTask, _bufferManager->GetViewportAov() != HdAovTokens->color);

    // Update Selection.
    _selectionHelper->SetSelectionContextData(_engine.get());

    // Set common render parameters before calling CommitTaskValues.
    // The tasks will consult these parameters to update themselves.
    _passParams.renderParams.camera = _cameraDelegate->GetCameraId();

    // Commit the task values for renderable tasks.
    _taskManager->CommitTaskValues(TaskFlagsBits::kExecutableBit);

    // Return the list of enabled tasks provided by the task manager.
    return _taskManager->GetTasks(TaskFlagsBits::kExecutableBit);
}

unsigned int FramePass::Render()
{
    return Render(GetRenderTasks());
}

unsigned int FramePass::Render(HdTaskSharedPtrVector const& renderTasks)
{
    HD_TRACE_FUNCTION();

    // Render using a list of render tasks.
    _engine->Execute(GetRenderIndex(), const_cast<HdTaskSharedPtrVector*>(&renderTasks));
    return 100;
}

HdRenderBuffer* FramePass::GetRenderBuffer(TfToken const& aovToken) const
{
    return _bufferManager->GetRenderOutput(aovToken);
}

HgiTextureHandle FramePass::GetRenderTexture(TfToken const& aovToken) const
{
    return _bufferManager->GetAovTexture(aovToken, _engine.get());
}

HdRenderIndex* FramePass::GetRenderIndex() const
{
    return _taskManager->GetRenderIndex();
}

HdxPickTaskContextParams FramePass::GetDefaultPickParams() const
{
    HdxPickTaskContextParams pickParams;
    pickParams.resolveMode      = HdxPickTokens->resolveNearestToCenter;
    pickParams.viewMatrix       = _passParams.viewInfo.viewMatrix;
    pickParams.projectionMatrix = _passParams.viewInfo.projectionMatrix;
    pickParams.collection =
        HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));

    return pickParams;
}

void FramePass::Pick(const HdxPickTaskContextParams& pickParams)
{
    TF_VERIFY(pickParams.outHits, "The 'outHits' parameter cannot be null.");

    _selectionHelper->SetVisualizeAOV(_passParams.visualizeAOV);

    const VtValue vtPickParams { pickParams };
    _engine->SetTaskContextData(HdxPickTokens->pickParams, vtPickParams);

    auto pickingTasks = _taskManager->GetTasks(TaskFlagsBits::kPickingTaskBit);

    _taskManager->CommitTaskValues(TaskFlagsBits::kPickingTaskBit);

    _engine->Execute(GetRenderIndex(), &pickingTasks);
}

HdSelectionSharedPtr FramePass::Pick(TfToken const& pickTarget, TfToken const& resolveMode,
    ViewportEngine::SelectionFilterFn const& filter)
{
    const TfToken newResolveMode = (pickTarget == HdxPickTokens->pickPrimsAndInstances)
        ? resolveMode
        : HdxPickTokens->resolveUnique;

    HdxPickHitVector allHits;

    // Gets the default selection parameters.
    HdxPickTaskContextParams pickParams { GetDefaultPickParams() };

    // Adjusts them based on the type of search.
    pickParams.pickTarget  = pickTarget;
    pickParams.resolveMode = newResolveMode;
    pickParams.outHits     = &allHits;

    // For vertices, edges, etc. objects, the collection to use is not the default one.
    if (pickTarget != HdxPickTokens->pickPrimsAndInstances)
    {
        // Adds a meshPoints repr since it isn't populated in HdRenderIndex::_ConfigureReprs.
        HdMesh::ConfigureRepr(_tokens->meshPoints,
            HdMeshReprDesc(HdMeshGeomStylePoints, HdCullStyleNothing,
                HdMeshReprDescTokens->pointColor,
                /*flatShadingEnabled=*/true,
                /*blendWireframeColor=*/false));

        // Uses wireframe and enables points for edge and point picking.
        const auto sceneReprSel =
            HdReprSelector(HdReprTokens->wireOnSurf, HdReprTokens->disabled, _tokens->meshPoints);
        // Defines the picking collection representation.
        const auto pickablesCol = HdRprimCollection(_tokens->pickables, sceneReprSel);

        // Unfortunately, we have to explicitly add collections besides 'geometry'.
        // See HdRenderIndex constructor.
        // Note: Do nothing if the collection already exists i.e., no need to check for presence.
        GetRenderIndex()->GetChangeTracker().AddCollection(_tokens->pickables);

        pickParams.collection = pickablesCol;
    }

    // Excludes objects based on paths.
    pickParams.collection.SetExcludePaths({ SdfPath("/frozen") });

    // Searches for the objects.
    Pick(pickParams);

    // Builds & returns the selected objects.
    return ViewportEngine::PrepareSelection(
        allHits, pickTarget, HdSelection::HighlightModeSelect, filter);
}

void FramePass::SetSelection(HdSelectionSharedPtr const& selection)
{
    _selectionHelper->SetSelection(selection);
}

SdfPathVector FramePass::GetSelection(PXR_NS::HdSelection::HighlightMode highlightMode) const
{
	return _selectionHelper->GetSelection(highlightMode);
}

void FramePass::SetTaskContextData(const TfToken& id, const VtValue& data)
{
    _engine->SetTaskContextData(id, data);
}

void FramePass::SetEnableShadows(bool enable)
{
    _lightingManager->SetEnableShadows(enable);
}

HdxShadowTaskParams FramePass::GetShadowParams() const
{
    const SdfPath& taskPath = _taskManager->GetTaskPath(_tokens->shadowTask);
    if (taskPath.IsEmpty())
    {
        return {};
    }

    return GetParameter<HdxShadowTaskParams>(_syncDelegate, taskPath, HdTokens->params);
}

void FramePass::SetShadowParams(const HdxShadowTaskParams& params)
{
    const SdfPath& taskPath = _taskManager->GetTaskPath(_tokens->shadowTask);
    if (taskPath.IsEmpty())
    {
        return;
    }

    // NOTE: There is a small design issue to think about here: if we use the commit function, but
    //       we still use these get/set params functions, we need to be careful not to create
    //       unnecessary change notifications when HdxShadowTaskParams params such as
    //       enableSceneMaterials are changed here, but are then changed back to the proper value
    //       in the commit function.
    //
    //       Below, as a workaround, we make sure to set HdxShadowTaskParams::enableSceneMaterials
    //       to prevent such an issue with the change tracker, which could constantly pick-up
    //       changes, if enableSceneMaterials is different here than in the CommitFn, where is
    //       is also updated.
    //
    HdxShadowTaskParams modifiableParams  = params;
    modifiableParams.enableSceneMaterials = _passParams.renderParams.enableSceneMaterials;
    _taskManager->SetTaskValue(taskPath, HdTokens->params, VtValue(modifiableParams));
}

LightingSettingsProviderWeakPtr FramePass::GetLightingAccessor() const
{
    return _lightingManager;
}

RenderBufferSettingsProviderWeakPtr FramePass::GetRenderBufferAccessor() const
{
    return _bufferManager;
}

SelectionSettingsProviderWeakPtr FramePass::GetSelectionSettingsAccessor() const
{
    return _selectionHelper;
}

} // namespace HVT_NS
