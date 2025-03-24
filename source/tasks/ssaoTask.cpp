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

#include <hvt/tasks/ssaoTask.h>

#include <hvt/tasks/resources.h>

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#endif

#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/capabilities.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

namespace
{

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif _MSC_VER
#pragma warning(push)
#endif

// Define some tokens needed to support the task, including the shader names.
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((rawShader, "SSAO::Raw"))((blurShader, "SSAO::Blur"))((compositeShader, "SSAO::Composite")));

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

// Computes a projection matrix from a camera and view properties.
// NOTE: The logic here is adapted from HdRenderPassState::GetProjectionMatrix(). If this function
// is needed elsewhere in this project, it should be moved into a header file, and not copied.
GfMatrix4d ComputeProjectionMatrix(HdCamera const& camera, ViewProperties const& view)
{
    if (view.framing.IsValid())
    {
        const CameraUtilConformWindowPolicy policy = view.overrideWindowPolicy.has_value()
            ? view.overrideWindowPolicy.value()
            : camera.GetWindowPolicy();
        return view.framing.ApplyToProjectionMatrix(camera.ComputeProjectionMatrix(), policy);
    }
    else
    {
        const double aspect = view.viewport[3] != 0.0 ? view.viewport[2] / view.viewport[3] : 1.0;
        return CameraUtilConformedWindow(
            camera.ComputeProjectionMatrix(), camera.GetWindowPolicy(), aspect);
    }
}

// Gets the number of spiral turns that provide minimal discrepancy (and thus minimal variance) when
// applied during sampling of the specified number of samples.
int GetSpiralTurnCount(int sampleCount)
{
    // NOTE: This table comes from the reference implementation of Scalable Ambient Obscurance in
    // the G3D Innovation Engine: https://casual-effects.com/g3d.
    // clang-format off
    constexpr int kSampleCountMax = 64;
    constexpr int kSpiralTurnCounts[kSampleCountMax + 1] =
    {
        1,  1,  1,  2,  3,  2,  5,  2,  3,  2,  // 0
        3,  3,  5,  5,  3,  4,  7,  5,  5,  7,  // 1
        9,  8,  5,  5,  7,  7,  7,  8,  5,  8,  // 2
       11, 12,  7, 10, 13,  8, 11,  8,  7, 14,  // 3
       11, 11, 13, 12, 13, 19, 17, 13, 11, 18,  // 4
       19, 11, 11, 14, 17, 21, 15, 16, 17, 18,  // 5
       13, 17, 11, 17, 19                       // 6
    };
    // clang-format on

    return sampleCount > kSampleCountMax ? 257 : kSpiralTurnCounts[sampleCount];
}

} // namespace anonymous

SSAOTask::SSAOTask(HdSceneDelegate* /* pDelegate */, SdfPath const& id) : HdxTask(id)
{
    // NOTE: The Hgi interface is not available in the constructor, so complete initialization must
    // wait for the Sync / Prepare / Execute phases.

    // Get and retain the path to the shader code file.
    // NOTE: This file contains code for multiple shaders, used by different passes.
    _shaderPath = TfToken(hvt::GetShaderPath("ssao.glslfx").generic_u8string());
}

SSAOTask::~SSAOTask()
{
    // Destroy the temporary AO textures.
    if (_aoTexture1)
    {
        _GetHgi()->DestroyTexture(&_aoTexture1);
        _GetHgi()->DestroyTexture(&_aoTexture2);
    }
}

void SSAOTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // NOTE: The "Sync" phase is the time to resolve any dirty flags, usually by just retrieving
    // changed task properties. Other work is done in the subsequent "Prepare" and "Execute"
    // phases.

    // Get the updated task properties if they are dirty.
    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        SSAOTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            _params = params;
        }
    }

    // Clear the dirty flags.
    *dirtyBits = HdChangeTracker::Clean;
}

void SSAOTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* renderIndex)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // NOTE: The "Prepare" phase is the time to resolve prims from IDs, using the render index.
    // All other work to render is performed in the "Execute" phase.

    // Resolve the camera prim from the camera ID.
    _pCamera = static_cast<const HdCamera*>(
        renderIndex->GetSprim(HdPrimTypeTokens->camera, _params.view.cameraID));
}

void SSAOTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // NOTE: Most of the work for a single execution of the task (e.g. rendering for a single
    // frame) is performed in the "Execute" phase. This includes lazy creation of resources, and
    // then updating resources and drawing as needed.

    // Do nothing if the task is disabled.
    if (!_params.ao.isEnabled)
    {
        return;
    }

    // Get handles to several AOVs, along with their dimensions.
    HgiTextureHandle colorTexture, colorIntermediateTexture, depthTexture;
    _GetTaskContextData(ctx, HdAovTokens->color, &colorTexture);
    _GetTaskContextData(ctx, HdxAovTokens->colorIntermediate, &colorIntermediateTexture);
    _GetTaskContextData(ctx, HdAovTokens->depth, &depthTexture);
    const HgiTextureDesc& textureDesc = colorTexture->GetDescriptor();
    GfVec2i dimensions                = GfVec2i(textureDesc.dimensions.data());

    // Initialize the textures and shaders.
    // NOTE: These functions do nothing if the resources already exist and are valid.
    InitTextures(dimensions);
    InitRawShader();
    InitBlurShader();
    InitCompositeShader();

    // Execute the passes needed for SSAO. Two blur passes are performed: horizontal and vertical,
    // with offsets indicating the blur directions. The two temporary AO textures are used to share
    // results between passes in a "ping-ping" manner, so that a shader isn't reading from and
    // writing to the same texture at the same time.
    //
    // 1) Raw: read from depth AOV, write to AO #1.
    // 2) Blur H: read from AO #1, write to AO #2.
    // 3) Blur V: read from AO #2, write to AO #1.
    // 4) Composite: read from color AOV and AO #1, write to color intermediate AOV.
    ExecuteRawPass(depthTexture, _aoTexture1);
    if (_params.ao.isDenoiseEnabled)
    {
        ExecuteBlurPass(_aoTexture1, _aoTexture2, GfVec2i(0, 1));
        ExecuteBlurPass(_aoTexture2, _aoTexture1, GfVec2i(1, 0));
    }
    ExecuteCompositePass(colorTexture, _aoTexture1, colorIntermediateTexture);

    // Toggle (swap) between the color and color intermediate AOVs, so that the final composited
    // result in the color intermediate texture is used as the color AOV.
    _ToggleRenderTarget(ctx);
}

void SSAOTask::InitTextures(GfVec2i const& dimensions)
{
    Hgi* pHgi = _GetHgi();

    // Do nothing in the texture dimensions are unchanged and the textures already exist. If the
    // dimensions have changed, destroy the existing textures as they are no longer valid.
    if (_aoTexture1)
    {
        if (dimensions == _dimensions)
        {
            return;
        }
        else
        {
            pHgi->DestroyTexture(&_aoTexture1);
            pHgi->DestroyTexture(&_aoTexture2);
        }
    }

    // Prepare a texture description for the temporary textures. The temporary textures only need
    // to be 8-bit RGBA here, even if the color AOV is floating-point.
    _dimensions = dimensions;
    HgiTextureDesc textureDesc;
    constexpr HgiFormat kFormat = HgiFormat::HgiFormatUNorm8Vec4;
    constexpr int kPixelSize    = 4;
    int width                   = _dimensions[0];
    int height                  = _dimensions[1];
    textureDesc.dimensions      = GfVec3i(width, height, 1);
    textureDesc.format          = kFormat;
    textureDesc.pixelsByteSize  = width * height * kPixelSize;
    textureDesc.usage           = HgiTextureUsageBitsShaderRead | HgiTextureUsageBitsColorTarget;

    // Create the temporary textures; see the other code for how these are used in task execution.
    textureDesc.debugName = "SSAO Texture 1";
    _aoTexture1           = pHgi->CreateTexture(textureDesc);
    textureDesc.debugName = "SSAO Texture 2";
    _aoTexture2           = pHgi->CreateTexture(textureDesc);
}

