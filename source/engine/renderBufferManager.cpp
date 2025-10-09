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

#include <hvt/engine/renderBufferManager.h>
#include <hvt/engine/taskUtils.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/resources.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4201)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4305)
#endif
// clang-format on

#include <pxr/pxr.h>

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (renderBufferDescriptor)
);

TF_DEFINE_PRIVATE_TOKENS(_shaderTokens, ((shader, "Compose::Fragment"))((shaderWithDepth, "Compose::FragmentWithDepth"))(composeShader));

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

namespace HVT_NS
{

// Prepare uniform buffer for GPU computation.
struct Uniforms
{
    GfVec2f screenSize;
};

/// The Impl is derived from HdxTaskController. The Impl consolidates
/// the render-buffer related operations which were originally in the Task Controller.
///
/// The implementation is private and hidden for now. This will evolve. Modify at your own risks.
class RenderBufferManager::Impl : public RenderBufferSettingsProvider
{
public:
    explicit Impl(HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate);
    ~Impl();

    Impl(Impl const&)            = delete;
    Impl& operator=(Impl const&) = delete;

    /// Sets the size of the render buffer and MSAA settings, update render buffer descriptors.
    void SetBufferSizeAndMsaa(
        const GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled);

    HdRenderBuffer* GetRenderOutput(TfToken const& name, SdfPath const& controllerId);

    /// Updates render output parameters and creates new render buffers if needed.
    /// Note: AOV binding values are stored here and consulted later by RenderTasks.
    bool SetRenderOutputs(TfTokenVector const& names, RenderBufferBindings const& inputs,
        GfVec4d const& viewport, SdfPath const& controllerId);

    /// Updates the render output clear color.
    /// Note: Clear color values are stored here and consulted later by RenderTasks.
    void SetRenderOutputClearColor(
        TfToken const& name, SdfPath const& controllerId, VtValue const& clearValue);

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(TfToken const& api, VtValue const& framebufferHandle)
    {
        _presentParams.windowHandle              = VtValue();
        _presentParams.api                       = api;
        _presentParams.framebufferHandle         = framebufferHandle;
    }

    /// Set interop destination handle to present to and composition parameters.
    void SetInteropPresentation(VtValue const& destinationInteropHandle, VtValue const& composition)
    {
        // NOTE: The underlying type of destinationInteropHandle VtValue is HgiPresentInteropHandle,
        // which is a std::variant. See declaration of HgiPresentInteropHandle for more details.
        _presentParams.windowHandle              = VtValue();
        _presentParams.framebufferHandle         = destinationInteropHandle;
        _presentParams.compositionParams         = composition;
    }

    /// Set vsync and window destination handle to present to.
    void SetWindowPresentation(VtValue const& windowHandle, bool vsync)
    {
        // NOTE: The underlying type of windowHandle VtValue is HgiPresentWindowHandle,
        // which is a std::variant. See declaration of HgiPresentWindowHandle.
        _presentParams.windowHandle              = windowHandle;
        _presentParams.windowVsync               = vsync;
        _presentParams.framebufferHandle         = VtValue();
    }

    /// Returns true if AOVs (RenderBuffer Bprims) are supported by the render delegate.
    bool IsAovSupported() const override;

    /// Returns true if progressive rendering is enabled.
    bool IsProgressiveRenderingEnabled() const override { return _isProgressiveRenderingEnabled; }

    /// Returns the name of the AOV to be used for the viewport.
    TfToken const& GetViewportAov() const override { return _viewportAov; }

    /// Get the size of the render buffers.
    GfVec2i const& GetRenderBufferSize() const override;

    /// Builds the AOV oath from the controller ID and AOV name.
    static SdfPath GetAovPath(const SdfPath& controllerID, const TfToken& aov);

    /// Returns the AOV parameter cache, containing data required to update RenderTask AOV binding
    /// parameters.
    AovParams const& GetAovParamCache() const override { return _aovTaskCache; }

    // Returns the presentation parameters, containing data relevant to the HdxPresentTask.
    PresentationParams const& GetPresentationParams() const override { return _presentParams; }

private:

