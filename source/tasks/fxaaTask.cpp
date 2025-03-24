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

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#endif

#include <pxr/imaging/hdx/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
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
    static TfToken shader { hvt::GetShaderPath("fxaa.glslfx").generic_u8string() };
    return shader;
}

}

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "FXAA::Fragment"))(antiAliasingShader));

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
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
        HgiShaderFunctionAddConstantParam(&shaderDesc, "uResolution", "float");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);

        _shader->SetShaderConstants(sizeof(_params.resolution), &_params.resolution);
    }

    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        FXAATaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            if (_params != params)
            {
                _shader->SetShaderConstants(sizeof(params.resolution), &params.resolution);
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

std::ostream& operator<<(std::ostream& out, const FXAATaskParams& pv)
{
    out << "FXAATask Params: " << pv.resolution;
    return out;
}

bool operator==(const FXAATaskParams& lhs, const FXAATaskParams& rhs)
{
    return lhs.resolution == rhs.resolution;
}

bool operator!=(const FXAATaskParams& lhs, const FXAATaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace hvt
