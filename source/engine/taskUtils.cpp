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

} // namespace HVT_NS