    /// Copy the contents of the input buffer into the output buffer
    void PrepareBuffersFromInputs(RenderBufferBinding const& colorInput,
        RenderBufferBinding const& depthInput, HdRenderBufferDescriptor const& desc,
        SdfPath const& controllerId);

    /// Sets the viewport render output (color or buffer visualization).
    void SetViewportRenderOutput(const TfToken& name, const SdfPath& controllerId);

    /// The render texture dimensions.
    GfVec2i _renderBufferSize;

    /// Multisampling enabled or not.
    bool _enableMultisampling { true };

    /// Number of samples for multisampling.
    size_t _msaaSampleCount { 4 };

    bool _isProgressiveRenderingEnabled;

    /// List of Bprim IDs. These IDs are used to:
    ///  - Insert and remove Bprims from the RenderIndex.
    ///  - Get Bprims from the RenderIndex.
    ///  - Get and Set parameters in the SyncDelegate.
    ///     e.g.
    ///       aovDelegate.param[bufferID, tokens::stormMsaaSampleCount]
    ///       HdRenderBufferDescriptor aovDelegate.param[bufferID, tokens::renderBufferDescriptor]
    SdfPathVector _aovBufferIds;

    /// AOV output cache, for checking if outputs have changed since the last call and only update
    /// render or aovBindings when necessary.
    TfTokenVector _aovOutputs;

    /// AOV input cache, for checking if inputs have changed since the last call.
    RenderBufferBindings _aovInputs;

    /// Viewport AOV cache to prevent unnecessary execution or dirty states in
    /// SetViewportRenderOutput.
    TfToken _viewportAov;

    /// Intermediate storage for RenderTask AOV parameters. These values are meant to be set
    /// into RenderTaskParams, but the RenderBufferManager is not responsible for doing it, it only
    /// stores the values.
    AovParams _aovTaskCache;

    /// The presentation parameters. This class holds data relevant to the HdxPresentTask.
    PresentationParams _presentParams;

    /// The RenderIndex, used to create Bprims (buffers).
    HdRenderIndex* _pRenderIndex;

    /// The SyncDelegate used to create RenderBufferDescriptor data for use by the render index.
    SyncDelegatePtr _syncDelegate;

    ///  The shaders used to copy the contents of the input into the output render buffer.
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyShaderNoDepth;
};


RenderBufferManager::Impl::Impl(HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate) :
    _renderBufferSize(0, 0), _pRenderIndex(pRenderIndex), _syncDelegate(syncDelegate)
{
    _presentParams.api             = HgiTokens->OpenGL;
    _isProgressiveRenderingEnabled = { TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false) };
}

RenderBufferManager::Impl::~Impl()
{
    for (auto const& id : _aovBufferIds)
    {
        _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
    }
}

bool RenderBufferManager::Impl::IsAovSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

GfVec2i const& RenderBufferManager::Impl::GetRenderBufferSize() const
{
    return _renderBufferSize;
}

SdfPath RenderBufferManager::Impl::GetAovPath(const SdfPath& controllerID, const TfToken& aov)
{
    std::string identifier = std::string("aov_") + TfMakeValidIdentifier(aov.GetString());
    return controllerID.AppendChild(TfToken(identifier));
}

Hgi* GetHgi(HdRenderIndex const* renderIndex)
{
    HdDriverVector const& drivers = renderIndex->GetDrivers();
    for (HdDriver* hdDriver : drivers)
    {
        if ((hdDriver->name == HgiTokens->renderDriver) && hdDriver->driver.IsHolding<Hgi*>())
        {
            return hdDriver->driver.UncheckedGet<Hgi*>();
        }
    }

    return nullptr;
}

const TfToken& _GetShaderPath()
{
    static TfToken shader { GetShaderPath("compose.glslfx").generic_u8string() };
    return shader;
}

