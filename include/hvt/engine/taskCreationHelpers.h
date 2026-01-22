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
#include <hvt/engine/taskManager.h>
#include <hvt/engine/taskUtils.h>

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/path.h>

#include <tuple>

/// The TaskCreation helper main responsibility is to define the TaskCommitFn callbacks.
/// It is also responsible for providing the default list of task, to mimic OpenUSD's Task
/// Controller.
namespace HVT_NS
{

using FnGetLayerSettings = std::function<BasicLayerParams const*()>;

// This structure holds various parameters used for updating the render task parameters, before
// HdEngine::Execute() is called.
// Some of the update logic depends on the order of the task in the TaskManager, the material type,
// etc. Hence the need for all these inputs.
struct HVT_API UpdateRenderTaskFnInput
{
    PXR_NS::TfToken taskName;
    PXR_NS::TfToken materialTag;
    TaskManager* pTaskManager;
    FnGetLayerSettings getLayerSettings;
};

struct HVT_API RenderTaskData
{
    PXR_NS::HdxRenderTaskParams params;
    PXR_NS::TfTokenVector renderTags;
    PXR_NS::HdRprimCollection collection;
};

/// Creates the default list of tasks to render a scene based on the render delegate plugin.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \note This instance needs to be valid even after the task is created, as it will be consulted
/// to update the task parameters before rendering.
/// \param lightingSettingsProvider An accessor instance for lighting settings.
/// \note This instance needs to be valid even after the task is created, as it will be consulted
/// to update the task parameters before rendering.
/// \param selectionSettingsProvider An accessor instance for selection settings.
/// \note This instance needs to be valid even after the task is created, as it will be consulted
/// to update the task parameters before rendering.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The list of task and render task identifiers.
HVT_API extern std::tuple<PXR_NS::SdfPathVector, PXR_NS::SdfPathVector> CreateDefaultTasks(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    SelectionSettingsProviderWeakPtr const& selectionSettingsProvider,
    FnGetLayerSettings const& getLayerSettings);

/// Creates the minimal list of tasks to render a scene based on the render delegate plugin.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \param lightingSettingsProvider An accessor instance for lighting settings.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The list of task and render task identifiers.
HVT_API extern std::tuple<PXR_NS::SdfPathVector, PXR_NS::SdfPathVector> CreateMinimalTasks(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    FnGetLayerSettings const& getLayerSettings);

/// Creates the lighting task.
/// \param taskManager The task manager to update.
/// \param lightingSettingsProvider The light setting accessor.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateLightingTask(TaskManagerPtr& taskManager,
    LightingSettingsProviderWeakPtr const& lightingSettingsProvider,
    FnGetLayerSettings const& getLayerSettings);

/// Creates the shadow task.
/// \param taskManager The task manager to update.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateShadowTask(
    TaskManagerPtr& taskManager, FnGetLayerSettings const& getLayerSettings);

/// Creates the color correction task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateColorCorrectionTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings);

/// Creates the OIT resolve task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateOitResolveTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider);

/// Creates the selection task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateSelectionTask(
    TaskManagerPtr& taskManager, SelectionSettingsProviderWeakPtr const& selectionSettingsProvider);

/// Creates the colorize selection task.
/// \param taskManager The task manager to update.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateColorizeSelectionTask(
    TaskManagerPtr& taskManager, SelectionSettingsProviderWeakPtr const& selectionSettingsProvider);

/// Creates the AOV visualization task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateVisualizeAovTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider);

/// Creates the pick task.
/// \param taskManager The task manager to update.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreatePickTask(
    TaskManagerPtr& taskManager, FnGetLayerSettings const& getLayerSettings);

/// Creates the pick from the render buffer task.
/// \param taskManager The task manager to update.
/// \param selectionSettingsProvider An accessor instance for selection settings.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreatePickFromRenderBufferTask(TaskManagerPtr& taskManager,
    SelectionSettingsProviderWeakPtr const& selectionSettingsProvider,
    FnGetLayerSettings const& getLayerSettings);

