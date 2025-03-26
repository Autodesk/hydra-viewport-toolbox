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

#include <hvt/tasks/composeTask.h>

#include <hvt/tasks/resources.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

#include <pxr/imaging/hdx/tokens.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

namespace
{

const TfToken& _GetShaderPath()
{
    static TfToken shader { hvt::GetShaderPath("compose.glslfx").generic_u8string() };
    return shader;
}

}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "Compose::Fragment"))(composeShader));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

// Defines all the uniforms for GPU computation.
struct Uniforms
{
    GfVec2f screenSize;
};

ComposeTask::ComposeTask(HdSceneDelegate*, SdfPath const& uid) : HdxTask(uid) {}

void ComposeTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_shader)
    {
        _shader = std::make_unique<HdxFullscreenShader>(_GetHgi(), "Compose Shader");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = _tokens->shader.GetString();
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "screenSize", "vec2");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);
    }

    // Updates the parameters.

    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        ComposeTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            if (_params != params)
            {
                _params = params;
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void ComposeTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
}

void ComposeTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_HasTaskContextData(ctx, HdAovTokens->color))
    {
        TF_CODING_ERROR("Missing color texture.");
        return;
    }

    // Get the final destinations.

    HgiTextureHandle aovColor;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovColor);

    // Get the intermediate color on which to make the blend operation.

    HgiTextureHandle aovIntermediateColor;
    _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &aovIntermediateColor);

    // Update with the new screen size.

    HgiTextureDesc const& dstDesc = aovColor->GetDescriptor();

    GfVec2f screenSize { static_cast<float>(dstDesc.dimensions[0]),
        static_cast<float>(dstDesc.dimensions[1]) };
    _shader->SetShaderConstants(sizeof(screenSize), &screenSize);

    // The code should copy the color source in the intermediate color texture, blend the
    // current color on top and finally, switch the intermediate as the new color texture.

    // Step 1 - Copy the source color into the current color intermediate.

    HgiTextureDesc const& srcDesc = _params.aovTextureHandle->GetDescriptor();

    if (dstDesc.dimensions[0] != srcDesc.dimensions[0] ||
        dstDesc.dimensions[1] != srcDesc.dimensions[1] ||
        dstDesc.dimensions[2] != srcDesc.dimensions[2])
    {
        TF_CODING_ERROR("Incompatible dimensions.");
        return;
    }

    _params.aovTextureHandle->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    // Default values when blending is off.
    _shader->SetBlendState(false, HgiBlendFactorZero, HgiBlendFactorZero, HgiBlendOpAdd,
        HgiBlendFactorZero, HgiBlendFactorZero, HgiBlendOpAdd);

    _shader->SetAttachmentLoadStoreOp(HgiAttachmentLoadOpDontCare, HgiAttachmentStoreOpStore);

    // Draw i.e., copy into the color intermediate texture.
    _shader->BindTextures({ _params.aovTextureHandle });
    _shader->Draw(aovIntermediateColor, HgiTextureHandle());

    _params.aovTextureHandle->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);

    // Step 2 - Blend the current color into the intermediate color.

    aovColor->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    // [CresRGBA] = [CsrcRGBA] * CsrcA + [CdestRGBA] * (1.0 - CsrcA)
    _shader->SetBlendState(true, HgiBlendFactorSrcAlpha, HgiBlendFactorOneMinusSrcAlpha,
        HgiBlendOpAdd, HgiBlendFactorSrcAlpha, HgiBlendFactorOneMinusSrcAlpha, HgiBlendOpAdd);

    // Use LoadOpLoad to blend the source color onto the intermediate color.
    _shader->SetAttachmentLoadStoreOp(HgiAttachmentLoadOpLoad, HgiAttachmentStoreOpStore);

    _shader->BindTextures({ aovColor });
    _shader->Draw(aovIntermediateColor, HgiTextureHandle());

    aovColor->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);

    // Step 3 - Switches the color texture with the intermediate color which contains the final
    // result.

    _ToggleRenderTarget(ctx);
}

const TfToken& ComposeTask::GetToken()
{
    static const TfToken token { "composeTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const HgiTextureHandle& handle)
{
    HgiTextureDesc desc = handle.Get()->GetDescriptor();
    out << handle.GetId() << " " << desc.debugName << " " << desc.dimensions[0] << "x"
        << desc.dimensions[1] << "x" << desc.dimensions[2] << "x" << desc.format;
    return out;
}

std::ostream& operator<<(std::ostream& out, const ComposeTaskParams& pv)
{
    out << "ComposeTask Params: " << pv.aovToken << " " << pv.aovTextureHandle;
    return out;
}

bool ComposeTaskParams::operator==(const ComposeTaskParams& rhs) const
{
    return aovToken == rhs.aovToken && aovTextureHandle == rhs.aovTextureHandle;
}

bool ComposeTaskParams::operator!=(const ComposeTaskParams& rhs) const
{
    return !(*this == rhs);
}

} // namespace hvt