void RenderBufferManager::Impl::PrepareBuffersFromInputs(RenderBufferBinding const& colorInputAov,
    RenderBufferBinding const& depthInputAov, HdRenderBufferDescriptor const& desc, SdfPath const& controllerId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HgiTextureHandle colorInput = colorInputAov.texture;
    HgiTextureHandle depthInput = depthInputAov.texture;

    if (!colorInput)
    {
        return;
    }

    const SdfPath aovPath = GetAovPath(controllerId, colorInputAov.aovName);
    // Get the buffer that the renderer will draw into from the render index.
    HdRenderBuffer* colorBuffer = static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovPath));

    // If there is no color buffer in this render index it was determined that the color buffer
    // to write into should come from the input buffer from the previous pass.
    if (!colorBuffer)
    {
        // Use the input color buffer
        colorBuffer = colorInputAov.buffer;
    }
    else
    {
        // This might be a newly created BPrim.  Allocate the GPU texture if needed.
        colorBuffer->Allocate(desc.dimensions, desc.format, desc.multiSampled);
    }

    HgiTextureHandle colorOutput =
        colorBuffer->GetResource(desc.multiSampled).Get<HgiTextureHandle>();
    if (!colorOutput)
    {
        TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
            colorInputAov.aovName.GetText());
        return;
    }
    // If the input and output are the same texture, no need to copy.
    if (colorOutput == colorInput)
        return;

    // Get the depth texture handle from the input depth buffer.
    HgiTextureHandle depthOutput;
    if (depthInput)
    {
        const SdfPath aovDepthPath = GetAovPath(controllerId, pxr::HdAovTokens->depth);

        // Get the buffer that the renderer will draw into from the render index.
        HdRenderBuffer* depthBuffer = static_cast<HdRenderBuffer*>(
            _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovDepthPath));

        // If there is no depth buffer in this render index it was determined that the depth buffer
        // to write into should come from the input buffer from the previous pass.
        if (!depthBuffer)
        {
            // Use the input color buffer
            depthBuffer = depthInputAov.buffer;
        }
        else
        {
            // This might be a newly created BPrim.  Allocate the GPU texture if needed.
            depthBuffer->Allocate(desc.dimensions, HdFormatFloat32, desc.multiSampled);
        }

        if (depthBuffer)
            depthOutput = depthBuffer->GetResource(desc.multiSampled).Get<HgiTextureHandle>();

        if (!depthOutput)
        {
            TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                aovDepthPath.GetName().c_str());
            return;
        }
    }

    // Initialize the shader that will copy the contents from the input to the output.
    if (!_copyShader)
    {
        _copyShader =
            std::make_unique<HdxFullscreenShader>(GetHgi(_pRenderIndex), "Copy Color Buffer");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = TfToken("Copy Color Buffer");
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
        HgiShaderFunctionAddTexture(&shaderDesc, "depthIn", 1);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "screenSize", "vec2");

        _copyShader->SetProgram(_GetShaderPath(), _shaderTokens->shaderWithDepth, shaderDesc);
    }

    // Initialize the shader that will copy the contents from the input to the output.
    if (!_copyShaderNoDepth)
    {
        _copyShaderNoDepth =
            std::make_unique<HdxFullscreenShader>(GetHgi(_pRenderIndex), "Copy Color Buffer No Depth");

        HgiShaderFunctionDesc shaderDesc;
        shaderDesc.debugName   = TfToken("Copy Color Buffer No Depth");
        shaderDesc.shaderStage = HgiShaderStageFragment;
        HgiShaderFunctionAddStageInput(&shaderDesc, "uvOut", "vec2");
        HgiShaderFunctionAddTexture(&shaderDesc, "colorIn", 0);
        HgiShaderFunctionAddStageOutput(&shaderDesc, "hd_FragColor", "vec4", "color");
        HgiShaderFunctionAddConstantParam(&shaderDesc, "screenSize", "vec2");

        _copyShaderNoDepth->SetProgram(_GetShaderPath(), _shaderTokens->shader, shaderDesc);
    }

    PXR_NS::HdxFullscreenShader* shader =
        (!depthInput ? _copyShaderNoDepth.get() : _copyShader.get());

    // Set the screen size constant on the shader.
    GfVec2f screenSize { static_cast<float>(desc.dimensions[0]),
        static_cast<float>(desc.dimensions[1]) };
    shader->SetShaderConstants(sizeof(screenSize), &screenSize);
 
    // Submit the layout change to read from the textures.
    auto colorUsage = colorInput->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    if (!depthInput)
    {
        shader->BindTextures({ colorInput });
        shader->Draw(colorOutput, HgiTextureHandle());
    }
    else
    {
        auto depthUsage = depthInput->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
        shader->BindTextures({ colorInput, depthInput });
        shader->Draw(colorOutput, depthOutput);
        depthInput->SubmitLayoutChange(depthUsage);
    }

    colorInput->SubmitLayoutChange(colorUsage);
}