/// Creates the bounding box task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateBoundingBoxTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider);

/// Creates the AOV render buffers.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateAovInputTask(
    TaskManagerPtr& taskManager, RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider);

/// Creates the present task i.e., displays the rendering result (using a frame buffer in OpenGL).
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreatePresentTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings);

/// Creates the render task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \param materialTag The material tag associated with the render task.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateRenderTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::TfToken const& materialTag);

/// Creates the sky dome task.
/// \param taskManager The task manager to update.
/// \param renderSettingsProvider An accessor instance for render buffer and AOV settings.
/// \param getLayerSettings Callback for accessing the layer settings.
/// \param atPos The unique identifier of the task where to insert this new task.
/// \param order The insertion order relative to atPos.
/// \return The task unique identifier.
HVT_API extern PXR_NS::SdfPath CreateSkyDomeTask(TaskManagerPtr& taskManager,
    RenderBufferSettingsProviderWeakPtr const& renderSettingsProvider,
    FnGetLayerSettings const& getLayerSettings, PXR_NS::SdfPath const& atPos = PXR_NS::SdfPath(),
    TaskManager::InsertionOrder order = TaskManager::InsertionOrder::insertAtEnd);

/// Default callback used for updating the render task parameters, collection and render tags.
/// \param pRenderBufferSettings A valid pointer to the render buffer settings provider.
/// \param inputParams The input parameters for the update callback.
HVT_API extern RenderTaskData DefaultRenderTaskUpdateFn(
    RenderBufferSettingsProvider* pRenderBufferSettings,
    UpdateRenderTaskFnInput const& inputParams);

using FnRenderTaskUpdate =
    std::function<RenderTaskData(RenderBufferSettingsProvider*, UpdateRenderTaskFnInput const&)>;

/// Creates the render task.
/// \param renderSettingsWeakPtr A weak pointer to the render buffer settings provider.
/// \param inParams The input parameters for the render task.
/// \param updateRenderTaskFn The callback used for updating the render task parameters, collection and render tags.
/// \param atPos The unique identifier of the task where to insert this new task.
/// \param order The insertion order relative to atPos.
/// \return The task unique identifier.
template <class TRenderTask>
HVT_API extern PXR_NS::SdfPath CreateRenderTask(
    RenderBufferSettingsProviderWeakPtr renderSettingsWeakPtr, UpdateRenderTaskFnInput inParams,
    FnRenderTaskUpdate updateRenderTaskFn = DefaultRenderTaskUpdateFn,
    PXR_NS::SdfPath const& atPos      = PXR_NS::SdfPath::EmptyPath(),
    TaskManager::InsertionOrder order = TaskManager::InsertionOrder::insertAtEnd)
{
    auto fnCommit =
        [renderSettingsWeakPtr, inParams, updateRenderTaskFn](
            TaskManager::GetTaskValueFn const&,
                        TaskManager::SetTaskValueFn const& fnSetValue)
    {
        if (const auto renderBufferSettings = renderSettingsWeakPtr.lock())
        {
            RenderTaskData taskData = updateRenderTaskFn(renderBufferSettings.get(), inParams);
            
            // Set task parameters.
            fnSetValue(HdTokens->params, PXR_NS::VtValue(taskData.params));

            // Set task render tags.
            fnSetValue(HdTokens->renderTags, PXR_NS::VtValue(taskData.renderTags));

            // Set task collection.
            fnSetValue(HdTokens->collection, PXR_NS::VtValue(taskData.collection));
        }
    };

    // Creates a dedicated version of the render params.
    PXR_NS::HdxRenderTaskParams renderParams = inParams.getLayerSettings()->renderParams;

    // Set the blend state based on material tag.
    SetBlendStateForMaterialTag(inParams.materialTag, renderParams);

    return inParams.pTaskManager->AddRenderTask<TRenderTask>(
        inParams.taskName, renderParams, fnCommit, atPos, order);
}
} // namespace HVT_NS
