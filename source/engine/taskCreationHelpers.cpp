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

#include <hvt/engine/taskCreationHelpers.h>

#include <hvt/engine/taskUtils.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/copyTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/boundingBoxTask.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hdx/colorizeSelectionTask.h>
#include <pxr/imaging/hdx/oitRenderTask.h>
#include <pxr/imaging/hdx/oitResolveTask.h>
#include <pxr/imaging/hdx/oitVolumeRenderTask.h>
#include <pxr/imaging/hdx/pickFromRenderBufferTask.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/presentTask.h>
#include <pxr/imaging/hdx/selectionTask.h>
#include <pxr/imaging/hdx/shadowTask.h>
#include <pxr/imaging/hdx/simpleLightTask.h>
#include <pxr/imaging/hdx/visualizeAovTask.h>


// ADSK: For pending changes to OpenUSD from Autodesk: hgiPresent.
#if defined(ADSK_OPENUSD_PENDING)
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hgiPresent/interopHandle.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

namespace
{

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // tasks
    (simpleLightTask)
    (shadowTask)
    (aovInputTask)
    (selectionTask)
    (colorizeSelectionTask)
    (oitResolveTask)
    (colorCorrectionTask)
    (pickTask)
    (pickFromRenderBufferTask)
    (boundingBoxTask)
    (presentTask)    
    (visualizeAovTask)
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
// Helper function to get the depth compositing setting from the layer settings.
// \params layerSettings The layer settings to get the depth compositing setting from.
// \return The depth compositing setting.
HgiCompareFunction GetDepthCompositing(
    hvt::BasicLayerParams const* layerSettings [[maybe_unused]])
{
#ifdef AGP_CONTROLABLE_DEPTH_COMPOSITING
    return getLayerSettings()->depthCompare;
#else
    return HgiCompareFunctionLEqual;
#endif
}

// Helper function to get the interop destination from the render parameters.
// \params renderParams The render parameters to get the interop destination from.
// \return The interop destination.
HgiInteropHandle GetInteropDestination(
    hvt::RenderBufferSettingsProvider const& renderParams [[maybe_unused]])
{
#ifdef PXR_GL_SUPPORT_ENABLED
    auto const& aovParams = renderParams.GetAovParamCache();

    VtValue presentFramebufferValue = aovParams.presentFramebuffer;
    uint32_t framebuffer                 = 0;
    if (presentFramebufferValue.IsHolding<uint32_t>())
    {
        framebuffer = presentFramebufferValue.UncheckedGet<uint32_t>();
    }
    return HgiGLInteropHandle { framebuffer };
#else
    TF_WARN("Present not supported");
    return HgiNullInteropHandle {};
#endif // PXR_GL_SUPPORT_ENABLED
}
#endif // ADSK_OPENUSD_PENDING

} // namespace

// TODO: OGSMOD-6571 Replace FramePass V1 custom task mechanism with FramePass V2 TaskManager
//       approach.
//       As a part of the custom task mechanism improvements, we need to improve how this function
//       is used and integrated to the FramePass. We also should either bundle or get rid of the
//       last parameters. Perhaps this function could also be broken in 2: one for HdStorm,
//       another one for HdEmbree (or !HdStorm).
std::tuple<SdfPathVector, SdfPathVector> CreateDefaultTasks(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    SelectionSettingsProviderWeakPtr const& selectionSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    // FIXME: There are few render passes that are not stable when WebGPU is enabled
    // This flag will help to disable those passes. Please note, this is a stopgap
    // until Vulkan is stable. There is active development in progress.

    bool isWebGPUDriverEnabled = false;

#ifdef EMSCRIPTEN
    isWebGPUDriverEnabled =
        hvt::GetRenderingBackendName(taskManager->GetRenderIndex()) == HgiTokens->WebGPU;
#endif

    static constexpr bool kGPUEnabled = true;

    SdfPathVector taskIds, renderTaskIds;

    if (IsStormRenderDelegate(taskManager->GetRenderIndex()))
    {
        taskIds.push_back(
            CreateLightingTask(taskManager, lightingSettingsProvider, getLayerSettings));

        taskIds.push_back(CreateShadowTask(taskManager, getLayerSettings));

        renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider,
            getLayerSettings, HdStMaterialTagTokens->defaultMaterialTag));
        renderTaskIds.push_back(CreateRenderTask(
            taskManager, renderSettingsProvider, getLayerSettings, HdStMaterialTagTokens->masked));
        renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider,
            getLayerSettings, HdStMaterialTagTokens->additive));
