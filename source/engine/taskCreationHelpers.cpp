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
#include <hvt/tasks/visualizeAovTask.h>

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
HgiCompareFunction _GetDepthCompositing(BasicLayerParams const& layerSettings [[maybe_unused]])
{
#ifdef AGP_CONTROLABLE_DEPTH_COMPOSITING
    return layerSettings.depthCompare;
#else
    return HgiCompareFunctionLEqual;
#endif
}

// Helper function to get the interop destination from the render parameters.
// \params renderParams The render parameters to get the interop destination from.
// \return The interop destination.
HgiPresentInteropHandle _GetInteropHandleDestination(PresentationParams const& inPresentParam)
{
    // First, check if the data is of type HgiPresentInteropHandle (new use cases).
    if (inPresentParam.framebufferHandle.IsHolding<HgiPresentInteropHandle>())
    {
        return inPresentParam.framebufferHandle.UncheckedGet<HgiPresentInteropHandle>();
    }

#if defined(PXR_GL_SUPPORT_ENABLED)
    // Second, check if the framebufferHandle just contains a numeric framebuffer.
    // Note: Not providing a framebuffer to present to is a normal use case. No warning needed.
    uint32_t framebuffer = 0;
    if (inPresentParam.framebufferHandle.IsHolding<uint32_t>())
    {
        framebuffer = inPresentParam.framebufferHandle.UncheckedGet<uint32_t>();
    }

    return HgiPresentGLInteropHandle { framebuffer };

#else
    TF_WARN("Present GL interop not supported");
    return HgiPresentNullInteropHandle {};
#endif // PXR_GL_SUPPORT_ENABLED
}

HgiPresentWindowParams _GetPresentWindowDestination(
    RenderBufferSettingsProvider const& renderParams, BasicLayerParams const& layerSettings)
{
    PresentationParams const& presentParams = renderParams.GetPresentationParams();

    HgiPresentWindowParams presentWindowParams {};
    presentWindowParams.wantVsync = presentParams.windowVsync;

    if (presentParams.windowHandle.IsHolding<HgiPresentWindowHandle>())
    {
        presentWindowParams.window =
            presentParams.windowHandle.UncheckedGet<HgiPresentWindowHandle>();
    }
    else
    {
        TF_CODING_ERROR(
            "Invalid HgiPresentWindowHandle in PresentationParams::windowHandle VtValue.");
    }

    // Set the window color space settings.
    if (layerSettings.colorspace.IsEmpty() ||
        layerSettings.colorspace == HdxColorCorrectionTokens->disabled)
    {
        // Linear sRGB present to sRGB texture.
        presentWindowParams.srcColorSpace     = GfColorSpaceNames->LinearRec709;
        presentWindowParams.surfaceColorSpace = GfColorSpaceNames->SRGBRec709;
    }
    else if (layerSettings.colorspace == HdxColorCorrectionTokens->sRGB)
    {
        // sRGB present to sRGB texture.
        presentWindowParams.srcColorSpace     = GfColorSpaceNames->SRGBRec709;
        presentWindowParams.surfaceColorSpace = GfColorSpaceNames->SRGBRec709;
    }
    else if (layerSettings.colorspace == HdxColorCorrectionTokens->openColorIO)
    {
        // Pass-through.
        presentWindowParams.srcColorSpace     = GfColorSpaceNames->Raw;
        presentWindowParams.surfaceColorSpace = GfColorSpaceNames->Raw;
    }
    else
    {
        TF_CODING_ERROR("Unknown colorCorrectionMode token");
    }

    // TODO: OGSMOD-8066 Find a way to set HgiPresentWindowParams::preferredSurfaceFormat
    // using HdStHgiConversions::GetHgiFormat(HdFormat fmt), where fmt is the format of the
    // aov_color descriptor used by the rendering tasks.

    return presentWindowParams;
}

HgiPresentInteropParams _GetPresentInteropDestination(
    RenderBufferSettingsProvider const& renderParams, BasicLayerParams const& layerSettings)
{
    PresentationParams const& inPresentParams = renderParams.GetPresentationParams();
    GfVec2i const& renderSize = renderParams.GetRenderBufferSize();

    HgiPresentInteropParams dstParams;
    if (inPresentParams.compositionParams.IsHolding<HgiPresentCompositionParams>())
    {
        dstParams.composition =
            inPresentParams.compositionParams.UncheckedGet<HgiPresentCompositionParams>();
    }
    else
    {
        dstParams.composition.colorSrcBlendFactor = HgiBlendFactorOne;
        dstParams.composition.colorDstBlendFactor = HgiBlendFactorOneMinusSrcAlpha;
        dstParams.composition.colorBlendOp        = HgiBlendOpAdd;
        dstParams.composition.alphaSrcBlendFactor = HgiBlendFactorOne;
        dstParams.composition.alphaDstBlendFactor = HgiBlendFactorOneMinusSrcAlpha;
        dstParams.composition.alphaBlendOp        = HgiBlendOpAdd;
        dstParams.composition.depthFunc = _GetDepthCompositing(layerSettings);
    }
    dstParams.composition.dstRegion = GfRect2i({ 0, 0 }, renderSize[0], renderSize[1]);

    dstParams.destination = _GetInteropHandleDestination(inPresentParams);

    return dstParams;
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
#if PXR_VERSION <= 2508
        params.enableSceneMaterials = getLayerSettings()->renderParams.enableSceneMaterials;
#endif
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
// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
            auto params    = fnGetValue(HdTokens->params).Get<HdxVisualizeAovTaskParams>();
#else
            auto params    = fnGetValue(HdTokens->params).Get<VisualizeAovTaskParams>();
#endif // ADSK_OPENUSD_PENDING
            params.aovName = renderBufferSettings->GetViewportAov();
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

// ADSK: For pending changes to OpenUSD from Autodesk.
#if defined(ADSK_OPENUSD_PENDING)
    return taskManager->AddTask<HdxVisualizeAovTask>(
        _tokens->visualizeAovTask, HdxVisualizeAovTaskParams(), fnCommit);
#else
    return taskManager->AddTask<VisualizeAovTask>(
        _tokens->visualizeAovTask, VisualizeAovTaskParams(), fnCommit);
#endif // ADSK_OPENUSD_PENDING
}