void SSAOTask::InitRawShader()
{
    // Do nothing if the shader already exists.
    if (_pRawShader)
    {
        return;
    }

    // Create an Hdx "fullscreen shader" object. This is a utility class to execute fragment
    // shaders for every pixel of an input texture, sometimes called "screen quad" rendering.
    // The fullscreen shader has an internal vertex shader.
    _pRawShader = std::make_unique<class HdxFullscreenShader>(_GetHgi(), "SSAORawShader");

    // Populate a shader function description for the fragment shader, including the input and
    // output parameters.
    // NOTE: The constants must correspond to the data layout of the uniforms data structure.
    HgiShaderFunctionDesc shaderDesc;
    shaderDesc.debugName   = _tokens->rawShader.GetString();
    shaderDesc.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
    HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddTexture(&shaderDesc, "depthIn");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uClipInfo", "vec4");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uProjInfo", "vec4");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uScreenSize", "ivec2");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uAmount", "float");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uSampleRadius", "float");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uIsScreenSampleRadius", "int");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uSampleCount", "int");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uSpiralTurnCount", "int");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uIsBlurEnabled", "int");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uIsOrthographic", "int");

    // Set the shader file path and the shader description on the fullscreen shader.
    _pRawShader->SetProgram(_shaderPath, _tokens->rawShader, shaderDesc);
}

void SSAOTask::InitBlurShader()
{
    // Do nothing if the shader already exists.
    if (_pBlurShader)
    {
        return;
    }

    // Create an Hdx "fullscreen shader" object.
    _pBlurShader = std::make_unique<class HdxFullscreenShader>(_GetHgi(), "SSAOBlurShader");

    // Populate a shader function description for the fragment shader, including the input and
    // output parameters.
    // NOTE: An integer constant is used in place of a boolean as Hydra code generation does not
    // support the boolean type.
    HgiShaderFunctionDesc shaderDesc;
    shaderDesc.debugName   = _tokens->blurShader.GetString();
    shaderDesc.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
    HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddTexture(&shaderDesc, "aoIn");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uScreenSize", "ivec2");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uOffset", "ivec2");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uEdgeSharpness", "float");

    // Set the shader file path and the shader description on the fullscreen shader.
    _pBlurShader->SetProgram(_shaderPath, _tokens->blurShader, shaderDesc);
}

void SSAOTask::InitCompositeShader()
{
    // Do nothing if the shader already exists.
    if (_pCompositeShader)
    {
        return;
    }

    // Create an Hdx "fullscreen shader" object.
    _pCompositeShader =
        std::make_unique<class HdxFullscreenShader>(_GetHgi(), "SSAOCompositeShader");

    // Populate a shader function description for the fragment shader, including the input and
    // output parameters.
    // NOTE: An integer constant is used in place of a boolean as Hydra code generation does not
    // support the boolean type.
    HgiShaderFunctionDesc shaderDesc;
    shaderDesc.debugName   = _tokens->compositeShader.GetString();
    shaderDesc.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
    HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
    HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
    HgiShaderFunctionAddTexture(&shaderDesc, "aoIn", 1);
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uScreenSize", "ivec2");
    HgiShaderFunctionAddConstantParam(&shaderDesc, "uIsShowOnlyEnabled", "int");

    // Set the shader file path and the shader description on the fullscreen shader.
    _pCompositeShader->SetProgram(_shaderPath, _tokens->compositeShader, shaderDesc);
}

