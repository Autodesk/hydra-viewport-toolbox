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

#include <hvt/tasks/outline/outlineOverlayTask.h>

#include "outlineTextureNames.h"

#include <hvt/tasks/resources.h>

#include <pxr/imaging/hdSt/renderPassState.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hgi/enums.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hio/glslfx.h>

#include <filesystem>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS::Outline
{

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((shaderNoBlur, "OutlineOverlayTask::NoBlur"))
    ((shaderBlur3x3, "OutlineOverlayTask::Blur3x3"))
    ((shaderBlur5x5, "OutlineOverlayTask::Blur5x5"))
);

OutlineOverlayTask::OutlineOverlayTask(HdSceneDelegate* /* delegate */, SdfPath const& id) :
    HdxTask(id), _renderIndex(nullptr)
{
}

OutlineOverlayTask::~OutlineOverlayTask()
{
    if (_defaultTexture)
    {
        _GetHgi()->DestroyTexture(&_defaultTexture);
    }
}

void OutlineOverlayTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_fullscreenShader)
    {
        _fullscreenShader = std::make_unique<HdxFullscreenShader>(_GetHgi(), "OutlineOverlay");

        _SetProgram();
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        OutlineOverlayTaskParams params;
        if (!_GetTaskParams(delegate, &params))
        {
            // Leave the dirty bits set so a later re-sync retries the fetch. The previously
            // fetched parameters stay in effect meanwhile. Warn once per failure streak: this
            // path re-runs every frame while the fetch keeps failing.
            if (!_paramsFetchWarned)
            {
                TF_WARN("OutlineOverlayTask: could not fetch task parameters; keeping the "
                        "previous values and retrying on the next sync.");
                _paramsFetchWarned = true;
            }
            return;
        }
        _paramsFetchWarned = false;

        // Check if blur mode changed to determine if we need to recompile shader.
        bool blurModeChanged = (_params.blurMode != params.blurMode);

        _params = params;

        if (blurModeChanged)
        {
            _SetProgram();
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void OutlineOverlayTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* renderIndex)
{
    _renderIndex = renderIndex;
}

void OutlineOverlayTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!_params.enabled)
    {
        return;
    }

    if (!_HasTaskContextData(ctx, HdAovTokens->color))
    {
        return;
    }

    HgiTextureHandle aovColorTexture;
    _GetTaskContextData(ctx, HdAovTokens->color, &aovColorTexture);

    HgiTextureHandle textureHandle;

    static const TfToken maskTextureToken(OutlineMaskTextureName());
    VtValue textureValue;
    if (_HasTaskContextData(ctx, maskTextureToken) &&
        _GetTaskContextData(ctx, maskTextureToken, &textureValue) &&
        textureValue.IsHolding<HgiTextureHandle>())
    {
        textureHandle = textureValue.UncheckedGet<HgiTextureHandle>();
    }
    else
    {
        textureHandle = _GetDefaultTexture();
    }

    if (!textureHandle)
    {
        TF_CODING_ERROR("Invalid texture handle");
        return;
    }

    struct ShaderConstants
    {
        float screenScale;
        float blurIntensity;
        float textureSizeX;
        float textureSizeY;
    } constants;

    constants.screenScale   = _params.screenScale;
    constants.blurIntensity = _params.blurIntensity;
    constants.textureSizeX  = static_cast<float>(_params.size[0]);
    constants.textureSizeY  = static_cast<float>(_params.size[1]);

    _fullscreenShader->SetShaderConstants(sizeof(ShaderConstants), &constants);

    _fullscreenShader->BindTextures({ textureHandle });
    // Composite the outline mask over the scene AOV in place, using non-premultiplied
    // src-over. Color uses (SrcAlpha, OneMinusSrcAlpha) because the source color is not
    // premultiplied. The alpha channel uses (One, OneMinusSrcAlpha): the source factor is
    // One (not SrcAlpha) because that is canonical src-over for the alpha channel itself --
    // using SrcAlpha would square the source alpha and leave antialiased outline edges
    // slightly under-opaque. The previous (One, Zero) overwrote the destination alpha with
    // the mask's alpha (~0 except near outline edges), wiping the AOV's alpha across the
    // full-screen quad.
    //
    // The colour factors treat the source as non-premultiplied over an effectively opaque
    // destination; where the destination alpha is < 1 the composited colour is not weighted by
    // it. That holds for the scene colour AOV this task targets.
    _fullscreenShader->SetBlendState(true, HgiBlendFactorSrcAlpha, HgiBlendFactorOneMinusSrcAlpha,
        HgiBlendOpAdd, HgiBlendFactorOne, HgiBlendFactorOneMinusSrcAlpha, HgiBlendOpAdd);
    _fullscreenShader->Draw(aovColorTexture, {});
}

