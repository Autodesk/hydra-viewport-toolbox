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
#include <pxr/imaging/hdx/skydomeTask.h>
#include <pxr/imaging/hdx/visualizeAovTask.h>
#if defined(ADSK_OPENUSD_PENDING) // hgipresent work
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hgiPresent/interopHandle.h>
#endif

// ADSK: For pending changes to OpenUSD from Autodesk: hgiPresent.
#if defined(ADSK_OPENUSD_PENDING)
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hgiPresent/interopHandle.h>
#endif

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
    #pragma clang diagnostic ignored "-Wc++20-extensions"
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
    (skydomeTask)
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
HgiCompareFunction _GetDepthCompositing(BasicLayerParams const* layerSettings [[maybe_unused]])
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
HgiPresentInteropHandle _GetInteropDestination(
    RenderBufferSettingsProvider const& renderParams [[maybe_unused]])
{
#ifdef PXR_GL_SUPPORT_ENABLED
    auto const& aovParams = renderParams.GetAovParamCache();

    VtValue presentFramebufferValue = aovParams.presentFramebuffer;
    uint32_t framebuffer            = 0;
    if (presentFramebufferValue.IsHolding<uint32_t>())
    {
        framebuffer = presentFramebufferValue.UncheckedGet<uint32_t>();
    }
    return HgiPresentGLInteropHandle { framebuffer };
#else
    TF_WARN("Present not supported");
    return HgiPresentNullInteropHandle {};
#endif // PXR_GL_SUPPORT_ENABLED
}
#endif // ADSK_OPENUSD_PENDING

bool _IsWebGPUDriverEnabled(TaskManagerPtr& taskManager [[maybe_unused]])
{
    bool isWebGPUDriverEnabled = false;

#ifdef EMSCRIPTEN
    isWebGPUDriverEnabled =
        GetRenderingBackendName(taskManager->GetRenderIndex()) == HgiTokens->WebGPU;
#endif
    return isWebGPUDriverEnabled;
}

TfToken _GetFirstRenderTaskName(const TaskManager& taskManager)
{
    SdfPathVector renderTasks;
    taskManager.GetTaskPaths(TaskFlagsBits::kRenderTaskBit, true, renderTasks);
    if (!renderTasks.empty())
    {
        // Return the first render task.
        return renderTasks[0].GetNameToken();
    }

    // Return the empty token if no render task was found.
    return TfToken();
}

} // namespace

std::tuple<SdfPathVector, SdfPathVector> CreateDefaultTasks(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    SelectionSettingsProviderWeakPtr const& selectionSettingsProvider,
    FnGetLayerSettings const& getLayerSettings)
{
    // NOTE: Certain render passes exhibit instability when WebGPU is enabled.
    // This flag provides a temporary mechanism to disable those passes.
    // This is an interim solution while Vulkan support is being stabilized,
    // with active development currently underway.

    const bool isWebGPUDriverEnabled = _IsWebGPUDriverEnabled(taskManager);

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
#if defined(DRAW_ORDER)
        renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider,
            getLayerSettings, HdStMaterialTagTokens->draworder));
#endif
        renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider,
            getLayerSettings, HdStMaterialTagTokens->translucent));

        if (renderSettingsProvider.lock()->IsAovSupported())
        {
            taskIds.push_back(CreateAovInputTask(taskManager, renderSettingsProvider));

            taskIds.push_back(CreateBoundingBoxTask(taskManager, renderSettingsProvider));
        }

        renderTaskIds.push_back(CreateRenderTask(
            taskManager, renderSettingsProvider, getLayerSettings, HdStMaterialTagTokens->volume));

        if (renderSettingsProvider.lock()->IsAovSupported())
        {
            taskIds.push_back(CreateOitResolveTask(taskManager, renderSettingsProvider));

            if (!isWebGPUDriverEnabled)
            {
                taskIds.push_back(CreateSelectionTask(taskManager, selectionSettingsProvider));
            }

            taskIds.push_back(CreateColorizeSelectionTask(taskManager, selectionSettingsProvider));

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

        if (renderSettingsProvider.lock()->IsAovSupported() && kGPUEnabled)
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
    FnGetLayerSettings const& getLayerSettings)
{
    SdfPathVector taskIds, renderTaskIds;

    taskIds.push_back(CreateLightingTask(taskManager, lightingSettingsProvider, getLayerSettings));

    renderTaskIds.push_back(CreateRenderTask(taskManager, renderSettingsProvider, getLayerSettings,
        HdStMaterialTagTokens->defaultMaterialTag));
    renderTaskIds.push_back(CreateRenderTask(
        taskManager, renderSettingsProvider, getLayerSettings, HdStMaterialTagTokens->additive));

    if (renderSettingsProvider.lock()->IsAovSupported())
    {
        taskIds.push_back(CreateAovInputTask(taskManager, renderSettingsProvider));
        taskIds.push_back(CreatePresentTask(taskManager, renderSettingsProvider, getLayerSettings));
    }

    return { taskIds, renderTaskIds };
}