// Require recent version of USD
#ifdef DRAWORDER
        renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider,
            getLayerSettings, HdStMaterialTagTokens->draworder));
#endif
        renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider,
            getLayerSettings, HdStMaterialTagTokens->translucent));

        if (renderSettingsProvider.lock()->AovsSupported())
        {
            taskIds.push_back(CreateAovInputTask(taskManager, renderSettingsProvider));

            taskIds.push_back(CreateBoundingBoxTask(taskManager, renderSettingsProvider));
        }

        renderTaskIds.push_back(CreateRenderTask(
            taskManager, renderSettingsProvider, getLayerSettings, HdStMaterialTagTokens->volume));

        if (renderSettingsProvider.lock()->AovsSupported())
        {
            taskIds.push_back(CreateOitResolveTask(taskManager, renderSettingsProvider));

            if (!isWebGPUDriverEnabled)
            {
                taskIds.push_back(CreateSelectionTask(taskManager, selectionSettingsProvider));
            }

            taskIds.push_back(
                CreateColorCorrectionTask(taskManager, renderSettingsProvider, getLayerSettings));

            taskIds.push_back(CreateVisualizeAovTask(taskManager, renderSettingsProvider));

            taskIds.push_back(
                CreatePresentTask(taskManager, renderSettingsProvider, getLayerSettings));

            if (!isWebGPUDriverEnabled)
            {
                taskIds.push_back(CreatePickTask(taskManager, getLayerSettings));
            }
        }
    }
    else
    {
        renderTaskIds.push_back(
            CreateRenderTask(taskManager, renderSettingsProvider, getLayerSettings, TfToken()));

        if (renderSettingsProvider.lock()->AovsSupported() && kGPUEnabled)
        {
            taskIds.push_back(CreateAovInputTask(taskManager, renderSettingsProvider));

            taskIds.push_back(CreateBoundingBoxTask(taskManager, renderSettingsProvider));

            taskIds.push_back(CreateColorizeSelectionTask(taskManager, selectionSettingsProvider));

            taskIds.push_back(
                CreateColorCorrectionTask(taskManager, renderSettingsProvider, getLayerSettings));

            taskIds.push_back(CreateVisualizeAovTask(taskManager, renderSettingsProvider));

            taskIds.push_back(
                CreatePresentTask(taskManager, renderSettingsProvider, getLayerSettings));

            taskIds.push_back(CreatePickFromRenderBufferTask(
                taskManager, selectionSettingsProvider, getLayerSettings));
        }
    }

    return { taskIds, renderTaskIds };
}

std::tuple<SdfPathVector, SdfPathVector> CreateMinimalTasks(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    SdfPathVector taskIds, renderTaskIds;

    taskIds.push_back(CreateLightingTask(taskManager, lightingSettingsProvider, getLayerSettings));

    renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider, getLayerSettings,
        HdStMaterialTagTokens->defaultMaterialTag));
    renderTaskIds.push_back(CreateRenderTask(
        taskManager, renderSettingsProvider, getLayerSettings, HdStMaterialTagTokens->masked));
    renderTaskIds.push_back(CreateRenderTask(
        taskManager, renderSettingsProvider, getLayerSettings, HdStMaterialTagTokens->additive));

    if (renderSettingsProvider.lock()->AovsSupported())
    {
        taskIds.push_back(CreateAovInputTask(taskManager, renderSettingsProvider));
        taskIds.push_back(CreatePresentTask(taskManager, renderSettingsProvider, getLayerSettings));
    }

    return { taskIds, renderTaskIds };
}

