// Copyright 2026 Autodesk, Inc.
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

#include <hvt/tasks/greyScaleTask.h>

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

namespace HVT_NS
{

namespace
{

const TfToken& _GetShaderPath()
{
    static const TfToken shader {
        GetShaderPath("greyScale.glslfx").generic_u8string(), TfToken::Immortal
    };
    return shader;
}

} // namespace

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "GreyScale::Fragment"))(greyScaleShader));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

GreyScaleTask::GreyScaleTask(HdSceneDelegate*, SdfPath const& uid) : HdxTask(uid) {}

void GreyScaleTask::_Sync(
    HdSceneDelegate* /* delegate */, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_shader)
    {
        _shader = std::make_unique<HdxFullscreenShader>(_GetHgi(), "GreyScale Shader");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = _tokens->shader.GetString();
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void GreyScaleTask::Prepare(HdTaskContext*, HdRenderIndex*) {}

void GreyScaleTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_HasTaskContextData(ctx, HdAovTokens->color) ||
        !_HasTaskContextData(ctx, HdxAovTokens->colorIntermediate))
    {
        TF_CODING_ERROR("Missing color or color intermediate texture.");
        return;
    }

    HgiTextureHandle aovTexture;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovTexture);
    HgiTextureHandle aovTextureIntermediate;
    _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &aovTextureIntermediate);

    aovTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    _shader->BindTextures({ aovTexture });
    _shader->Draw(aovTextureIntermediate, HgiTextureHandle());
    aovTexture->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);

    _ToggleRenderTarget(ctx);
}

const TfToken& GreyScaleTask::GetToken()
{
    static const TfToken token { "greyScaleTask", TfToken::Immortal };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const GreyScaleTaskParams& /* pv */)
{
    out << "GreyScaleTask Params: (none)";
    return out;
}

bool operator==(const GreyScaleTaskParams& /* lhs */, const GreyScaleTaskParams& /* rhs */)
{
    return true;
}

bool operator!=(const GreyScaleTaskParams& lhs, const GreyScaleTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