SdfPath CreateOitResolveTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr)
{
    auto fnCommit = [renderSettingsWeakPtr](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxOitResolveTaskParams>();
// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
            params.screenSize = renderBufferSettings->GetRenderBufferSize();
#endif
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    HdxOitResolveTaskParams oitParams;

    // OIT is using its own buffers which are only per pixel and not per
    // sample. Thus, we resolve the AOVs before starting to render any
    // OIT geometry and only use the resolved AOVs from then on.
    oitParams.useAovMultiSample = false;

    return taskManager->AddTask<HdxOitResolveTask>(_tokens->oitResolveTask, oitParams, fnCommit);
}

SdfPath CreateSelectionTask(
    TaskManagerPtr& taskManager, SelectionSettingsProviderWeakPtr const& selectionSettingsWeakPtr)
{
    auto fnCommit = [selectionSettingsWeakPtr](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto selectionSettingsProvider = selectionSettingsWeakPtr.lock())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxSelectionTaskParams>();

            SelectionSettings const& selectionSettings = selectionSettingsProvider->GetSettings();

            params.enableSelectionHighlight = selectionSettings.enableSelection;
            params.selectionColor           = selectionSettings.selectionColor;
            params.locateColor              = selectionSettings.locateColor;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    HdxSelectionTaskParams initialParams;
    initialParams.enableSelectionHighlight = true;
    initialParams.enableLocateHighlight    = true;
    initialParams.selectionColor           = GfVec4f(1, 1, 0, 1);
    initialParams.locateColor              = GfVec4f(0, 0, 1, 1);

    return taskManager->AddTask<HdxSelectionTask>(_tokens->selectionTask, initialParams, fnCommit);
}

SdfPath CreateColorizeSelectionTask(
    TaskManagerPtr& taskManager, SelectionSettingsProviderWeakPtr const& selectionSettingsWeakPtr)
{
    auto fnCommit = [selectionSettingsWeakPtr](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto selectionSettingsProvider = selectionSettingsWeakPtr.lock())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxColorizeSelectionTaskParams>();

            SelectionSettings const& selectionSettings = selectionSettingsProvider->GetSettings();
            params.locateColor                         = selectionSettings.locateColor;
            params.enableOutline                       = selectionSettings.enableOutline;
            params.outlineRadius                       = selectionSettings.outlineRadius;
            params.enableSelectionHighlight            = selectionSettings.enableSelection || selectionSettings.enableOutline;
            params.selectionColor                      = selectionSettings.selectionColor;

            SelectionBufferPaths const& selectionBufferPaths =
                selectionSettingsProvider->GetBufferPaths();
            params.primIdBufferPath     = selectionBufferPaths.primIdBufferPath;
            params.instanceIdBufferPath = selectionBufferPaths.instanceIdBufferPath;
            params.elementIdBufferPath  = selectionBufferPaths.elementIdBufferPath;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    HdxColorizeSelectionTaskParams initialParams;
    initialParams.enableSelectionHighlight = true;
    initialParams.selectionColor           = GfVec4f(1, 1, 0, 1);
    initialParams.locateColor              = GfVec4f(0, 0, 1, 1);

    return taskManager->AddTask<HdxColorizeSelectionTask>(
        _tokens->colorizeSelectionTask, initialParams, fnCommit);
}

SdfPath CreateLightingTask(TaskManagerPtr& taskManager,
    LightingSettingsProviderWeakPtr const& lightingSettingsWeakPtr,
    FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [lightingSettingsWeakPtr, getLayerSettings](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto lightingSettingsProvider = lightingSettingsWeakPtr.lock())
        {
            HdxSimpleLightTaskParams simpleLightParams;
            simpleLightParams.cameraPath = getLayerSettings()->renderParams.camera;
            // Grab any simple lights from this specific task controller only and excludes others.
            //
            // Note: The render index can contain lights from different task managers when
            // shared between several task managers. As the light implementation does not use the
            // task manager identifier as root path it takes all lights even unrelated ones. The
            // task creation there can exclude lights if needed.
            simpleLightParams.lightExcludePaths = lightingSettingsProvider->GetExcludedLights();
            simpleLightParams.enableShadows     = lightingSettingsProvider->GetShadowsEnabled();
            const auto lightingContext          = lightingSettingsProvider->GetLightingContext();
            if (lightingContext)
            {
                simpleLightParams.sceneAmbient = lightingContext->GetSceneAmbient();
                simpleLightParams.material     = lightingContext->GetMaterial();
            }
            // TODO: Verify why the viewport and framing are absent from this CommitTaskFn.
            //       They seem to be absent from the pxr::HdxTaskController also (double check!),
            //       so that might not be an issue.
            fnSetValue(HdTokens->params, VtValue(simpleLightParams));
        }
    };

    return taskManager->AddTask<HdxSimpleLightTask>(
        _tokens->simpleLightTask, HdxSimpleLightTaskParams(), fnCommit);
}