void SSAOTask::ExecuteRawPass(
    HgiTextureHandle const& inDepthTexture, HgiTextureHandle const& outTexture)
{
    // Issuing explicit layout change barrier(s) on  render target(s). This is
    // handled by OpenGL driver for HgiGL backend (for all cases) and by Metal
    // driver for hgiMetal backend (for this case) hence has no impact on HgiGL
    // and HgiMetal but is explicitly need for HgiVulkan backend.
    inDepthTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    // Assign the input depth texture to the shader.
    // NOTE: The shader internally optimizes for redundant setting of texture bindings.
    _pRawShader->BindTextures({ inDepthTexture });

    // Prepare new values for uniform data.
    // NOTE: It is not sufficient to do this in the Sync phase because the uniforms depend on
    // the input texture ("screen") dimensions, and the Sync phase does not have updated
    // dimensions immediately after they are changed, e.g. resizing a window.
    RawUniforms uniforms;

    // Set the values for the "projection info" structure, based on the projection matrix and
    // screen dimensions. See the paper for details.
    GfMatrix4d P      = ComputeProjectionMatrix(*_pCamera, _params.view);
    float A           = -2.0f / (_dimensions[0] * static_cast<float>(P[0][0]));
    float B           = -2.0f / (_dimensions[1] * static_cast<float>(P[1][1]));
    float C           = (1.0f - static_cast<float>(P[2][0])) / static_cast<float>(P[0][0]);
    float D           = (1.0f + static_cast<float>(P[2][1])) / static_cast<float>(P[1][1]);
    uniforms.projInfo = { A, B, C, D };

    // Compute the "projection scale": the number of pixels per scene unit, at a distance of *one*
    // scene unit. The projection scale computation is based on element P[1][1], which is
    // - Perspective:  2.0 * Near / view_space_vertical_extents
    // - Orthographic: 2.0 / view_space_vertical_extents
    // ... so the value is height_in_pixels * P[1][1] / 2.0.
    float pixelsPerUnit = _dimensions[1] * P[1][1] / 2.0f;

    // Set the values for the "clip info" structure, based on the clipping range (near and far).
    // See the paper for details. This add the projection scale computed above, as the fourth entry.
    // NOTE: The clipping range has absolute distances, but the structure needs Z values, and since
    // we use a view looking down the -Z axis, the values must be negated.
    const GfRange1d& range = _pCamera->GetClippingRange();
    float zNear            = static_cast<float>(-range.GetMin());
    float zFar             = static_cast<float>(-range.GetMax());
    uniforms.clipInfo      = { zNear * zFar, zNear - zFar, zFar, pixelsPerUnit };

    // Set the basic values.
    uniforms.screenSize[0]        = _dimensions[0];
    uniforms.screenSize[1]        = _dimensions[1];
    uniforms.amount               = _params.ao.amount;
    uniforms.sampleRadius         = _params.ao.sampleRadius;
    uniforms.isScreenSampleRadius = _params.ao.isScreenSampleRadius ? 1 : 0;
    uniforms.sampleCount          = _params.ao.sampleCount;
    uniforms.spiralTurnCount      = GetSpiralTurnCount(_params.ao.sampleCount);
    uniforms.isBlurEnabled        = _params.ao.isDenoiseEnabled ? 1 : 0;
    uniforms.isOrthographic       = _pCamera->GetProjection() == HdCamera::Orthographic ? 1 : 0;

    // Compare the new and existing uniform values. If they have changed, set the updated values
    // on the shader. This comparison can avoid a redundant internal memory copy in the shader.
    if (uniforms != _rawUniforms)
    {
        _rawUniforms = uniforms;
        _pRawShader->SetShaderConstants(sizeof(RawUniforms), &_rawUniforms);
    }

    // Render the shader using the specified output texture, and with no depth texture assigned.
    _pRawShader->Draw(outTexture, HgiTextureHandle());

    // Issuing explicit layout change barrier(s) on  render target(s). This is
    // handled by OpenGL driver for HgiGL backend (for all cases) and by Metal
    // driver for hgiMetal backend (for this case) hence has no impact on HgiGL
    // and HgiMetal but is explicitly need for HgiVulkan backend.
    inDepthTexture->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget);
}

