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

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((outlineMaskTexture, "outlineMaskTexture"))((shaderNoBlur, "OutlineOverlayTask::NoBlur"))(
        (shaderBlur3x3, "OutlineOverlayTask::Blur3x3"))(
        (shaderBlur5x5, "OutlineOverlayTask::Blur5x5")));

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
            // Could not fetch params; clear the dirty bits anyway so the task does not
            // re-sync every frame.
            *dirtyBits = HdChangeTracker::Clean;
            return;
        }

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

    bool const hasMask = _HasTaskContextData(ctx, _tokens->outlineMaskTexture);
    VtValue textureValue;
    if (hasMask &&
        _GetTaskContextData(ctx, _tokens->outlineMaskTexture, &textureValue) &&
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
    // Blend alpha as src-over, matching the color channels. Using (One, Zero) would
    // overwrite the destination alpha with the mask's alpha (~0 except near outline
    // edges), wiping the AOV's alpha across the full-screen quad. (SrcAlpha,
    // OneMinusSrcAlpha) preserves destination alpha where the mask is transparent and
    // only affects alpha where the outline actually draws.
    _fullscreenShader->SetBlendState(true, HgiBlendFactorSrcAlpha, HgiBlendFactorOneMinusSrcAlpha,
        HgiBlendOpAdd, HgiBlendFactorSrcAlpha, HgiBlendFactorOneMinusSrcAlpha, HgiBlendOpAdd);
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
    TF_WARN("OutlineOverlayTask::_SetProgram shaderPath=%s", shaderPath.GetText());
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
    TF_WARN("OutlineOverlayTask::_SetProgram shader valid, loading program");

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
    TF_WARN("OutlineOverlayTask::_GetDefaultTexture created %dx%d valid=%d",
        width, height, (int)(bool)_defaultTexture);
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

    static TfToken const shader { shaderFilePath.generic_u8string(), TfToken::Immortal };
    return shader;
}

} // namespace HVT_NS::Outline