bool RenderBufferManager::Impl::SetRenderOutputs(const TfTokenVector& outputs,
    RenderBufferBindings const& inputs, GfVec4d const& viewport, SdfPath const& controllerId)
{
    if (!IsAovSupported())
    {
        return false;
    }

    bool somethingChanged = true;
    // If progressive rendering is enabled, do not return early.
    if (!_isProgressiveRenderingEnabled)
    {
        // Check if cached inputs and outputs have changed.
        if (_aovOutputs == outputs && inputs.size() == _aovInputs.size() &&
            std::equal(inputs.begin(), inputs.end(), _aovInputs.begin(), _aovInputs.end()))
        {
            somethingChanged = false;
        }
    }

    _aovOutputs = outputs;

    // Temporary 2D dimensions to calculate dimensions3 (the 2D version isn't used later).
    const GfVec2i dimensions = _renderBufferSize != GfVec2i(0)
        ? _renderBufferSize
        : GfVec2i(static_cast<int>(viewport[2]), static_cast<int>(viewport[3]));
    const GfVec3i dimensions3(dimensions[0], dimensions[1], 1);

    HdAovDescriptorList outputDescs;

    // NOTE: A function could be used to get localOutputs.
    TfTokenVector localOutputs = outputs;

    if (somethingChanged)
    {
        _aovInputs.clear();
        if (inputs.size() > 0)
        {
            std::copy(inputs.begin(), inputs.end(), back_inserter(_aovInputs));
            _viewportAov = TfToken();
        }

        // If progressive rendering is enabled, render buffer clear is only required when
        // `_aovOutputs != outputs`.
        bool needClear = !_isProgressiveRenderingEnabled || _aovOutputs != outputs;
         
        // This will delete Bprims from the RenderIndex and clear the _viewportAov and _aovBufferIds
        // SdfPathVector.
        if (needClear)
        {
            for (size_t i = 0; i < _aovBufferIds.size(); ++i)
            {
                _pRenderIndex->RemoveBprim(HdPrimTypeTokens->renderBuffer, _aovBufferIds[i]);
            }
            // Clearing the viewport AOV triggers the recreation of the bindings after removing the
            // BPrims.
            _viewportAov = TfToken();
            _aovBufferIds.clear();
        }
    }

    // Get default AOV descriptors from the render delegate for each AOV token.
    // E.g. color:HdFormatFloat16Vec4, depth:HdFormatFloat32.
    for (auto it = localOutputs.begin(); it != localOutputs.end();)
    {
        HdAovDescriptor desc = _pRenderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(*it);
        if (desc.format == HdFormatInvalid)
        {
            // The backend doesn't support this AOV, so skip it.
            it = localOutputs.erase(it);
        }
        else
        {
            // Otherwise, stash the desc and move forward.
            outputDescs.push_back(desc);
            ++it;
        }
    }

    // Add the new RenderBuffers.
    // NOTE: GetAovPath returns ids of the form {controller_id}/aov_{name}.
    std::string rendererName = _pRenderIndex->GetRenderDelegate()->GetRendererDisplayName();
    RenderBufferBinding colorInput {};
    RenderBufferBinding depthInput {};
    HdRenderBufferDescriptor colorDesc;
    for (size_t i = 0; i < localOutputs.size(); ++i)
    {
        HdRenderBufferDescriptor desc;
        desc.dimensions   = dimensions3;
        desc.format       = outputDescs[i].format;
        desc.multiSampled = _enableMultisampling;
        bool inputFound   = false;

        for (auto input : inputs)
        {
            if (input.aovName == localOutputs[i])
            {
                inputFound = (rendererName == input.rendererName);
                if (localOutputs[i] == pxr::HdAovTokens->depth)
                {
                    depthInput = input;
                    if (inputFound)
                    {
                        // If the renderer remains the same, we don't want to copy the depth buffer.
                        // The existing depth buffer will continue to be used.
                        // We do this in order to not loose sub-pixel depth information.
                        // However, this means that if any Tasks write to the depth after a sub-pixel 
                        // resolve then the depth buffer will be inconsistent with the color buffer and 
                        // that depth information will be lost.  I don't think this currently happens in 
                        // practice, so we are opting in favor of keeping the sub-pixel resolution.
                        // 
                        // FUTURE: We may want to revisit this decision in the future.  
                        // The long-term solution may be to do post processing at the sub-pixel accuracy.
                        depthInput.texture = HgiTextureHandle();
                    }
                }
                else if (!colorInput.texture)
                {
                    colorDesc  = desc;
                    colorInput = input;
                }
                break;
            }
        }

        // If something has changed and the input was not found or the previous renderer is different 
        // than the current one, then we need to create a new render buffer.  
        // This will be the buffer used and the previous contents potentially copied into.
        if (somethingChanged && !inputFound)
        {
            const SdfPath aovId = GetAovPath(controllerId, localOutputs[i]);
            _pRenderIndex->InsertBprim(HdPrimTypeTokens->renderBuffer, _syncDelegate.get(), aovId);

            _syncDelegate->SetValue(aovId, _tokens->renderBufferDescriptor, VtValue(desc));
            _syncDelegate->SetValue(aovId, HdStRenderBufferTokens->stormMsaaSampleCount,
                VtValue(desc.multiSampled ? _msaaSampleCount : 1));
            _pRenderIndex->GetChangeTracker().MarkBprimDirty(
                aovId, HdRenderBuffer::DirtyDescription);
            _aovBufferIds.push_back(aovId);
        }
    }
    if (colorInput.texture)
    {
        PrepareBuffersFromInputs(colorInput, depthInput, colorDesc, controllerId);
    }

    // Create the list of AOV bindings.
    // This section only fills the 3 vectors below: aovBindingsClear, aovBindingsNoClear,
    // aovInputBindings.
    // Only the first render task clears AOVs so we also have a bindings set that specifies no clear
    // color for the remaining render tasks.
    HdRenderPassAovBindingVector aovBindingsClear;
    HdRenderPassAovBindingVector aovBindingsNoClear;
    HdRenderPassAovBindingVector aovInputBindings;
    aovBindingsClear.resize(localOutputs.size());
    aovBindingsNoClear.resize(aovBindingsClear.size());

    for (size_t i = 0; i < localOutputs.size(); ++i)
    {
        RenderBufferBinding foundInput {};
        for (auto input : inputs)
        {
            if (input.aovName == localOutputs[i])
            {
                foundInput = input;
                break;
            }
        }

        aovBindingsClear[i].aovName    = localOutputs[i];
        aovBindingsClear[i].clearValue = !foundInput.buffer ? outputDescs[i].clearValue : VtValue();
        aovBindingsClear[i].renderBufferId = GetAovPath(controllerId, localOutputs[i]);
        aovBindingsClear[i].aovSettings    = outputDescs[i].aovSettings;

        // Note, it would be better to just assign the output buffer here, but this breaks some
        // unit tests that expect this to be null and do a pointer-as-string comparison if it is not 
        // which is not easily fixable.
        HdRenderBuffer* outputBuffer = static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
            HdPrimTypeTokens->renderBuffer, aovBindingsClear[i].renderBufferId));
        aovBindingsClear[i].renderBuffer = !outputBuffer ? foundInput.buffer : nullptr;

        aovBindingsNoClear[i]            = aovBindingsClear[i];
        aovBindingsNoClear[i].clearValue = VtValue();

        if (localOutputs[i] == HdAovTokens->depth)
        {
            aovInputBindings.push_back(aovBindingsNoClear[i]);
        }
    }

    // Used by the render tasks to indicate what targets are rendered into
    _aovTaskCache.aovBindingsClear   = aovBindingsClear;
    _aovTaskCache.aovBindingsNoClear = aovBindingsNoClear;

    // Used for volume rendering and contains only depth.
    _aovTaskCache.aovInputBindings   = aovInputBindings;

    _aovTaskCache.hasNoAovInputs     = (inputs.size() == 0); // For progressive rendering only?

    const SdfPath volumeId = GetRenderTaskPath(controllerId, HdStMaterialTagTokens->volume);

    if (localOutputs.size() > 0)
    {
        SetViewportRenderOutput(localOutputs[0], controllerId);
    }

    // NOTE: The viewport data plumbed to tasks unfortunately depends on whether aovs are being
    // used.
    return true;
}