void SSAOTask::ExecuteBlurPass(
    HgiTextureHandle const& inAOTexture, HgiTextureHandle const& outTexture, GfVec2i const& offset)
{
    // Issuing explicit layout change barrier(s) on  render target(s). This is
    // handled by OpenGL driver for HgiGL backend (for all cases) and by Metal
    // driver for hgiMetal backend (for this case) hence has no impact on HgiGL
    // and HgiMetal but is explicitly need for HgiVulkan backend.
    inAOTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    // Assign the AO texture to the shader.
    // NOTE: The shader internally optimizes for redundant setting of texture bindings.
    _pBlurShader->BindTextures({ inAOTexture });

    // Prepare new values for uniform data.
    BlurUniforms uniforms;
    uniforms.screenSize[0] = _dimensions[0];
    uniforms.screenSize[1] = _dimensions[1];
    uniforms.offset        = offset;
    uniforms.edgeSharpness = _params.ao.denoiseEdgeSharpness;

    // Compare the new and existing uniform values. If they have changed, set the updated values
    // on the shader. This comparison can avoid a redundant internal memory copy in the shader.
    if (uniforms != _blurUniforms)
    {
        _blurUniforms = uniforms;
        _pBlurShader->SetShaderConstants(sizeof(BlurUniforms), &_blurUniforms);
    }

    // Render the shader using the specified output texture, and with no depth texture assigned.
    _pBlurShader->Draw(outTexture, HgiTextureHandle());

    // Issuing explicit layout change barrier(s) on  render target(s). This is
    // handled by OpenGL driver for HgiGL backend (for all cases) and by Metal
    // driver for hgiMetal backend (for this case) hence has no impact on HgiGL
    // and HgiMetal but is explicitly need for HgiVulkan backend.
    inAOTexture->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);
}

void SSAOTask::ExecuteCompositePass(HgiTextureHandle const& inColorTexture,
    HgiTextureHandle const& inAOTexture, HgiTextureHandle const& outTexture)
{
    // Issuing explicit layout change barrier(s) on  render target(s). This is
    // handled by OpenGL driver for HgiGL backend (for all cases) and by Metal
    // driver for hgiMetal backend (for this case) hence has no impact on HgiGL
    // and HgiMetal but is explicitly need for HgiVulkan backend.
    inAOTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
    inColorTexture->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    // Assign the input color and AO textures to the shader.
    // NOTE: The shader internally optimizes for redundant setting of texture bindings.
    _pCompositeShader->BindTextures({ inColorTexture, inAOTexture });

    // Prepare new values for uniform data.
    CompositeUniforms uniforms;
    uniforms.screenSize[0]     = _dimensions[0];
    uniforms.screenSize[1]     = _dimensions[1];
    uniforms.isShowOnlyEnabled = _params.ao.isShowOnlyEnabled ? 1 : 0;

    // Compare the new and existing uniform values. If they have changed, set the updated values
    // on the shader. This comparison can avoid a redundant internal memory copy in the shader.
    if (uniforms != _compositeUniforms)
    {
        _compositeUniforms = uniforms;
        _pCompositeShader->SetShaderConstants(sizeof(CompositeUniforms), &_compositeUniforms);
    }

    // Render the shader using the specified output texture, and with no depth texture assigned.
    _pCompositeShader->Draw(outTexture, HgiTextureHandle());

    // Issuing explicit layout change barrier(s) on  render target(s). This is
    // handled by OpenGL driver for HgiGL backend (for all cases) and by Metal
    // driver for hgiMetal backend (for this case) hence has no impact on HgiGL
    // and HgiMetal but is explicitly need for HgiVulkan backend.
    inAOTexture->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);
    inColorTexture->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);
}

} // namespace hvt
