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

#include <hvt/tasks/wboitResolveTask.h>

#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

#include "wboitTokens.h"

namespace HVT_NS
{

namespace
{

const TfToken& _GetShaderPath()
{
    static const TfToken shader { GetShaderPath("wboitResolve.glslfx").generic_u8string(), TfToken::Immortal };
    return shader;
}

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "WBOIT_Resolve::Fragment")));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

} // namespace

WbOitResolveTask::WbOitResolveTask(HdSceneDelegate*, SdfPath const& id) : HdxTask(id) {}

void WbOitResolveTask::_Sync(
    HdSceneDelegate* /* delegate */, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_shader)
    {
        _shader = std::make_unique<HdxFullscreenShader>(_GetHgi(), "WBOIT Resolve Shader");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = _tokens->shader.GetString();
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "buffer0", 0);
        HgiShaderFunctionAddTexture(&shaderDesc, "buffer1", 1);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void WbOitResolveTask::Prepare(HdTaskContext*, HdRenderIndex*) {}

void WbOitResolveTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (ctx->erase(HdxTokens->oitRequestFlag) == 0)
    {
        // Nothing to do when the WBOIT request flag is not present.
        return;
    }

    HgiTextureHandle aovTexture, buffer0, buffer1;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovTexture);
    _GetTaskContextData(ctx, _wboitTokens->hdxWboitBufferOne, &buffer0);
    _GetTaskContextData(ctx, _wboitTokens->hdxWboitBufferTwo, &buffer1);

    if (!aovTexture || !buffer0 || !buffer1)
    {
        TF_CODING_ERROR("Missing textures for WBOIT resolve task");
        return;
    }

    const auto oldLayout0 = buffer0->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    const auto oldLayout1 = buffer1->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    _shader->BindTextures({ buffer0, buffer1 });
    _shader->SetBlendState(true, HgiBlendFactor::HgiBlendFactorSrcAlpha,
        HgiBlendFactor::HgiBlendFactorOneMinusSrcAlpha, HgiBlendOp::HgiBlendOpAdd,
        HgiBlendFactor::HgiBlendFactorOne, HgiBlendFactor::HgiBlendFactorOneMinusSrcAlpha,
        HgiBlendOp::HgiBlendOpAdd);

    _shader->Draw(aovTexture, {});

    buffer0->SubmitLayoutChange(oldLayout0);
    buffer1->SubmitLayoutChange(oldLayout1);
}

const TfToken& WbOitResolveTask::GetToken()
{
    static const TfToken token { "wboitResolveTask", TfToken::Immortal };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const WbOitResolveTaskParams& /* pv */)
{
    out << "WbOitResolveTask Params: (none)";
    return out;
}

bool operator==(const WbOitResolveTaskParams& /* lhs */, const WbOitResolveTaskParams& /* rhs */)
{
    return true;
}

bool operator!=(const WbOitResolveTaskParams& lhs, const WbOitResolveTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