SdfPath CreateShadowTask(TaskManagerPtr& taskManager, FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        auto params                 = fnGetValue(HdTokens->params).Get<HdxShadowTaskParams>();
        params.enableSceneMaterials = getLayerSettings()->renderParams.enableSceneMaterials;
        fnSetValue(HdTokens->params, VtValue(params));
    };

    const SdfPath id =
        taskManager->AddTask<HdxShadowTask>(_tokens->shadowTask, HdxShadowTaskParams(), fnCommit);

    // Only use geometry render tags for shadows.
    TfTokenVector renderTags = { HdRenderTagTokens->geometry };

    taskManager->SetTaskValue(id, HdTokens->renderTags, VtValue(renderTags));

    return id;
}

SdfPath CreateColorCorrectionTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr,
    FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [renderSettingsWeakPtr, getLayerSettings](
                        TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxColorCorrectionTaskParams>();

            params.aovName             = renderBufferSettings->GetViewportAov();
            params.colorCorrectionMode = getLayerSettings()->colorspace;

            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    HdxColorCorrectionTaskParams initialParams;
    initialParams.colorCorrectionMode = getLayerSettings()->colorspace;
    initialParams.displayOCIO         = "";
    initialParams.viewOCIO            = "";
    initialParams.colorspaceOCIO      = "";
    initialParams.looksOCIO           = "";
    initialParams.lut3dSizeOCIO       = 1;

    return taskManager->AddTask<HdxColorCorrectionTask>(
        _tokens->colorCorrectionTask, initialParams, fnCommit);
}

SdfPath CreateVisualizeAovTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr)
{
    auto fnCommit = [renderSettingsWeakPtr](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            auto params    = fnGetValue(HdTokens->params).Get<HdxVisualizeAovTaskParams>();
            params.aovName = renderBufferSettings->GetViewportAov();
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    return taskManager->AddTask<HdxVisualizeAovTask>(
        _tokens->visualizeAovTask, HdxVisualizeAovTaskParams(), fnCommit);
}

SdfPath CreatePickTask(TaskManagerPtr& taskManager, FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        HdxPickTaskParams pickParams;

        pickParams.cullStyle            = getLayerSettings()->renderParams.cullStyle;
        pickParams.enableSceneMaterials = getLayerSettings()->renderParams.enableSceneMaterials;

        // Set Pick task Parameter.
        fnSetValue(HdTokens->params, VtValue(pickParams));

        // Set Render Tags for Pick task.
        fnSetValue(HdTokens->renderTags, VtValue(getLayerSettings()->renderTags));
    };

    return taskManager->AddTask<HdxPickTask>(_tokens->pickTask, HdxPickTaskParams(), fnCommit,
        PXR_NS::SdfPath(), TaskManager::InsertionOrder::insertBefore,
        TaskFlagsBits::kPickingTaskBit);
}

