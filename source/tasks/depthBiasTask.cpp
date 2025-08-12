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

#include <hvt/tasks/depthBiasTask.h>

#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

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

namespace HVT_NS
{
namespace
{

const TfToken& _GetShaderPath()
{
    static TfToken shader { GetShaderPath("depthBias.glslfx").generic_u8string() };
    return shader;
}

} // anonymous namespace

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "DepthBias::Fragment"))(depthBiasShader));

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

DepthBiasTask::DepthBiasTask(HdSceneDelegate*, SdfPath const& uid) : HdxTask(uid) {}

DepthBiasTask::~DepthBiasTask()
{
    if (_depthIntermediate)
    {
        _GetHgi()->DestroyTexture(&_depthIntermediate);
    }
}

void DepthBiasTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_shader)
    {
        _shader = std::make_unique<class HdxFullscreenShader>(_GetHgi(), "Depth Bias Shader");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = _tokens->shader.GetString();
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", /*bindIndex = */0);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddTexture(&shaderDesc, "depthIn", /*bindIndex = */1);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "gl_FragDepth", "float", "depth(any)");

        // The constants must correspond to the data layout of the uniforms data structure.
        HgiShaderFunctionAddConstantParam(&shaderDesc, "uScreenSize", "vec2");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "uDepthConstFactor", "float");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "uDepthSlope", "float");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);
    }

    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        DepthBiasTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            if (_params != params)
            {
                _params  = params;
            }
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void DepthBiasTask::Prepare(HdTaskContext*, HdRenderIndex*) {}

void DepthBiasTask::_CreateIntermediate(HgiTextureDesc const& desc, HgiTextureHandle& texHandle)
{
    HgiTextureDesc texDesc { desc };
    texDesc.debugName  = "Intermediate depth texture";
    texDesc.usage      = HgiTextureUsageBitsShaderRead | HgiTextureUsageBitsDepthTarget;
    
    texHandle = _GetHgi()->CreateTexture(texDesc);
}

void DepthBiasTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_params.depthBiasEnable)
    {
        if (!_HasTaskContextData(ctx, HdAovTokens->depth))
        {
            TF_CODING_ERROR("Missing the depth texture.");
            return;
        }

        // Gets the texture handles.

        HgiTextureHandle inColor, colorIntermediate;
        _GetTaskContextData(ctx, HdAovTokens->color, &inColor);
        _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &colorIntermediate);

        HgiTextureHandle inDepth;
        _GetTaskContextData(ctx, HdAovTokens->depth, &inDepth);

        if (!_HasTaskContextData(ctx, HdxAovTokens->depthIntermediate))
        {
            if (!_depthIntermediate)
            {
                HgiTextureDesc const& desc = inDepth->GetDescriptor();
                _CreateIntermediate(desc, _depthIntermediate);
            }
            (*ctx)[HdxAovTokens->depthIntermediate] = VtValue(_depthIntermediate);
        }

        HgiTextureHandle depthIntermediate;
        _GetTaskContextData(ctx, HdxAovTokens->depthIntermediate, &depthIntermediate);

        // Updates the in parameters.

        GfVec3i const& dimensions = inDepth->GetDescriptor().dimensions;
    
        _uniforms = { {static_cast<float>(dimensions[0]), static_cast<float>(dimensions[1])},
            _params.depthBiasConstantFactor, _params.depthBiasSlopeFactor };
        _shader->SetShaderConstants(sizeof(_uniforms), &_uniforms);

        // Executes the fragment shader.

        inColor->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
        inDepth->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

        _shader->BindTextures({ inColor, inDepth });
        _shader->Draw(colorIntermediate, depthIntermediate);
     
        inColor->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);
        inDepth->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget);

        _ToggleRenderTarget(ctx);
        _ToggleDepthTarget(ctx);
    }
}

const TfToken& DepthBiasTask::GetToken()
{
    static const TfToken token { "depthBiasTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// VtValue Requirements
// -------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const DepthBiasTaskParams& param)
{
    out << "DepthBiasTask Params: " << param.depthBiasEnable << " " << param.depthBiasConstantFactor
        << " " << param.depthBiasSlopeFactor;
    return out;
}

bool operator==(const DepthBiasTaskParams& lhs, const DepthBiasTaskParams& rhs)
{
    return lhs.depthBiasEnable == rhs.depthBiasEnable &&
        lhs.depthBiasConstantFactor == rhs.depthBiasConstantFactor &&
        lhs.depthBiasSlopeFactor == rhs.depthBiasSlopeFactor;
}

bool operator!=(const DepthBiasTaskParams& lhs, const DepthBiasTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