void RenderBufferManager::Impl::SetViewportRenderOutput(TfToken const& name, const SdfPath& controllerId)
{
    if (!IsAovSupported())
    {
        return;
    }

    if (_viewportAov == name)
    {
        return;
    }
    _viewportAov = name;

    _aovTaskCache.aovBufferPath   = SdfPath::EmptyPath();
    _aovTaskCache.depthBufferPath = SdfPath::EmptyPath();
    _aovTaskCache.neyeBufferPath  = SdfPath::EmptyPath();
    _aovTaskCache.aovBuffer       = nullptr;
    _aovTaskCache.depthBuffer     = nullptr;
    _aovTaskCache.neyeBuffer      = nullptr;

    if (!name.IsEmpty())
    {
        _aovTaskCache.aovBufferPath = GetAovPath(controllerId, name);
        _aovTaskCache.aovBuffer     = GetRenderOutput(name, controllerId);
        if (name == HdAovTokens->color)
        {
            // if we are visualizing the color AOV then we want to set the depth (and Neye?) as well.
            _aovTaskCache.depthBufferPath = GetAovPath(controllerId, HdAovTokens->depth);
            _aovTaskCache.neyeBufferPath  = GetAovPath(controllerId, HdAovTokens->Neye);
            _aovTaskCache.depthBuffer     = GetRenderOutput(HdAovTokens->depth, controllerId);
            _aovTaskCache.neyeBuffer      = GetRenderOutput(HdAovTokens->Neye, controllerId);
        }
    }
}