SdfPath CreateOitResolveTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider)
{
    auto fnCommit = [renderSettingsProvider](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!renderSettingsProvider.expired())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxOitResolveTaskParams>();
// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
            params.screenSize = renderSettingsProvider.lock()->GetRenderBufferSize();
#endif
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id = taskManager->AddTask<HdxOitResolveTask>(_tokens->oitResolveTask, fnCommit);

    HdxOitResolveTaskParams params;
    // OIT is using its own buffers which are only per pixel and not per
    // sample. Thus, we resolve the AOVs before starting to render any
    // OIT geometry and only use the resolved AOVs from then on.
    params.useAovMultiSample = false;

    taskManager->SetTaskValue(id, HdTokens->params, VtValue(params));

    return id;
}

SdfPath CreateSelectionTask(
    TaskManagerPtr& taskManager, SelectionSettingsProviderWeakPtr const& settingsProvider)
{
    auto fnCommit = [settingsProvider](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!settingsProvider.expired())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxSelectionTaskParams>();

            SelectionSettings const& selectionSettings = settingsProvider.lock()->GetSettings();

            params.enableSelectionHighlight = selectionSettings.enableSelection;
            params.selectionColor           = selectionSettings.selectionColor;
            params.locateColor              = selectionSettings.locateColor;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id = taskManager->AddTask<HdxSelectionTask>(_tokens->selectionTask, fnCommit);

    HdxSelectionTaskParams params;
    params.enableSelectionHighlight = true;
    params.enableLocateHighlight    = true;
    params.selectionColor           = GfVec4f(1, 1, 0, 1);
    params.locateColor              = GfVec4f(0, 0, 1, 1);

    taskManager->SetTaskValue(id, HdTokens->params, VtValue(params));

    return id;
}

SdfPath CreateColorizeSelectionTask(
    TaskManagerPtr& taskManager, SelectionSettingsProviderWeakPtr const& settingsProvider)
{
    auto fnCommit = [settingsProvider](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!settingsProvider.expired())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxColorizeSelectionTaskParams>();

            // Convert to shared_ptr once, avoid locking multiple times.
            auto sharedSettingsProvider = settingsProvider.lock();

            SelectionSettings const& selectionSettings = sharedSettingsProvider->GetSettings();
            params.locateColor                         = selectionSettings.locateColor;
            params.enableOutline                       = selectionSettings.enableOutline;
            params.outlineRadius                       = selectionSettings.outlineRadius;
            params.enableSelectionHighlight            = selectionSettings.enableSelection;
            params.selectionColor                      = selectionSettings.selectionColor;

            SelectionBufferPaths const& selectionBufferPaths =
                sharedSettingsProvider->GetBufferPaths();
            params.primIdBufferPath     = selectionBufferPaths.primIdBufferPath;
            params.instanceIdBufferPath = selectionBufferPaths.instanceIdBufferPath;
            params.elementIdBufferPath  = selectionBufferPaths.elementIdBufferPath;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id =
        taskManager->AddTask<HdxColorizeSelectionTask>(_tokens->colorizeSelectionTask, fnCommit);

    HdxColorizeSelectionTaskParams params;
    params.enableSelectionHighlight = true;
    params.selectionColor           = GfVec4f(1, 1, 0, 1);
    params.locateColor              = GfVec4f(0, 0, 1, 1);

    taskManager->SetTaskValue(id, HdTokens->params, VtValue(params));

    return id;
}

SdfPath CreateLightingTask(TaskManagerPtr& taskManager,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    auto fnCommit = [lightingSettingsProvider, getLayerSettings](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!lightingSettingsProvider.expired())
        {
            HdxSimpleLightTaskParams simpleLightParams;
            simpleLightParams.cameraPath = getLayerSettings()->renderParams.camera;
            // Grab any simple lights from this specific task controller only and excludes others.
            //
            // Note: The render index can contain lights from different task controllers when
            // shared between several task controllers. As the light implementation does not use the
            // task controller identifier as root path it takes all lights even unrelated ones. So,
            // the task creation can identifier lights to exclude if needed.
            LightingSettingsProvider const& inputLightData = *lightingSettingsProvider.lock();
            simpleLightParams.lightExcludePaths            = inputLightData.GetExcludedLights();
            simpleLightParams.enableShadows                = inputLightData.GetShadowsEnabled();
            // -----------------------------------------------------
            // Replaces the lower part of LightingManager::Impl::SetLightingState,
            // where the HdxSimpleLightTaskParams are updated.
            const auto lightingContext = inputLightData.GetLightingContext();
            if (lightingContext)
            {
                simpleLightParams.sceneAmbient = lightingContext->GetSceneAmbient();
                simpleLightParams.material     = lightingContext->GetMaterial();
            }
            // TODO: Verify why the viewport and framing are absent from this CommitTaskFn.
            //       They seem to be absent from the HdxTaskController also (double check!),
            //       so that might not be an issue.
            //  -----------------------------------------------------
            //  Replaces SyncDelegate.SetParameter(simpleLightTaskId, HdTokens->params, params);
            fnSetValue(HdTokens->params, VtValue(simpleLightParams));
        }
    };

    const SdfPath id = taskManager->AddTask<HdxSimpleLightTask>(_tokens->simpleLightTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    HdxSimpleLightTaskParams simpleLightParams;
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(simpleLightParams));

    return id;
}