SdfPath CreatePickTask(TaskManagerPtr& taskManager, FnGetLayerSettings const& getLayerSettings)
{
    auto fnCommit = [getLayerSettings](TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        HdxPickTaskParams pickParams;

        pickParams.cullStyle            = getLayerSettings()->renderParams.cullStyle;
#if PXR_VERSION <= 2508
        pickParams.enableSceneMaterials = getLayerSettings()->renderParams.enableSceneMaterials;
#endif

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
// ADSK: For pending changes to OpenUSD from Autodesk: hgiPresent.
#if defined(ADSK_OPENUSD_PENDING)

            auto const& inPresentParams = renderBufferSettings->GetPresentationParams();

            params.enabled = getLayerSettings()->enablePresentation;

            if (!inPresentParams.windowHandle.IsEmpty())
            {
                params.destinationParams =
                    _GetPresentWindowDestination(*renderBufferSettings, *getLayerSettings());
            }
            else
            {
                params.destinationParams =
                    _GetPresentInteropDestination(*renderBufferSettings, *getLayerSettings());
            }

#else  // official release

            GfVec2i const& renderSize = renderBufferSettings->GetRenderBufferSize();

            params.enabled = getLayerSettings()->enablePresentation;

            // Note: This is unused and untested in the ViewportToolbox.
            auto const& presentParams = renderBufferSettings->GetPresentationParams();
            params.dstApi             = presentParams.api;
            params.dstFramebuffer     = presentParams.framebufferHandle;

            params.dstRegion = GfVec4i(0, 0, renderSize[0], renderSize[1]);
#endif // ADSK_OPENUSD_PENDING
       // Sets the task parameter value.
            fnSetValue(HdTokens->params, VtValue(params));
        }
    };

    return taskManager->AddTask<HdxPresentTask>(
        _tokens->presentTask, HdxPresentTaskParams(), fnCommit);
}

HVT_API extern PXR_NS::SdfPath CreateRenderTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::TfToken const& materialTag)
{
    UpdateRenderTaskFnInput updateCallbackParams;
    updateCallbackParams.taskName              = GetRenderTaskPathLeaf(materialTag);
    updateCallbackParams.materialTag           = materialTag;
    updateCallbackParams.pTaskManager          = taskManager.get();
    updateCallbackParams.getLayerSettings      = getLayerSettings;

    if (materialTag == HdStMaterialTagTokens->translucent)
    {
        return CreateRenderTask<HdxOitRenderTask>(renderSettingsProvider, updateCallbackParams);
    }
    else if (materialTag == HdStMaterialTagTokens->volume)
    {
        return CreateRenderTask<HdxOitVolumeRenderTask>(
            renderSettingsProvider, updateCallbackParams);
    }
    return CreateRenderTask<HdxRenderTask>(renderSettingsProvider, updateCallbackParams);
}

SdfPath CreateSkyDomeTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::SdfPath const& atPos,
    TaskManager::InsertionOrder order)
{
    UpdateRenderTaskFnInput updateCallbackParams;
    updateCallbackParams.taskName              = _tokens->skydomeTask;
    updateCallbackParams.materialTag           = HdStMaterialTagTokens->defaultMaterialTag;
    updateCallbackParams.pTaskManager          = taskManager.get();
    updateCallbackParams.getLayerSettings      = getLayerSettings;

    return CreateRenderTask<HdxSkydomeTask>(
        renderSettingsProvider, updateCallbackParams, DefaultRenderTaskUpdateFn, atPos, order);
}

RenderTaskData DefaultRenderTaskUpdateFn(
    RenderBufferSettingsProvider* renderBufferSettings, UpdateRenderTaskFnInput const& inputParams)
{
    RenderTaskData outData;

    // Initialize the render task params with the layer render params.
    outData.params = inputParams.getLayerSettings()->renderParams;

    // Set blend state and depth mask according to material tag (additive, masked, etc).
    SetBlendStateForMaterialTag(inputParams.materialTag, outData.params);

    // Viewport is only used if framing is invalid. See pxr::HdxRenderTaskParams.
    outData.params.viewport = kDefaultViewport;
    outData.params.camera   = inputParams.getLayerSettings()->renderParams.camera;

    // Translucent and volume can't use MSAA
    if (!CanUseMsaa(inputParams.materialTag))
    {
        outData.params.useAovMultiSample = false;
    }

    // GetAovBindings() applies aov buffer clearing logic for 1st render task.
    outData.params.aovBindings =
        GetDefaultAovBindings(*inputParams.pTaskManager, inputParams.taskName, *renderBufferSettings);

    if (inputParams.materialTag == HdStMaterialTagTokens->volume)
    {
        outData.params.aovInputBindings = renderBufferSettings->GetAovParamCache().aovInputBindings;
    }

    outData.renderTags = inputParams.getLayerSettings()->renderTags;
    outData.collection =
        GetDefaultCollection(inputParams.getLayerSettings(), inputParams.materialTag);

    return outData;
}

} // namespace HVT_NS