HdRenderBuffer* RenderBufferManager::Impl::GetRenderOutput(
    const TfToken& name, const SdfPath& controllerId)
{
    if (!IsAovSupported())
    {
        return nullptr;
    }

    for (auto& binding : _aovTaskCache.aovBindingsClear)
    {
        if (name == binding.aovName)
        {
            if (binding.renderBuffer)
                return binding.renderBuffer;

            const SdfPath aovId = GetAovPath(controllerId, name);
            return static_cast<HdRenderBuffer*>(
                _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovId));
        }
    }

    return nullptr;
}

void RenderBufferManager::Impl::SetRenderOutputClearColor(
    const TfToken& name, const SdfPath& controllerId, const VtValue& clearValue)
{
    if (!IsAovSupported())
    {
        return;
    }

    // Check if we're setting a value for a nonexistent AOV.
    const SdfPath renderBufferId = GetAovPath(controllerId, name);

    if (clearValue.IsEmpty())
    {
        _aovTaskCache.outputClearValues.erase(renderBufferId);
    }
    else
    {
       _aovTaskCache.outputClearValues[renderBufferId] = clearValue;
    }
}

void RenderBufferManager::Impl::SetBufferSizeAndMsaa(
    const GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled)
{
    HdChangeTracker& changeTracker = _pRenderIndex->GetChangeTracker();

    bool descriptorSpecsChanged = false;
    bool msaaSampleCountChanged = false;

    if (_enableMultisampling != msaaEnabled || _renderBufferSize != newRenderBufferSize)
    {
        _renderBufferSize      = newRenderBufferSize;
        _enableMultisampling   = msaaEnabled;
        descriptorSpecsChanged = true;
    }

    if (_msaaSampleCount != msaaSampleCount)
    {
        _msaaSampleCount       = msaaSampleCount;
        msaaSampleCountChanged = true;
    }

    if (!msaaSampleCountChanged && !descriptorSpecsChanged)
    {
        // No changes to the render buffer size or MSAA sample count.
        return;
    }

    const GfVec3i dimensions3(_renderBufferSize[0], _renderBufferSize[1], 1);

    for (auto const& id : _aovBufferIds)
    {
        bool bprimDirty = false;
        if (descriptorSpecsChanged)
        {
            VtValue vParams = _syncDelegate->GetValue(id, _tokens->renderBufferDescriptor);
            HdRenderBufferDescriptor desc = vParams.Get<HdRenderBufferDescriptor>();

            if (desc.dimensions != dimensions3 || desc.multiSampled != _enableMultisampling)
            {
                desc.dimensions   = dimensions3;
                desc.multiSampled = _enableMultisampling;
                _syncDelegate->SetValue(id, _tokens->renderBufferDescriptor, VtValue(desc));
                bprimDirty = true;
            }
        }

        if (msaaSampleCountChanged)
        {
            _syncDelegate->SetValue(
                id, HdStRenderBufferTokens->stormMsaaSampleCount, VtValue(_msaaSampleCount));
            bprimDirty = true;
        }

        if (bprimDirty)
        {
            changeTracker.MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
        }
    }
}