SdfPath CreateShadowTask(
    TaskManagerPtr& taskManager, std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        auto params                 = fnGetValue(HdTokens->params).Get<HdxShadowTaskParams>();
        params.enableSceneMaterials = getLayerSettings()->renderParams.enableSceneMaterials;
        fnSetValue(HdTokens->params, VtValue(params));
    };

    const SdfPath id = taskManager->AddTask<HdxShadowTask>(_tokens->shadowTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(HdxShadowTaskParams()));

    // Only use geometry render tags for shadows.
    TfTokenVector renderTags = { HdRenderTagTokens->geometry };

    taskManager->SetTaskValue(id, HdTokens->renderTags, VtValue(renderTags));

    return id;
}

SdfPath CreateColorCorrectionTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    auto fnCommit = [renderSettingsProvider, getLayerSettings](
                        TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!renderSettingsProvider.expired())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxColorCorrectionTaskParams>();

            params.aovName             = renderSettingsProvider.lock()->GetViewportAov();
            params.colorCorrectionMode = getLayerSettings()->colorspace;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id =
        taskManager->AddTask<HdxColorCorrectionTask>(_tokens->colorCorrectionTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    HdxColorCorrectionTaskParams params;
    params.colorCorrectionMode = getLayerSettings()->colorspace;
    params.displayOCIO         = "";
    params.viewOCIO            = "";
    params.colorspaceOCIO      = "";
    params.looksOCIO           = "";
    params.lut3dSizeOCIO       = 1;

    taskManager->SetTaskValue(id, HdTokens->params, VtValue(params));

    return id;
}

SdfPath CreateVisualizeAovTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider)
{
    auto fnCommit = [renderSettingsProvider](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!renderSettingsProvider.expired())
        {
            auto params    = fnGetValue(HdTokens->params).Get<HdxVisualizeAovTaskParams>();
            params.aovName = renderSettingsProvider.lock()->GetViewportAov();
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id =
        taskManager->AddTask<HdxVisualizeAovTask>(_tokens->visualizeAovTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(HdxVisualizeAovTaskParams()));

    return id;
}

SdfPath CreatePickTask(
    TaskManagerPtr& taskManager, std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        HdxPickTaskParams pickParams;

        pickParams.cullStyle            = getLayerSettings()->renderParams.cullStyle;
        pickParams.enableSceneMaterials = getLayerSettings()->renderParams.enableSceneMaterials;

        // Set Pick Task Parameter.
        fnSetValue(HdTokens->params, VtValue(pickParams));

        // Set Render Tags for Pick Task.
        fnSetValue(HdTokens->renderTags, VtValue(getLayerSettings()->renderTags));
    };

    const SdfPath id = taskManager->AddPickingTask<HdxPickTask>(_tokens->pickTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(HdxPickTaskParams()));

    return id;
}

