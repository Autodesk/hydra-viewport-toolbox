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

#include <hvt/engine/taskUtils.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

GfVec4i ToVec4i(GfVec4d const& v)
{
    return GfVec4i(int(v[0]), int(v[1]), int(v[2]), int(v[3]));
}

bool IsStormRenderDelegate(HdRenderIndex const* renderIndex)
{
    return dynamic_cast<HdStRenderDelegate*>(renderIndex->GetRenderDelegate());
}

TfToken GetRenderingBackendName(HdRenderIndex const* renderIndex)
{
    HdDriverVector const& drivers = renderIndex->GetDrivers();
    for (HdDriver* hdDriver : drivers)
    {
        if ((hdDriver->name == HgiTokens->renderDriver) && hdDriver->driver.IsHolding<Hgi*>())
        {
            return hdDriver->driver.UncheckedGet<Hgi*>()->GetAPIName();
        }
    }

    return HgiTokens->OpenGL;
}

void SetBlendStateForMaterialTag(TfToken const& materialTag, HdxRenderTaskParams& renderParams)
{
    if (materialTag == HdStMaterialTagTokens->additive)
    {
        // Additive blend -- so no sorting of drawItems is needed
        renderParams.blendEnable = true;
        // For color, we are setting all factors to ONE.
        //
        // This means we are expecting pre-multiplied alpha coming out
        // of the shader: vec4(rgb*a, a).  Setting ColorSrc to
        // HdBlendFactorSourceAlpha would give less control on the
        // shader side, since it means we would force a pre-multiplied
        // alpha step on the color coming out of the shader.
        //
        renderParams.blendColorOp        = HdBlendOpAdd;
        renderParams.blendColorSrcFactor = HdBlendFactorOne;
        renderParams.blendColorDstFactor = HdBlendFactorOne;

        // For alpha, we set the factors so that the alpha in the
        // framebuffer won't change.  Recall that the geometry in the
        // additive render pass is supposed to be emitting light but
        // be fully transparent, that is alpha = 0, so that the order
        // in which it is drawn doesn't matter.
        renderParams.blendAlphaOp        = HdBlendOpAdd;
        renderParams.blendAlphaSrcFactor = HdBlendFactorZero;
        renderParams.blendAlphaDstFactor = HdBlendFactorOne;

        // Translucent objects should not block each other in depth buffer
        renderParams.depthMaskEnable = false;

        // Since we are using alpha blending, we disable screen door
        // transparency for this renderpass.
        renderParams.enableAlphaToCoverage = false;

#if defined(DRAW_ORDER)
    }
    else if (materialTag == HdStMaterialTagTokens->draworder)
    {

        // ResultColor = SrcColor * SrcAlpha + DestColor * (1-SrcAlpha)
        renderParams.blendEnable         = true;
        renderParams.blendColorOp        = HdBlendOpAdd;
        renderParams.blendColorSrcFactor = HdBlendFactorSrcAlpha;
        renderParams.blendColorDstFactor = HdBlendFactorOneMinusSrcAlpha;

        renderParams.blendAlphaOp        = HdBlendOpAdd;
        renderParams.blendAlphaSrcFactor = HdBlendFactorOne;
        renderParams.blendAlphaDstFactor = HdBlendFactorZero;

        // Disable depth buffer for draworder.
        renderParams.depthMaskEnable = false;

        // Since we are using alpha blending, we disable screen door
        // transparency for this renderpass.
        renderParams.enableAlphaToCoverage = false;
#endif
    }
    else if (materialTag == HdStMaterialTagTokens->defaultMaterialTag ||
        materialTag == HdStMaterialTagTokens->masked)
    {
        // The default and masked material tags share the same blend state, but
        // we classify them as separate because in the general case, masked
        // materials use fragment shader discards while the defaultMaterialTag
        // should not.
        renderParams.blendEnable           = false;
        renderParams.depthMaskEnable       = true;
        renderParams.enableAlphaToCoverage = true;
    }
}

SdfPath GetRenderTaskPath(SdfPath const& controllerId, TfToken const& materialTag)
{
    TfToken leafName = GetRenderTaskPathLeaf(materialTag);
    return controllerId.AppendChild(leafName);
}

TfToken GetRenderTaskPathLeaf(TfToken const& materialTag)
{
    std::string str = TfStringPrintf("renderTask_%s", materialTag.GetText());
    std::replace(str.begin(), str.end(), ':', '_');
    return TfToken(str);
}

SdfPath GetAovPath(SdfPath const& parentId, TfToken const& aov)
{
    std::string identifier = std::string("aov_") + TfMakeValidIdentifier(aov.GetString());
    return parentId.AppendChild(TfToken(identifier));
}

VtValue GetDefaultCollection(
    HVT_NS::BasicLayerParams const* layerSettings, TfToken const& materialTag)
{
    // Set task collection.
    HdRprimCollection taskCollection = layerSettings->collection;
    taskCollection.SetMaterialTag(materialTag);
    return VtValue(taskCollection);
}

bool CanClearAOVs(TaskManager const& taskManager, TfToken const& taskName,
    RenderBufferSettingsProvider const& renderBufferSettings)
{
    // Only clear the frame for the first render task.
    // Ref: pxr::HdxTaskController::_CreateRenderTask().

    bool isFirstRenderTask = GetFirstRenderTaskName(taskManager) == taskName;

    // With progressive rendering, only clear the first frame if there is no AOV inputs.
    if (isFirstRenderTask && renderBufferSettings.IsProgressiveRenderingEnabled())
    {
        isFirstRenderTask = renderBufferSettings.GetAovParamCache().hasNoAovInputs;
    }

    return isFirstRenderTask;
}

bool CanUseMsaa(TfToken const& materialTag)
{
    // OIT is using its own buffers which are only per pixel and not per
    // sample. Thus, we resolve the AOVs before starting to render any
    // OIT geometry and only use the resolved AOVs from then on. Same for volume.
    // Ref: pxr::HdxTaskController::_CreateRenderTask().
    return (materialTag != HdStMaterialTagTokens->translucent &&
        materialTag != HdStMaterialTagTokens->volume);
}

TfToken GetFirstRenderTaskName(const TaskManager& taskManager)
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

HdRenderPassAovBindingVector GetAovBindings(TaskManager const& taskManager,
    TfToken const& taskName, RenderBufferSettingsProvider const& renderBufferSettings)
{
    hvt::AovParams const& aovData = renderBufferSettings.GetAovParamCache();

    bool clearAOVs = CanClearAOVs(taskManager, taskName, renderBufferSettings);

    // Assign the proper aovBindings, following the need to clear the frame or not.
    // Ref: pxr::HdxTaskController::_CreateRenderTask().
    HdRenderPassAovBindingVector aovBindings =
        clearAOVs ? aovData.aovBindingsClear : aovData.aovBindingsNoClear;

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
    for (size_t i = 0; i < aovBindings.size(); ++i)
    {
        auto it = aovData.outputClearValues.find(aovBindings[i].renderBufferId);
        if (it != aovData.outputClearValues.end())
        {
            aovBindings[i].clearValue = clearAOVs ? it->second : VtValue();
        }
    }

    return aovBindings;
}

} // namespace HVT_NS
