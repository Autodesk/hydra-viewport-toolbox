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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <pxr/pxr.h>
PXR_NAMESPACE_USING_DIRECTIVE

#include "composeTaskHelpers.h"
#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePassUtils.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/sceneIndex/boundingBoxSceneIndex.h>
#include <hvt/sceneIndex/displayStyleOverrideSceneIndex.h>
#include <hvt/sceneIndex/wireFrameSceneIndex.h>
#include <hvt/tasks/composeTask.h>
#include <hvt/tasks/resources.h>

namespace TestHelpers
{
// Add the compose task to the second frame pass.
void AddComposeTask(
    TestHelpers::FramePassInstance const& framePass1, TestHelpers::FramePassInstance& framePass2)
{
    auto* main = framePass2.sceneFramePass.get();

    // Create the fnCommit for the compose task.

    auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                        hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
        const VtValue value           = fnGetValue(HdTokens->params);
        hvt::ComposeTaskParams params = value.Get<hvt::ComposeTaskParams>();

        // Gets the color texture information from the previous frame pass.
        params.aovToken         = HdAovTokens->color;
        params.aovTextureHandle = framePass1.sceneFramePass->GetRenderTexture(HdAovTokens->color);

        fnSetValue(HdTokens->params, VtValue(params));
    };

    // Adds the compose task.

    // NOTE: Usually, the compose task is right after the AOV input task, to let following
    // tasks process the AOV buffers as usual.
    const SdfPath aovInputTask =
        main->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->aovInputTask);

    const SdfPath composePath = main->GetTaskManager()->AddTask<hvt::ComposeTask>(
        hvt::ComposeTask::GetToken(), hvt::ComposeTaskParams(), fnCommit, aovInputTask,
        hvt::TaskManager::InsertionOrder::insertAfter);
}

// Renders the first frame pass i.e., do not display it and let the next frame pass doing it.
void RenderFirstFramePass(TestHelpers::FramePassInstance& framePass1, int width, int height,
    TestHelpers::TestStage const& stage)
{
    hvt::FramePassParams& params = framePass1.sceneFramePass->params();

    params.renderBufferSize          = GfVec2i(width, height);
    params.viewInfo.viewport         = { { 0, 0 }, { width, height } };
    params.viewInfo.viewMatrix       = stage.viewMatrix();
    params.viewInfo.projectionMatrix = stage.projectionMatrix();
    params.viewInfo.lights           = stage.defaultLights();
    params.viewInfo.material         = stage.defaultMaterial();
    params.viewInfo.ambient          = stage.defaultAmbient();

    params.colorspace      = HdxColorCorrectionTokens->disabled;
    params.backgroundColor = TestHelpers::ColorDarkGrey;
    params.selectionColor  = TestHelpers::ColorYellow;

    // Delays the display to the next frame pass.
    params.enablePresentation = false;

    // Renders the frame pass.
    framePass1.sceneFramePass->Render();
}

// Renders the second frame pass which also display the result.
void RenderSecondFramePass(TestHelpers::FramePassInstance& framePass2, int width, int height,
    TestHelpers::TestStage const& stage, hvt::RenderBufferBindings const& inputAOVs,
    bool clearBackground /*= true*/)
{
    auto& params = framePass2.sceneFramePass->params();

    params.renderBufferSize = GfVec2i(width, height);

    params.viewInfo.viewport         = { { 0, 0 }, { width, height } };
    params.viewInfo.viewMatrix       = stage.viewMatrix();
    params.viewInfo.projectionMatrix = stage.projectionMatrix();
    params.viewInfo.lights           = stage.defaultLights();
    params.viewInfo.material         = stage.defaultMaterial();
    params.viewInfo.ambient          = stage.defaultAmbient();

    params.colorspace      = HdxColorCorrectionTokens->disabled;
    params.clearBackground = clearBackground;
    // NoAlpha is mandatory for the alpha blending.
    params.backgroundColor = TestHelpers::ColorBlackNoAlpha;
    params.selectionColor  = TestHelpers::ColorYellow;

    // Gets the list of tasks to render but use the render buffers from the first frame
    // pass.
    const HdTaskSharedPtrVector renderTasks = framePass2.sceneFramePass->GetRenderTasks(inputAOVs);

    framePass2.sceneFramePass->Render(renderTasks);
}

} // namespace TestHelpers