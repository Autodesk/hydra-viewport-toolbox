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

#include <hvt/tasks/wboitRenderTask.h>

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

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hdSt/renderBuffer.h>
#include <pxr/imaging/hdSt/renderPassShader.h>
#include <pxr/imaging/hdSt/renderPassState.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hio/glslfx.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

TF_DEFINE_PRIVATE_TOKENS(_wboitTokens,
    (hdxWboitBufferOne)
    (hdxWboitBufferTwo)
);

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
{

namespace
{

const TfToken& _GetShaderPath()
{
    static TfToken shader { GetShaderPath("wboit.glslfx").generic_u8string() };
    return shader;
}

const HioGlslfxSharedPtr& _GetRenderPassWbOitGlslfx()
{
    static const HioGlslfxSharedPtr glslfx =
        std::make_shared<HioGlslfx>(_GetShaderPath().GetString());
    return glslfx;
}

} // namespace

WbOitRenderTask::WbOitRenderTask(HdSceneDelegate* delegate, SdfPath const& id)
    : HdxRenderTask(delegate, id)
{
}

WbOitRenderTask::~WbOitRenderTask()
{
    if (_renderIndex)
    {
        HdRenderParam* renderParam = _renderIndex->GetRenderDelegate()->GetRenderParam();
        for (auto const& aovBuffer : _wboitBuffers)
        {
            aovBuffer->Finalize(renderParam);
        }
    }
    _wboitBuffers.clear();
    _wboitAovBindings.clear();
}

void WbOitRenderTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_renderPassShader)
    {
        _renderPassShader =
            std::make_shared<HdStRenderPassShader>(_GetRenderPassWbOitGlslfx());
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        HdxRenderTask::_Sync(delegate, ctx, dirtyBits);

        HdRenderPassStateSharedPtr renderPassState = _GetRenderPassState(ctx);
        if (!TF_VERIFY(renderPassState, "WBOIT: The render pass state is not valid"))
        {
            return;
        }

        HdStRenderPassState* extendedState =
            dynamic_cast<HdStRenderPassState*>(renderPassState.get());
        if (!TF_VERIFY(extendedState, "WBOIT: Only works with HdSt"))
        {
            return;
        }

        renderPassState->SetMultiSampleEnabled(false);

        extendedState->SetBlendEnabled(true);
        renderPassState->SetBlend(
            HdBlendOp::HdBlendOpAdd,
            HdBlendFactor::HdBlendFactorOne,
            HdBlendFactor::HdBlendFactorOne,
            HdBlendOp::HdBlendOpAdd,
            HdBlendFactor::HdBlendFactorZero,
            HdBlendFactor::HdBlendFactorOneMinusSrcAlpha);
        extendedState->SetAlphaToCoverageEnabled(false);
        extendedState->SetAlphaThreshold(0.f);
        renderPassState->SetEnableDepthTest(true);
        renderPassState->SetEnableDepthMask(false);
        renderPassState->SetColorMaskUseDefault(false);
        renderPassState->SetColorMasks({ HdRenderPassState::ColorMaskRGBA });
    }
}

void WbOitRenderTask::Prepare(HdTaskContext* ctx, HdRenderIndex* renderIndex)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _renderIndex = renderIndex;

    if (!HdxRenderTask::_HasDrawItems())
    {
        return;
    }

    HdxRenderTask::Prepare(ctx, renderIndex);

    (*ctx)[HdxTokens->oitRequestFlag] = VtValue(true);

    HdRenderPassStateSharedPtr renderPassState = _GetRenderPassState(ctx);
    if (!TF_VERIFY(renderPassState, "WBOIT: The render pass state is not valid"))
    {
        return;
    }

    HdStRenderPassState* extendedState =
        dynamic_cast<HdStRenderPassState*>(renderPassState.get());

    extendedState->SetRenderPassShader(_renderPassShader);

    _InitTextures(ctx, renderPassState);
    renderPassState->SetAovBindings(_wboitAovBindings);

    auto width  = _wboitAovBindings.front().renderBuffer->GetWidth();
    auto height = _wboitAovBindings.front().renderBuffer->GetHeight();
    renderPassState->SetViewport(GfVec4d(0, 0, width, height));
}

void WbOitRenderTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!HdxRenderTask::_HasDrawItems())
    {
        return;
    }

    HdxRenderTask::Execute(ctx);
}

bool WbOitRenderTask::_InitTextures(
    HdTaskContext* ctx, HdRenderPassStateSharedPtr const& renderPassState)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    auto aovBindings = renderPassState->GetAovBindings();
    if (aovBindings.empty())
    {
        TF_WARN("No AOV bindings found for WBOIT render task");
        return false;
    }

    const bool createOitBuffers = _wboitBuffers.empty();

    if (!createOitBuffers && (aovBindings.front() == _wboitAovBindings.front()))
    {
        return false;
    }

    auto colorRenderBuffer =
        static_cast<HdStRenderBuffer*>(aovBindings.front().renderBuffer);
    GfVec2i dimensions =
        GfVec2i(colorRenderBuffer->GetWidth(), colorRenderBuffer->GetHeight());
    bool isMultiSampled = false;

    const static TfTokenVector aovOutputs = {
        _wboitTokens->hdxWboitBufferOne,
        _wboitTokens->hdxWboitBufferTwo,
    };

    if (createOitBuffers)
    {
        HdStResourceRegistrySharedPtr const& hdStResourceRegistry =
            std::static_pointer_cast<HdStResourceRegistry>(
                _renderIndex->GetResourceRegistry());

        for (size_t i = 0; i < aovOutputs.size(); ++i)
        {
            TfToken const& aovOutput = aovOutputs[i];
            SdfPath const aovId      = SdfPath("wboitBuffer" + std::to_string(i));

            _wboitBuffers.push_back(
                std::make_unique<HdStRenderBuffer>(hdStResourceRegistry.get(), aovId));

            HdFormat format = (aovOutput == _wboitTokens->hdxWboitBufferOne)
                ? HdFormatFloat16Vec4
                : HdFormatFloat16;
            HdAovDescriptor aovDesc =
                HdAovDescriptor(format, isMultiSampled, VtValue(GfVec4f(0, 0, 0, 1)));

            HdRenderPassAovBinding binding;
            binding.aovName        = aovOutput;
            binding.renderBufferId = aovId;
            binding.aovSettings    = aovDesc.aovSettings;
            binding.renderBuffer   = _wboitBuffers.back().get();
            binding.clearValue     = VtValue(GfVec4f(0, 0, 0, 1));

            _wboitAovBindings.push_back(binding);
        }

        auto depthBufferBinding = std::find_if(
            aovBindings.begin(), aovBindings.end(),
            [](const HdRenderPassAovBinding& aovBinding)
            {
                return HdAovHasDepthSemantic(aovBinding.aovName) ||
                    HdAovHasDepthStencilSemantic(aovBinding.aovName);
            });
        if (depthBufferBinding != aovBindings.end())
        {
            _wboitAovBindings.push_back(*depthBufferBinding);
        }
        else
        {
            TF_WARN("No depth buffer found for WBOIT render task");
        }
    }

    VtValue existingResource =
        _wboitAovBindings.front().renderBuffer->GetResource(false);
    if (existingResource.IsHolding<HgiTextureHandle>())
    {
        int32_t width  = _wboitAovBindings.front().renderBuffer->GetWidth();
        int32_t height = _wboitAovBindings.front().renderBuffer->GetHeight();
        if (width == dimensions[0] && height == dimensions[1])
        {
            return false;
        }
    }

    for (size_t i = 0; i < aovOutputs.size(); ++i)
    {
        HdRenderPassAovBinding const& aovBinding = _wboitAovBindings[i];
        HdFormat format = (aovBinding.aovName == _wboitTokens->hdxWboitBufferOne)
            ? HdFormatFloat16Vec4
            : HdFormatFloat16;
        aovBinding.renderBuffer->Allocate(
            GfVec3i(dimensions[0], dimensions[1], 1), format, isMultiSampled);
        (*ctx)[aovOutputs[i]] = aovBinding.renderBuffer->GetResource(false);
    }

    return true;
}

const TfToken& WbOitRenderTask::GetToken()
{
    static const TfToken token { "wboitRenderTask" };
    return token;
}

} // namespace HVT_NS