SdfPath CreatePickFromRenderBufferTask(TaskManagerPtr& taskManager,
    SelectionSettingsProviderWeakPtr const& selectionSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    auto fnCommit = [selectionSettingsProvider, getLayerSettings](
                        TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!selectionSettingsProvider.expired())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxPickFromRenderBufferTaskParams>();

            SelectionBufferPaths const& selectionBufferPaths =
                selectionSettingsProvider.lock()->GetBufferPaths();

            // Update the buffer paths using the SelectionSettingsProvider data.
            params.primIdBufferPath     = selectionBufferPaths.primIdBufferPath;
            params.instanceIdBufferPath = selectionBufferPaths.instanceIdBufferPath;
            params.elementIdBufferPath  = selectionBufferPaths.elementIdBufferPath;
            params.depthBufferPath      = selectionBufferPaths.depthBufferPath;

            // Update the common task parameters using the global values.
            params.cameraId             = getLayerSettings()->renderParams.camera;
            params.framing              = getLayerSettings()->renderParams.framing;
            params.overrideWindowPolicy = getLayerSettings()->renderParams.overrideWindowPolicy;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id = taskManager->AddPickingTask<HdxPickFromRenderBufferTask>(
        _tokens->pickFromRenderBufferTask, fnCommit);

    HdxPickFromRenderBufferTaskParams params;
    params.cameraId = getLayerSettings()->renderParams.camera;
    params.viewport = kDefaultViewport;

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(params));

    return id;
}

SdfPath CreateBoundingBoxTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider)
{
    auto fnCommit = [renderSettingsProvider](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        auto params    = fnGetValue(HdTokens->params).Get<HdxBoundingBoxTaskParams>();
        params.aovName = renderSettingsProvider.lock()->GetViewportAov();
        fnSetValue(HdTokens->params, VtValue(params));
    };

    const SdfPath id = taskManager->AddTask<HdxBoundingBoxTask>(_tokens->boundingBoxTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(HdxBoundingBoxTaskParams()));

    return id;
}

SdfPath CreateAovInputTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider)
{
    auto fnCommit = [renderSettingsProvider](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!renderSettingsProvider.expired())
        {
            const auto& aovData = renderSettingsProvider.lock()->GetAovParamCache();

            AovInputTaskParams params;
            params.aovBufferPath   = aovData.aovBufferPath;
            params.depthBufferPath = aovData.depthBufferPath;
            params.aovBuffer       = aovData.aovBuffer;
            params.depthBuffer     = aovData.depthBuffer;
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id = taskManager->AddTask<AovInputTask>(_tokens->aovInputTask, fnCommit);

    taskManager->SetTaskValue(id, HdTokens->params, VtValue(AovInputTaskParams()));

    return id;
}

SdfPath CreatePresentTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings, renderSettingsProvider](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!renderSettingsProvider.expired())
        {
            RenderBufferSettingsProvider const& renderParams = *renderSettingsProvider.lock().get();

            HdxPresentTaskParams params;

            GfVec2i const& renderSize = renderParams.GetRenderBufferSize();
// ADSK: For pending changes to OpenUSD from Autodesk: hgiPresent.
#if defined(ADSK_OPENUSD_PENDING)
            HgiInteropPresentParams dstParams;

            auto& compParams               = dstParams.composition;
            compParams.colorSrcBlendFactor = HgiBlendFactorOne;
            compParams.colorDstBlendFactor = HgiBlendFactorOneMinusSrcAlpha;
            compParams.colorBlendOp        = HgiBlendOpAdd;
            compParams.alphaSrcBlendFactor = HgiBlendFactorOne;
            compParams.alphaDstBlendFactor = HgiBlendFactorOneMinusSrcAlpha;
            compParams.alphaBlendOp        = HgiBlendOpAdd;
            compParams.dstRegion           = GfRect2i({ 0, 0 }, renderSize[0], renderSize[1]);

            compParams.depthFunc = GetDepthCompositing(getLayerSettings());

            params.enabled = getLayerSettings()->enablePresentation;

            dstParams.destination = GetInteropDestination(renderParams);

            params.destination = dstParams;
#else // official release
            params.enabled = getLayerSettings()->enablePresentation;

            // Note: This is unused and untested in the ViewportToolbox.
            auto const& aovParams = renderParams.GetAovParamCache();
            params.dstApi         = aovParams.presentApi;
            params.dstFramebuffer = aovParams.presentFramebuffer;
            params.dstRegion      = GfVec4i(0, 0, renderSize[0], renderSize[1]);
#endif // ADSK_OPENUSD_PENDING
            // Sets the task parameter value.
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    const SdfPath id = taskManager->AddTask<HdxPresentTask>(_tokens->presentTask, fnCommit);

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    taskManager->SetTaskValue(id, HdTokens->params, VtValue(HdxPresentTaskParams()));

    return id;
}

