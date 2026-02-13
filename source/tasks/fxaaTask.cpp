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

#include <hvt/tasks/fxaaTask.h>

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
    static TfToken shader { GetShaderPath("fxaa.glslfx").generic_u8string() };
    return shader;
}

}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "FXAA::Fragment"))(antiAliasingShader));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

FXAATask::FXAATask(HdSceneDelegate*, SdfPath const& uid) : HdxTask(uid) {}

void FXAATask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_shader)
    {
        _shader = std::make_unique<class HdxFullscreenShader>(_GetHgi(), "Fxaa Shader");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = _tokens->shader.GetString();
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "uResolution", "vec2");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);

        _shader->SetShaderConstants(sizeof(_params.pixelToUV), &_params.pixelToUV);
    }

    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        FXAATaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            if (_params != params)
            {
                _shader->SetShaderConstants(sizeof(params.pixelToUV), &params.pixelToUV);
                _params = params;
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void FXAATask::Prepare(HdTaskContext*, HdRenderIndex*) {}

void FXAATask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

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

const TfToken& FXAATask::GetToken()
{
    static const TfToken token { "fxaaTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, FXAATaskParams const& pv)
{
    out << "FXAATask Params: " << pv.pixelToUV;
    return out;
}

bool operator==(FXAATaskParams const& lhs, FXAATaskParams const& rhs)
{
    return lhs.pixelToUV == rhs.pixelToUV;
}

bool operator!=(FXAATaskParams const& lhs, FXAATaskParams const& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