SdfPath CreatePickFromRenderBufferTask(TaskManagerPtr& taskManager,
    SelectionSettingsProviderWeakPtr const& selectionSettingsWeakPtr,
    FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [selectionSettingsWeakPtr, getLayerSettings](
                        TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto selectionSettingsProvider = selectionSettingsWeakPtr.lock())
        {
            auto params = fnGetValue(HdTokens->params).Get<HdxPickFromRenderBufferTaskParams>();

            SelectionBufferPaths const& selectionBufferPaths =
                selectionSettingsProvider->GetBufferPaths();

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

    HdxPickFromRenderBufferTaskParams initialParams;
    initialParams.cameraId = getLayerSettings()->renderParams.camera;
    initialParams.viewport = kDefaultViewport;

    return taskManager->AddTask<HdxPickFromRenderBufferTask>(_tokens->pickFromRenderBufferTask,
        initialParams, fnCommit, PXR_NS::SdfPath(), TaskManager::InsertionOrder::insertBefore,
        TaskFlagsBits::kPickingTaskBit);
}

SdfPath CreateBoundingBoxTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr)
{
    auto fnCommit = [renderSettingsWeakPtr](TaskManager::GetTaskValueFn const& fnGetValue,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            auto params    = fnGetValue(HdTokens->params).Get<HdxBoundingBoxTaskParams>();
            params.aovName = renderBufferSettings->GetViewportAov();
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    return taskManager->AddTask<HdxBoundingBoxTask>(
        _tokens->boundingBoxTask, HdxBoundingBoxTaskParams(), fnCommit);
}

SdfPath CreateAovInputTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr)
{
    auto fnCommit = [renderSettingsWeakPtr](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            auto const& aovData = renderBufferSettings->GetAovParamCache();

            AovInputTaskParams params;
            params.aovBufferPath   = aovData.aovBufferPath;
            params.depthBufferPath = aovData.depthBufferPath;
            params.aovBuffer       = aovData.aovBuffer;
            params.depthBuffer     = aovData.depthBuffer;
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    return taskManager->AddTask<AovInputTask>(
        _tokens->aovInputTask, AovInputTaskParams(), fnCommit);
}

SdfPath CreatePresentTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr,
    FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings, renderSettingsWeakPtr](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            HdxPresentTaskParams params;

            GfVec2i const& renderSize = renderBufferSettings->GetRenderBufferSize();
// ADSK: For pending changes to OpenUSD from Autodesk: hgiPresent.
#if defined(ADSK_OPENUSD_PENDING)
            HgiPresentInteropParams dstParams;

            auto& compParams               = dstParams.composition;
            compParams.colorSrcBlendFactor = HgiBlendFactorOne;
            compParams.colorDstBlendFactor = HgiBlendFactorOneMinusSrcAlpha;
            compParams.colorBlendOp        = HgiBlendOpAdd;
            compParams.alphaSrcBlendFactor = HgiBlendFactorOne;
            compParams.alphaDstBlendFactor = HgiBlendFactorOneMinusSrcAlpha;
            compParams.alphaBlendOp        = HgiBlendOpAdd;
            compParams.dstRegion           = GfRect2i({ 0, 0 }, renderSize[0], renderSize[1]);

            compParams.depthFunc = _GetDepthCompositing(getLayerSettings());

            params.enabled = getLayerSettings()->enablePresentation;

            dstParams.destination = _GetInteropDestination(*renderBufferSettings);

            params.destinationParams = dstParams;
#else  // official release
            params.enabled = getLayerSettings()->enablePresentation;

            // Note: This is unused and untested in the ViewportToolbox.
            auto const& aovParams = renderBufferSettings->GetAovParamCache();
            params.dstApi         = aovParams.presentApi;
            params.dstFramebuffer = aovParams.presentFramebuffer;
            params.dstRegion      = GfVec4i(0, 0, renderSize[0], renderSize[1]);
#endif // ADSK_OPENUSD_PENDING
       // Sets the task parameter value.
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    return taskManager->AddTask<HdxPresentTask>(
        _tokens->presentTask, HdxPresentTaskParams(), fnCommit);
}