SdfPath CreateRenderTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    std::function<BasicLayerParams const*()> const& getLayerSettings, TfToken const& materialTag)
{
    SdfPath renderTaskId   = {};
    const TfToken taskName = GetRenderTaskPathLeaf(materialTag);

    // OIT is using its own buffers which are only per pixel and not per
    // sample. Thus, we resolve the AOVs before starting to render any
    // OIT geometry and only use the resolved AOVs from then on.

    const bool isTranslucentTask = (materialTag == HdStMaterialTagTokens->translucent);
    const bool isVolumeTask      = (materialTag == HdStMaterialTagTokens->volume);
    const bool isFirstTaskInList = taskManager->GetRenderTasks().empty();
    const bool isProgressiveRenderingEnabled =
        TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false);

    auto fnCommit = [isVolumeTask, isFirstTaskInList, isTranslucentTask,
                        isProgressiveRenderingEnabled, materialTag, renderSettingsProvider,
                        getLayerSettings](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (!renderSettingsProvider.expired())
        {
            HdxRenderTaskParams renderParams = getLayerSettings()->renderParams;
            SetBlendStateForMaterialTag(materialTag, &renderParams);
            // NOTE: According to include\pxr\imaging\hdx\renderSetupTask.h, viewport is only
            // used if framing is invalid.
            renderParams.viewport = kDefaultViewport;
            renderParams.camera   = getLayerSettings()->renderParams.camera;
            // From RenderBufferManager::Impl::SetRenderOutputs
            // TODO: Rewrite and simplify the code below. Keeping it as is for now, so we can
            //       see the similarity with the original code.
            const auto& aovData = renderSettingsProvider.lock()->GetAovParamCache();

            const bool isFirstRenderTask = isProgressiveRenderingEnabled
                ? aovData.hasNoAovInputs && isFirstTaskInList
                : isFirstTaskInList;
            renderParams.aovBindings =
                isFirstRenderTask ? aovData.aovBindingsClear : aovData.aovBindingsNoClear;
            if (isVolumeTask)
            {
                renderParams.aovInputBindings = aovData.aovInputBindings;
            }
            if (isVolumeTask || isTranslucentTask)
            {
                // OIT is using its own buffers which are only per pixel and not per
                // sample. Thus, we resolve the AOVs before starting to render any
                // OIT geometry and only use the resolved AOVs from then on.
                renderParams.useAovMultiSample = false;
            }
            // From RenderBufferManager::Impl::SetRenderOutputs
            // for (const auto& kv : globals._outputSettings)
            //{
            //    const SdfPath& renderBufferId = kv.first;
            //    const HdAovDescriptor& desc   = kv.second;
            //    RenderBufferManagerImpl::UpdateRenderTaskAovOutputSettings(
            //            renderParams.aovBindings, renderBufferId, desc, isFirstTaskInList);
            //}
            // Note: the code below should do the same work as the one above, considering that
            //       UpdateRenderTaskAovOutputSettings only updates the clear value for the
            //       first task in the list, and that aovSettings data should be unchanged
            //       (since this data was pulled from the taskParams just before setting it
            //       back)
            for (size_t i = 0; i < renderParams.aovBindings.size(); ++i)
            {
                const SdfPath& renderBufferId = renderParams.aovBindings[i].renderBufferId;
                auto it                       = aovData.outputClearValues.find(renderBufferId);

                if (it != aovData.outputClearValues.end())
                {
                    // Note: Below, the boolean value "isFirstTaskInList" was initially used in the
                    // 1st implementation of this CommitTaskFn, to decide if we need to clear the
                    // buffer or not. This was based on the assumption that
                    // TaskController::SetRenderOutputSettings() was called every frame, where the
                    // condition: "bool isFirstRenderTask = renderTaskId == renderTaskIds.front();"
                    // is used. However, it turns out TaskController::SetRenderOutputSettings() was
                    // not called every frame, as there is a condition to prevent that in
                    // FramePass::GetRenderTasks(), where a check is performed on
                    //
                    //    "_passParams.clearBackground"
                    //
                    // This "_passParams.clearBackground" is set from AppRenderPipeline
                    // with the following code:
                    //
                    //    "// Don't clear background for progressive frame.
                    //     params.clearBackground = updateParams.inputAOVs.empty()
                    //         ? updateParams.pipelineParams.clearBackground
                    //         : false;"
                    //
                    // Here in this CommitTaskFn, we have the information about the aov inputs,
                    // so we can use a similar logic by using "isFirstRenderTask" instead of
                    // "isFirstTaskInList".
                    //
                    // Therefore, when using progressive rendering, "isFirstRenderTask" will only be
                    // true for the first frame, where a clear buffer is needed, and will then be
                    // set to false.
                    //
                    // A possible remaining bug would be if _taskController->SetRenderOutputSettings
                    // if forcibly called from elsewhere while inputAOVs aren't empty, with the
                    // actual intent of forcing a clear. In that particular case with progressive
                    // rendering, the clear() wouldn't happen.
                    //
                    // Double-check HydraRenderFragment::renderStage() to make sure
                    // calls to TaskController::SetRenderOutputSettings are not performed in this
                    // context (progressive rendering, clearing a frame that is NOT the first frame,
                    // aov inputs > 0)
                    VtValue clearValue = it->second;
                    renderParams.aovBindings[i].clearValue =
                        isFirstRenderTask ? clearValue : VtValue();
                }
            }

            // Set Task parameters.
            fnSetValue(HdTokens->params, VtValue(renderParams));

            // Set Task render tags.
            fnSetValue(HdTokens->renderTags, VtValue(getLayerSettings()->renderTags));

            // Set Task collection.
            HdRprimCollection taskCollection = getLayerSettings()->collection;
            taskCollection.SetMaterialTag(materialTag);
            fnSetValue(HdTokens->collection, VtValue(taskCollection));
        }
    };

    // Creates a dedicated version of the render params.
    HdxRenderTaskParams renderParams = getLayerSettings()->renderParams;

    // Set the blend state based on material tag.
    SetBlendStateForMaterialTag(materialTag, &renderParams);

    HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull),
        /*forcedRepr*/ false, materialTag);
    collection.SetRootPath(SdfPath::AbsoluteRootPath());

    if (isTranslucentTask)
    {
        renderTaskId = taskManager->AddRenderTask<HdxOitRenderTask>(taskName, fnCommit);
        renderParams.useAovMultiSample = false; // TEMP NoCommitFn
    }
    else if (isVolumeTask)
    {
        renderTaskId = taskManager->AddRenderTask<HdxOitVolumeRenderTask>(taskName, fnCommit);
        renderParams.useAovMultiSample = false; // TEMP NoCommitFn
    }
    else
    {
        renderTaskId = taskManager->AddRenderTask<HdxRenderTask>(taskName, fnCommit);
    }

    // TODO: OGSMOD-6844 TaskManager - Finalize Design of Tasks Parameter Initialization.
    // Note: HdxRenderTaskParams params are currently recreated every frame by the
    // CommitTaskFn.

    // Create an initial set of render tags in case the user doesn't set any.
    static const TfTokenVector renderTags { HdRenderTagTokens->geometry };

    // Placeholder values. We can possibly get rid of these initial values.
    taskManager->SetTaskValue(renderTaskId, HdTokens->params, VtValue(renderParams));
    taskManager->SetTaskValue(renderTaskId, HdTokens->collection, VtValue(collection));
    taskManager->SetTaskValue(renderTaskId, HdTokens->renderTags, VtValue(renderTags));

    return renderTaskId;
}

SdfPath CreateCopyTask(TaskManagerPtr& taskManager, SdfPath const& atPos)
{
    // Appends the copy task to copy the non-MSAA results back to the MSAA buffer.
    const SdfPath copyPath = taskManager->AddTask<CopyTask>(
        CopyTask::GetToken(), nullptr, atPos, TaskManager::InsertionOrder::insertBefore);

    // Default values are fine.
    CopyTaskParams copyParams;
    taskManager->SetTaskValue(copyPath, HdTokens->params, VtValue(copyParams));

    return copyPath;
}

} // namespace hvt
