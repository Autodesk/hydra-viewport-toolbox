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

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/usd/sdf/path.h>

#include <tuple>

/// The TaskCreation helper main responsibility is to define the TaskCommitFn callbacks.
/// It is also responsible for providing the default list of task, to mimic OpenUSD's Task
/// Controller.
namespace HVT_NS
{
using FnGetLayerSettings = std::function<BasicLayerParams const*()>;

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
    FnGetLayerSettings const& getLayerSettings, PXR_NS::SdfPath const& atPos,
    TaskManager::InsertionOrder order);
} // namespace HVT_NS