template <class TRenderTask>
SdfPath CreateRenderTask(TaskManagerPtr& pTaskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsWeakPtr,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::TfToken const& materialTag,
    TfToken const& taskName, PXR_NS::SdfPath const& atPos = SdfPath::EmptyPath(),
    TaskManager::InsertionOrder order = TaskManager::InsertionOrder::insertAtEnd)
{
    TaskManager& taskManager = *pTaskManager;

    auto fnCommit =
        [taskName, materialTag, &taskManager, renderSettingsWeakPtr, getLayerSettings](
            TaskManager::GetTaskValueFn const&, TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            // Initialize the render task params with the layer render params.
            HdxRenderTaskParams params = getLayerSettings()->renderParams;

            // Set blend state and depth mask according to material tag (additive, masked, etc).
            SetBlendStateForMaterialTag(materialTag, &params);

            // NOTE: According to pxr::HdxRenderTaskParams, viewport is only used if framing is
            // invalid.
            params.viewport = kDefaultViewport;
            params.camera   = getLayerSettings()->renderParams.camera;

            hvt::AovParams const& aovData = renderBufferSettings->GetAovParamCache();

            // Only clear the frame for the first render task.
            // Ref: pxr::HdxTaskController::_CreateRenderTask().
            
            bool isFirstRenderTask = _GetFirstRenderTaskName(taskManager) == taskName;

            // With progressive rendering, only clear the first frame if there is no AOV inputs.
            if (isFirstRenderTask && renderBufferSettings->IsProgressiveRenderingEnabled())
            {
                isFirstRenderTask = aovData.hasNoAovInputs;
            }

            // Assign the proper aovBindings, following the need to clear the frame or not.
            // Ref: pxr::HdxTaskController::_CreateRenderTask().
            params.aovBindings =
                isFirstRenderTask ? aovData.aovBindingsClear : aovData.aovBindingsNoClear;

            if (materialTag == HdStMaterialTagTokens->translucent)
            {
                // OIT is using its own buffers which are only per pixel and not per
                // sample. Thus, we resolve the AOVs before starting to render any
                // OIT geometry and only use the resolved AOVs from then on.
                // Ref: pxr::HdxTaskController::_CreateRenderTask().
                params.useAovMultiSample = false;
            }
            else if (materialTag == HdStMaterialTagTokens->volume)
            {
                // Ref: pxr::HdxTaskController::SetRenderOutputs
                params.aovInputBindings = aovData.aovInputBindings;
                // See above comment about HdxRenderTaskParams::useAovMultiSample for OIT.
                params.useAovMultiSample = false;
            }

            // Update the clear color values for each render buffer ID, where applicable (excluding
            // the first render task).
            //
            // Ref: pxr::HdxTaskController::SetRenderOutputSettings()
            // NOTE: SetRenderOutputSettings() is only known to be used for setting the clear color
            //       in OpenUSD code (see UsdImagingGLEngine).
            //       The call is always performed in this order:
            //
            //      ```
            //         HdAovDescriptor colorAovDesc = _taskController->GetRenderOutputSettings();
            //         colorAovDesc.clearValue = VtValue(clearColor);
            //         _taskController->SetRenderOutputSettings(colorAovDesc);
            //      ```
            // TODO: A possible improvement would be to prepare "aovData.aovBindingsClear" with
            //       the proper clear value in advance, to avoid doing it in the loop below.
            for (size_t i = 0; i < params.aovBindings.size(); ++i)
            {
                auto it = aovData.outputClearValues.find(params.aovBindings[i].renderBufferId);
                if (it != aovData.outputClearValues.end())
                {
                    params.aovBindings[i].clearValue = isFirstRenderTask ? it->second : VtValue();
                }
            }

            // Set task parameters.
            fnSetValue(HdTokens->params, VtValue(params));

            // Set task render tags.
            fnSetValue(HdTokens->renderTags, VtValue(getLayerSettings()->renderTags));

            // Set task collection.
            HdRprimCollection taskCollection = getLayerSettings()->collection;
            taskCollection.SetMaterialTag(materialTag);
            fnSetValue(HdTokens->collection, VtValue(taskCollection));
        }
    };

    // Creates a dedicated version of the render params.
    HdxRenderTaskParams renderParams = getLayerSettings()->renderParams;

    // Set the blend state based on material tag.
    SetBlendStateForMaterialTag(materialTag, &renderParams);

    return taskManager.AddRenderTask<TRenderTask>(taskName, renderParams, fnCommit, atPos, order);
}

HVT_API extern PXR_NS::SdfPath CreateRenderTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::TfToken const& materialTag)
{
    const TfToken taskName = GetRenderTaskPathLeaf(materialTag);

    if (materialTag == HdStMaterialTagTokens->translucent)
    {
        return CreateRenderTask<HdxOitRenderTask>(
            taskManager, renderSettingsProvider, getLayerSettings, materialTag, taskName);
    }
    else if (materialTag == HdStMaterialTagTokens->volume)
    {
        return CreateRenderTask<HdxOitVolumeRenderTask>(
            taskManager, renderSettingsProvider, getLayerSettings, materialTag, taskName);
    }
    return CreateRenderTask<HdxRenderTask>(
        taskManager, renderSettingsProvider, getLayerSettings, materialTag, taskName);
}

SdfPath CreateSkyDomeTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::SdfPath const& atPos,
    TaskManager::InsertionOrder order)
{
    TfToken const& materialTag = HdStMaterialTagTokens->defaultMaterialTag;
    const TfToken taskName     = _tokens->skydomeTask;

    return CreateRenderTask<HdxSkydomeTask>(
        taskManager, renderSettingsProvider, getLayerSettings, materialTag, taskName, atPos, order);
}

SdfPath CreateCopyTask(TaskManagerPtr& taskManager, SdfPath const& atPos /*= PXR_NS::SdfPath()*/)
{
    // Appends the copy task to copy the non-MSAA results back to the MSAA buffer.
    return taskManager->AddTask<CopyTask>(CopyTask::GetToken(), CopyTaskParams(), nullptr, atPos,
        TaskManager::InsertionOrder::insertBefore, TaskFlagsBits::kExecutableBit);
}

} // namespace HVT_NS