RenderBufferManager::RenderBufferManager(
    SdfPath const& taskManagerUid, HdRenderIndex* pRenderIndex, SyncDelegatePtr& syncDelegate) :
    _taskManagerUid(taskManagerUid), _pRenderIndex(pRenderIndex)
{
    _impl = std::make_unique<Impl>(pRenderIndex, syncDelegate);
}

RenderBufferManager::~RenderBufferManager() {}

HgiTextureHandle RenderBufferManager::GetAovTexture(TfToken const& token, HdEngine* engine) const
{
    VtValue aov;
    HgiTextureHandle aovTexture;

    // NOTE: The Metal only implementation needs an access to "id<MTLTexture>" that
    // only the HgiTextureHandle provides (by casting to HgiMetalTexture).

    if (engine->GetTaskContextData(token, &aov))
    {
        if (aov.IsHolding<HgiTextureHandle>())
        {
            aovTexture = aov.Get<HgiTextureHandle>();
        }
    }

    return aovTexture;
}

bool RenderBufferManager::IsAovSupported() const
{
    return _pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer);
}

bool RenderBufferManager::IsProgressiveRenderingEnabled() const
{
    return _impl->IsProgressiveRenderingEnabled();
}

AovParams const& RenderBufferManager::GetAovParamCache() const
{
    return _impl->GetAovParamCache();
}

PresentationParams const& RenderBufferManager::GetPresentationParams() const
{
    return _impl->GetPresentationParams();
}

TfToken const& RenderBufferManager::GetViewportAov() const
{
    return _impl->GetViewportAov();
}

GfVec2i const& RenderBufferManager::GetRenderBufferSize() const
{
    return _impl->GetRenderBufferSize();
}

HdRenderBuffer* RenderBufferManager::GetRenderOutput(const TfToken& name)
{
    return _impl->GetRenderOutput(name, _taskManagerUid);
}

void RenderBufferManager::SetBufferSizeAndMsaa(
    const GfVec2i& size, size_t msaaSampleCount, bool msaaEnabled)
{
    _impl->SetBufferSizeAndMsaa(size, msaaSampleCount, msaaEnabled);
}

void RenderBufferManager::SetRenderOutputClearColor(const TfToken& name, const VtValue& clearValue)
{
    _impl->SetRenderOutputClearColor(name, _taskManagerUid, clearValue);
}

bool RenderBufferManager::SetRenderOutputs(
    TfTokenVector const& names, RenderBufferBindings const& inputs, GfVec4d const& viewport)
{
    return _impl->SetRenderOutputs(names, inputs, viewport, _taskManagerUid);
}

void RenderBufferManager::SetPresentationOutput(TfToken const& api, VtValue const& framebuffer)
{
    _impl->SetPresentationOutput(api, framebuffer);
}

void RenderBufferManager::SetInteropPresentation(
    VtValue const& destination, VtValue const& composition)
{
    _impl->SetInteropPresentation(destination, composition);
}

void RenderBufferManager::SetWindowPresentation(VtValue const& window, bool vsync)
{
    _impl->SetWindowPresentation(window, vsync);
}

} // namespace HVT_NS