TfToken const& OutlineOverlayTask::GetToken()
{
    static const TfToken token { "outlineOverlayTask", TfToken::Immortal };
    return token;
}

void OutlineOverlayTask::_SetProgram()
{
    TfToken const shaderPath = _GetShaderFilePath();
    if (shaderPath.IsEmpty())
    {
        return;
    }
    HioGlslfx const glslfx(shaderPath, HioGlslfxTokens->defVal);

    std::string reason;
    if (!glslfx.IsValid(&reason))
    {
        TF_CODING_ERROR("Failed to parse shader %s, error: %s\n", shaderPath.GetString().c_str(),
            reason.c_str());
        return;
    }

    TfToken shaderToken;
    switch (_params.blurMode)
    {
    case BlurMode::None:
        shaderToken = _tokens->shaderNoBlur;
        break;
    case BlurMode::Blur3x3:
        shaderToken = _tokens->shaderBlur3x3;
        break;
    case BlurMode::Blur5x5:
        shaderToken = _tokens->shaderBlur5x5;
        break;
    default:
        shaderToken = _tokens->shaderBlur3x3;
        break;
    }

    HgiShaderFunctionDesc fragDesc;
    fragDesc.debugName   = "OutlineOverlayTask Fragment Shader";
    fragDesc.shaderStage = HgiShaderStageFragment;

    HgiShaderFunctionAddStageInput(&fragDesc, "uvOut", "vec2");
    HgiShaderFunctionAddStageOutput(&fragDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddTexture(&fragDesc, "overlayTexture");
    HgiShaderFunctionAddConstantParam(&fragDesc, "screenScale", "float");
    HgiShaderFunctionAddConstantParam(&fragDesc, "blurIntensity", "float");
    HgiShaderFunctionAddConstantParam(&fragDesc, "textureDimensions", "vec2");

    std::string const shaderCode = glslfx.GetSource(shaderToken);
    fragDesc.shaderCode          = shaderCode.c_str();

    _fullscreenShader->SetProgram(fragDesc);
    fragDesc.shaderCode = nullptr;
}

HgiTextureHandle OutlineOverlayTask::_GetDefaultTexture()
{
    if (_defaultTexture)
    {
        return _defaultTexture;
    }

    int width  = _params.size[0];
    int height = _params.size[1];

    HgiTextureDesc texDesc;
    texDesc.format         = HgiFormatFloat32Vec4;
    texDesc.dimensions     = { width, height, 1 };
    texDesc.debugName      = "OutlineOverlayTask Default Texture";
    texDesc.mipLevels      = 1;
    texDesc.usage          = HgiTextureUsageBitsShaderRead;
    texDesc.type           = HgiTextureType2D;
    texDesc.pixelsByteSize = HgiGetDataSize(texDesc.format, texDesc.dimensions);

    std::vector<float> initialData;
    initialData.resize(width * height * 4);
    for (int i = 0; i < height; ++i)
    {
        int oset = 4 * i * width;
        for (int j = 0; j < width; ++j)
        {
            initialData[oset + 4 * j]     = 0.0f; // R
            initialData[oset + 4 * j + 1] = 0.5f; // G
            initialData[oset + 4 * j + 2] = 0.0f; // B
            initialData[oset + 4 * j + 3] = 0.5f; // A (50% alpha)
        }
    }
    texDesc.initialData = initialData.data();
    _defaultTexture     = _GetHgi()->CreateTexture(texDesc);
    return _defaultTexture;
}

TfToken OutlineOverlayTask::_GetShaderFilePath()
{
    auto shaderFilePath = GetShaderPath("outlineOverlay.glslfx");
    if (!std::filesystem::is_regular_file(shaderFilePath))
    {
        TF_RUNTIME_ERROR("Shader file not found: %s", shaderFilePath.string().c_str());
        return TfToken {};
    }

    // generic_u8string() is UTF-8 on every platform (lossless for non-ASCII install paths),
    // unlike generic_string() which is the native narrow encoding (lossy ANSI on Windows).
    // The begin/end copy yields a std::string under both C++17 (char) and C++20 (char8_t).
    auto const u8str = shaderFilePath.generic_u8string();
    std::string const shaderStr(u8str.begin(), u8str.end());
    static TfToken const shader { shaderStr, TfToken::Immortal };
    return shader;
}

} // namespace HVT_NS::Outline
