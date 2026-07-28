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

#include <hvt/engine/framePassUtils.h>

#include <hvt/engine/taskBackend.h>

#if HVT_SI_TASK_BACKEND_SUPPORTED
#include "si/taskSIBackend.h"
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

SdfPath GetPickedPrim(FramePass* pass, GfMatrix4d const& pickingMatrix,
    CameraUtilFraming const& framing, GfMatrix4d const& viewMatrix)
{
    if (!pass || !pass->IsInitialized())
    {
        TF_CODING_ERROR("The frame pass is null or not initialized.");
        return {};
    }

    SdfPath hitPrimPath;

    pass->params().viewInfo.framing          = framing;
    pass->params().viewInfo.viewMatrix       = viewMatrix;
    pass->params().viewInfo.projectionMatrix = pickingMatrix;

    pass->UpdateScene();

    const HdSelectionSharedPtr hits = pass->Pick(HdxPickTokens->pickPrimsAndInstances);
    const SdfPathVector allPrims    = hits->GetAllSelectedPrimPaths();
    if (!allPrims.empty())
    {
        // TODO: Need to process all paths?
        hitPrimPath = SdfPath(allPrims[0]);
    }

    return hitPrimPath;
}

void HighlightSelection(
    FramePass* pass, SdfPathSet const& selectionPaths, SdfPathSet const& locatorPaths)
{
    if (!pass)
    {
        TF_CODING_ERROR("The frame pass is null.");
        return;
    }

    const HdSelectionSharedPtr selection = ViewportEngine::PrepareSelection(selectionPaths);
    if (!locatorPaths.empty())
        ViewportEngine::PrepareSelection(
            locatorPaths, HdSelection::HighlightMode::HighlightModeLocate, selection);

    pass->SetSelection(selection);
}

PXR_NS::HdRetainedSceneIndexRefPtr const& GetRetainedSceneIndex(
    [[maybe_unused]] TaskBackend const* taskBackend)
{
    static const PXR_NS::HdRetainedSceneIndexRefPtr empty;

#if HVT_SI_TASK_BACKEND_SUPPORTED
    // A null pointer yields a null dynamic_cast result, so this also guards against a null input.
    auto const* si = dynamic_cast<TaskSIBackend const*>(taskBackend);
    if (!si)
    {
        TF_CODING_ERROR("GetRetainedSceneIndex requires a valid TaskSIBackend.");
        return empty;
    }
    return si->GetRetainedSceneIndex();
#else
    TF_CODING_ERROR(
        "GetRetainedSceneIndex requires the legacy task schema (USD >= 25.05).");
    return empty;
#endif
}

} // namespace HVT_NS