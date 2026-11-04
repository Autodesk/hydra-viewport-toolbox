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

#include <hvt/tasks/bloomTask.h>

#include <hvt/tasks/resources.h>

#include <memory>
#include <ostream>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

#include <pxr/pxr.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/trace/trace.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/imaging/hgi/enums.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/shaderFunctionDesc.h>
#include <pxr/imaging/hgi/texture.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{
namespace
{

const TfToken& _GetShaderPath()
{
    static const TfToken shader {
        GetShaderPath("bloom.glslfx").generic_u8string(), TfToken::Immortal };
    return shader;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens, ((shader, "Bloom::Fragment"))(bloomShader));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

// std140-friendly constants for the bloom fragment shader.
struct BloomConstants
{
    GfVec2f screenSize { 0.0f };
    float threshold  { 0.75f };
    float intensity  { 0.5f };
    float blurRadius { 4.0f };
    float _pad { 0.0f };
};

BloomConstants _MakeConstants(BloomTaskParams const& params, GfVec3i const& dimensions)
{
    BloomConstants c;
    c.screenSize[0] = static_cast<float>(dimensions[0]);
    c.screenSize[1] = static_cast<float>(dimensions[1]);
    c.threshold     = params.threshold;
    c.intensity     = params.intensity;
    c.blurRadius    = params.blurRadius;
    return c;
}

} // anonymous namespace

BloomTask::BloomTask(HdSceneDelegate*, SdfPath const& uid) : HdxTask(uid) {}

void BloomTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_shader)
    {
        _shader = std::make_unique<HdxFullscreenShader>(_GetHgi(), "Bloom Shader");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = _tokens->shader.GetString();
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "screenSize", "vec2");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "threshold", "float");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "intensity", "float");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "blurRadius", "float");

        _shader->SetProgram(_GetShaderPath(), _tokens->shader, shaderDesc);
    }

    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        BloomTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            _params = params;
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void BloomTask::Prepare(HdTaskContext*, HdRenderIndex*) {}

void BloomTask::Execute(HdTaskContext* ctx)
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

    const GfVec3i& dimensions = aovTexture->GetDescriptor().dimensions;
    const BloomConstants constants = _MakeConstants(_params, dimensions);
    _shader->SetShaderConstants(sizeof(constants), &constants);

    aovTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    _shader->BindTextures({ aovTexture });
    _shader->Draw(aovTextureIntermediate, HgiTextureHandle());
    aovTexture->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);

    _ToggleRenderTarget(ctx);
}

const TfToken& BloomTask::GetToken()
{
    static const TfToken token { "bloomTask", TfToken::Immortal };
    return token;
}

std::ostream& operator<<(std::ostream& out, BloomTaskParams const& pv)
{
    out << "BloomTask Params: threshold=" << pv.threshold << " intensity=" << pv.intensity
        << " blurRadius=" << pv.blurRadius;
    return out;
}

bool operator==(BloomTaskParams const& lhs, BloomTaskParams const& rhs)
{
    return lhs.threshold == rhs.threshold && lhs.intensity == rhs.intensity &&
        lhs.blurRadius == rhs.blurRadius;
}

bool operator!=(BloomTaskParams const& lhs, BloomTaskParams const& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
