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

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

SdfPath GetPickedPrim(FramePass* pass, GfMatrix4d const& pickingMatrix,
    ViewportRect const& viewport, GfMatrix4d const& viewMatrix)
{
    if (!pass || !pass->IsInitialized())
    {
        TF_CODING_ERROR("The frame pass is null or not initialized.");
        return {};
    }

    SdfPath hitPrimPath;

    pass->params().viewInfo.viewport         = viewport;
    pass->params().viewInfo.viewMatrix       = viewMatrix;
    pass->params().viewInfo.projectionMatrix = pickingMatrix;

    pass->UpdateScene();

    const HdSelectionSharedPtr hits = pass->Pick(HdxPickTokens->pickPrimsAndInstances);
    const SdfPathVector allPrims    = hits->GetAllSelectedPrimPaths();
    if (allPrims.size() > 0)
    {
        // TODO: Need to process all paths?
        hitPrimPath = SdfPath(allPrims[0]);
    }

    return hitPrimPath;
}

HdSelectionSharedPtr PickObjects(FramePass* pass, GfMatrix4d const& pickingMatrix,
    ViewportRect const& viewport, GfMatrix4d const& viewMatrix, TfToken const& objectType)
{
#if defined(ADSK_OPENUSD)
#define AGP_DEEP_SELECTION
#endif

#ifdef AGP_DEEP_SELECTION
    // deep selection is not supported using the render graph (yet)
    if (pass && pass->IsInitialized())
    {
        pass->params().viewInfo.viewport         = viewport;
        pass->params().viewInfo.viewMatrix       = viewMatrix;
        pass->params().viewInfo.projectionMatrix = pickingMatrix;

        pass->UpdateScene();

        return pass->Pick(objectType, HdxPickTokens->resolveDeep);
    }
#endif

    return {};
}

void HighlightSelection(FramePass* pass, SdfPathSet const& highlightPaths)
{
    if (!pass)
    {
        TF_CODING_ERROR("The frame pass is null.");
        return;
    }

    const HdSelectionSharedPtr selection = ViewportEngine::PrepareSelection(highlightPaths);
    pass->SetSelection(selection);
}

} // namespace hvt