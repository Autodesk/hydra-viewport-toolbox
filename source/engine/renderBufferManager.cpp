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

#include <hvt/engine/engine.h>
#include <hvt/engine/hgiInstance.h>
#include <hvt/engine/renderBufferManager.h>
#include <hvt/engine/taskUtils.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/resources.h>

#include "copyDepthShader.h"

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
#include <pxr/imaging/hd/renderBufferSchema.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

class RenderBufferDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(RenderBufferDataSource)

    GfVec3i dimensions;
    HdFormat format;
    bool multiSampled;
    uint32_t msaaSampleCount;

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdRenderBufferSchemaTokens->dimensions)
            return HdRetainedTypedSampledDataSource<GfVec3i>::New(dimensions);
        if (name == HdRenderBufferSchemaTokens->format)
            return HdRetainedTypedSampledDataSource<HdFormat>::New(format);
        if (name == HdRenderBufferSchemaTokens->multiSampled)
            return HdRetainedTypedSampledDataSource<bool>::New(multiSampled);
        if (name == HdStRenderBufferTokens->stormMsaaSampleCount)
            return HdRetainedTypedSampledDataSource<uint32_t>::New(msaaSampleCount);
        return nullptr;
    }

    TfTokenVector GetNames() override
    {
        // clang-format off
        static const TfTokenVector result = {
            HdRenderBufferSchemaTokens->dimensions,
            HdRenderBufferSchemaTokens->format,
            HdRenderBufferSchemaTokens->multiSampled,
            HdStRenderBufferTokens->stormMsaaSampleCount 
        };
        // clang-format on

        return result;
    }

private:
    RenderBufferDataSource(
        GfVec3i const& dimensions, HdFormat format, bool multiSampled, uint32_t msaaSampleCount) :
        dimensions(dimensions),
        format(format),
        multiSampled(multiSampled),
        msaaSampleCount(msaaSampleCount)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(RenderBufferDataSource);

Hgi* GetHgi(HdRenderIndex const* renderIndex)
{
    Hgi* hgi = hvt::HgiInstance::instance().hgi();
    if (hgi)
        return hgi;

    // If it wasn't created by the HgiInstance look for it on the render index.
    HdDriverVector const& drivers = renderIndex->GetDrivers();
    for (HdDriver* hdDriver : drivers)
    {
        if ((hdDriver->name == HgiTokens->renderDriver) && hdDriver->driver.IsHolding<Hgi*>())
        {
            hgi = hdDriver->driver.UncheckedGet<Hgi*>();
            if (hgi)
                return hgi;
        }
    }

    return nullptr;
}

} // anonymous namespace

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
    explicit Impl(
        HdRenderIndex* pRenderIndex, HdRetainedSceneIndexRefPtr const& retainedSceneIndex);
    ~Impl();

    Impl(Impl const&)            = delete;
    Impl& operator=(Impl const&) = delete;

    /// Sets the size of the render buffer and MSAA settings, update render buffer descriptors.
    void SetBufferSizeAndMsaa(
        const GfVec2i newRenderBufferSize, size_t msaaSampleCount, bool msaaEnabled);

    HdRenderBuffer* GetRenderOutput(TfToken const& name, SdfPath const& controllerId);

    /// Updates render output parameters and creates new render buffers if needed.
    /// Note: AOV binding values are stored here and consulted later by RenderTasks.
    bool SetRenderOutputs(TfToken const& outputToVisualize, TfTokenVector const& outputs,
        RenderBufferBindings const& inputs, GfVec4d const& viewport, SdfPath const& controllerId);

    /// Get the render outputs.
    TfTokenVector const& GetRenderOutputs() const { return _aovOutputs; }

    /// Updates the render output clear color.
    /// Note: Clear color values are stored here and consulted later by RenderTasks.
    void SetRenderOutputClearColor(
        TfToken const& name, SdfPath const& controllerId, VtValue const& clearValue);

    /// Set the framebuffer to present the render to.
    void SetPresentationOutput(TfToken const& api, VtValue const& framebufferHandle)
    {
        _presentParams.windowHandle      = VtValue();
        _presentParams.api               = api;
        _presentParams.framebufferHandle = framebufferHandle;
    }

    /// Set interop destination handle to present to and composition parameters.
    void SetInteropPresentation(VtValue const& destinationInteropHandle, VtValue const& composition)
    {
        // NOTE: The underlying type of destinationInteropHandle VtValue is HgiPresentInteropHandle,
        // which is a std::variant. See declaration of HgiPresentInteropHandle for more details.
        _presentParams.windowHandle      = VtValue();
        _presentParams.framebufferHandle = destinationInteropHandle;
        _presentParams.compositionParams = composition;
    }

    /// Set vsync and window destination handle to present to.
    void SetWindowPresentation(VtValue const& windowHandle, bool vsync)
    {
        // NOTE: The underlying type of windowHandle VtValue is HgiPresentWindowHandle,
        // which is a std::variant. See declaration of HgiPresentWindowHandle.
        _presentParams.windowHandle      = windowHandle;
        _presentParams.windowVsync       = vsync;
        _presentParams.framebufferHandle = VtValue();
    }

    /// Returns true if AOVs (RenderBuffer Bprims) are supported by the render delegate.
    bool IsAovSupported() const override;

    /// Returns true if progressive rendering is enabled.
    bool IsProgressiveRenderingEnabled() const override { return _isProgressiveRenderingEnabled; }

    /// Returns the name of the AOV to be used for the viewport.
    TfToken const& GetViewportAov() const override { return _viewportAov; }

    /// Get the size of the render buffers.
    GfVec2i const& GetRenderBufferSize() const override;

    /// Returns the AOV parameter cache, containing data required to update RenderTask AOV binding
    /// parameters.
    AovParams const& GetAovParamCache() const override { return _aovTaskCache; }

    // Returns the presentation parameters, containing data relevant to the HdxPresentTask.
    PresentationParams const& GetPresentationParams() const override { return _presentParams; }

private:
    /// Copy the color & depth AOVs of the input buffers into the output buffers.
    void _PrepareBuffersFromInputs(RenderBufferBinding const& colorInput,
        RenderBufferBinding const& depthInput, HdRenderBufferDescriptor const& desc,
        SdfPath const& controllerId);

    /// Copy the depth AOV of the input buffer into the output buffer.
    void _PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
        HdRenderBufferDescriptor const& desc, SdfPath const& controllerId);

    /// Sets the viewport render output (color or buffer visualization).
    void SetViewportRenderOutput(const TfToken& name, const SdfPath& controllerId);

    /// The render texture dimensions.
    GfVec2i _renderBufferSize { 0, 0 };

    /// Multisampling enabled or not.
    bool _enableMultisampling { true };

    /// Number of samples for multisampling.
    size_t _msaaSampleCount { 4 };

    bool _isProgressiveRenderingEnabled { false };

    /// List of Bprim IDs. These IDs are used to:
    ///  - Add and remove Bprims from the retained scene index.
    ///  - Get Bprims from the RenderIndex.
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
    HdRenderIndex* _pRenderIndex { nullptr };

    /// The retained scene index used for render buffer Bprims.
    HdRetainedSceneIndexRefPtr _retainedSceneIndex;

    /// The shaders used to copy the contents of the input into the output render buffer.
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShader;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> _copyColorShaderNoDepth;
    std::unique_ptr<CopyDepthShader> _copyDepthShader;
};

RenderBufferManager::Impl::Impl(
    HdRenderIndex* pRenderIndex, HdRetainedSceneIndexRefPtr const& retainedSceneIndex) :
    _renderBufferSize(0, 0), _pRenderIndex(pRenderIndex), _retainedSceneIndex(retainedSceneIndex)
{
    _presentParams.api             = HgiTokens->OpenGL;
    _isProgressiveRenderingEnabled = { TfGetenvBool("AGP_ENABLE_PROGRESSIVE_RENDERING", false) };
}

RenderBufferManager::Impl::~Impl()
{
    if (_retainedSceneIndex && !_aovBufferIds.empty())
    {
        HdSceneIndexObserver::RemovedPrimEntries entries;
        for (auto const& id : _aovBufferIds)
        {
            entries.push_back({ id });
        }
        _retainedSceneIndex->RemovePrims(entries);
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

void RenderBufferManager::Impl::_PrepareBuffersFromInputs(RenderBufferBinding const& colorInputAov,
    RenderBufferBinding const& depthInputAov, HdRenderBufferDescriptor const& desc,
    SdfPath const& controllerId)
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
        if (!colorBuffer->IsMapped())
        {
            // This might be a newly created BPrim.  Allocate the GPU texture if needed.
            colorBuffer->Allocate(desc.dimensions, desc.format, desc.multiSampled);
        }
    }

    HgiTextureHandle colorOutput;
    VtValue colorOutputValue = colorBuffer->GetResource(desc.multiSampled);
    if (colorOutputValue.IsHolding<HgiTextureHandle>())
    {
        colorOutput = colorOutputValue.Get<HgiTextureHandle>();
        if (!colorOutput)
        {
            TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                colorInputAov.aovName.GetText());
            return;
        }
    }
    else
    {
        // The output render buffer is not holding a writeable buffer.
        // You will need to composite to blend passes results.
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
            if (!depthBuffer->IsMapped())
            {
                // This might be a newly created BPrim.  Allocate the GPU texture if needed.
                depthBuffer->Allocate(desc.dimensions, HdFormatFloat32, desc.multiSampled);
            }
        }

        if (depthBuffer)
        {
            VtValue depthOutputValue = depthBuffer->GetResource(desc.multiSampled);
            if (depthOutputValue.IsHolding<HgiTextureHandle>())
            {
                if (depthBuffer)
                    depthOutput = depthOutputValue.Get<HgiTextureHandle>();

                if (!depthOutput)
                {
                    TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                        aovDepthPath.GetName().c_str());
                    return;
                }
            }
            else
            {
                // The output render buffer is not holding a writeable buffer.
                // You will need to composite to blend passes results.
                return;
            }
        }
    }

    Hgi* hgi = GetHgi(_pRenderIndex);
    if (!hgi)
    {
        TF_CODING_ERROR("There is no valid Hgi driver.");
        return;
    }

    // Initialize the shader that will copy the contents from the input to the output.
    if (!_copyColorShader)
    {
        _copyColorShader = std::make_unique<HdxFullscreenShader>(hgi, "Copy Color Buffer");
    }

    // Initialize the shader that will copy the contents from the input to the output.
    if (!_copyColorShaderNoDepth)
    {
        _copyColorShaderNoDepth =
            std::make_unique<HdxFullscreenShader>(hgi, "Copy Color Buffer No Depth");
    }

    HdxFullscreenShader* shader =
        (!depthInput ? _copyColorShaderNoDepth.get() : _copyColorShader.get());

    // Submit the layout change to read from the textures.
    colorInput->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);

    if (!depthInput)
    {
        shader->BindTextures({ colorInput });
        shader->Draw(colorOutput, HgiTextureHandle());
    }
    else
    {
        depthInput->SubmitLayoutChange(HgiTextureUsageBitsShaderRead);
        shader->BindTextures({ colorInput, depthInput });
        shader->Draw(colorOutput, depthOutput);
        depthInput->SubmitLayoutChange(HgiTextureUsageBitsDepthTarget);
    }

    colorInput->SubmitLayoutChange(HgiTextureUsageBitsColorTarget);
}

// The code does not use the HdxFullscreenShader helper here because it only needs to copy the depth
// AOVs and HdxFullscreenShader always needs the color AOVs.
void RenderBufferManager::Impl::_PrepareDepthOnlyFromInput(RenderBufferBinding const& inputDepthAov,
    HdRenderBufferDescriptor const& desc, SdfPath const& controllerId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HgiTextureHandle input = inputDepthAov.texture;

    if (!input)
    {
        return;
    }

    const SdfPath aovPath = GetAovPath(controllerId, inputDepthAov.aovName);
    // Get the buffer that the renderer will draw into from the render index.
    HdRenderBuffer* buffer = static_cast<HdRenderBuffer*>(
        _pRenderIndex->GetBprim(HdPrimTypeTokens->renderBuffer, aovPath));

    // If there is no buffer in this render index it was determined that the buffer
    // to write into should come from the input buffer from the previous pass.
    if (!buffer)
    {
        // Use the input buffer
        buffer = inputDepthAov.buffer;
    }
    else
    {
        if (!buffer->IsMapped())
        {
            // This might be a newly created BPrim.  Allocate the GPU texture if needed.
            buffer->Allocate(desc.dimensions, desc.format, desc.multiSampled);
        }
    }

    HgiTextureHandle output;
    VtValue outputValue = buffer->GetResource(desc.multiSampled);
    if (outputValue.IsHolding<HgiTextureHandle>())
    {
        output = outputValue.Get<HgiTextureHandle>();
        if (!output)
        {
            TF_CODING_ERROR("The output render buffer does not have a valid texture %s.",
                inputDepthAov.aovName.GetText());
            return;
        }
    }
    else
    {
        // The output render buffer is not holding a writeable buffer.
        // You will need to composite to blend passes results.
        return;
    }

    // If the input and output are the same texture, no need to copy.
    if (output == input)
    {
        return;
    }

    Hgi* hgi = GetHgi(_pRenderIndex);
    if (!hgi)
    {
        TF_CODING_ERROR("There is no valid Hgi driver.");
        return;
    }

    // Note: HdxFullscreenShader must include the color AOV so it cannot be used here
    // because the code only needs to copy the depth AOV.

    if (!_copyDepthShader)
    {
        _copyDepthShader = std::make_unique<CopyDepthShader>(hgi);
    }

    // Copy the input to the output texture.
    _copyDepthShader->Execute(input, output);
}

bool RenderBufferManager::Impl::SetRenderOutputs(TfToken const& outputToVisualize,
    TfTokenVector const& outputs, RenderBufferBindings const& inputs, GfVec4d const& viewport,
    SdfPath const& controllerId)
{
    bool hasRemovedBuffers = false;

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

        // This will delete Bprims from the retained scene index and clear the _viewportAov and
        // _aovBufferIds SdfPathVector.
        if (needClear)
        {
            if (!_aovBufferIds.empty())
            {
                HdSceneIndexObserver::RemovedPrimEntries removedEntries;
                for (auto const& id : _aovBufferIds)
                {
                    removedEntries.push_back({ id });
                }
                _retainedSceneIndex->RemovePrims(removedEntries);
            }

            hasRemovedBuffers = true;

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
    const std::string rendererName = _pRenderIndex->GetRenderDelegate()->GetRendererDisplayName();
    RenderBufferBinding colorInput, depthInput;
    HdRenderBufferDescriptor colorDesc, depthDesc;
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
                    depthDesc  = desc;
                    depthInput = input;

                    if (inputFound)
                    {
                        // If the renderer remains the same, we don't want to copy the depth buffer.
                        // The existing depth buffer will continue to be used.
                        // We do this in order to not loose sub-pixel depth information.
                        // However, this means that if any Tasks write to the depth after a
                        // sub-pixel resolve then the depth buffer will be inconsistent with the
                        // color buffer and that depth information will be lost.  I don't think this
                        // currently happens in practice, so we are opting in favor of keeping the
                        // sub-pixel resolution.
                        //
                        // FUTURE: We may want to revisit this decision in the future.
                        // The long-term solution may be to do post processing at the sub-pixel
                        // accuracy.
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

        // If something has changed and the input was not found or the previous renderer is
        // different than the current one, then we need to create a new render buffer. This will be
        // the buffer used and the previous contents potentially copied into.
        if (somethingChanged && !inputFound)
        {
            const SdfPath aovId = GetAovPath(controllerId, localOutputs[i]);
            const uint32_t msaaCount =
                desc.multiSampled ? static_cast<uint32_t>(_msaaSampleCount) : 1;
            _retainedSceneIndex->AddPrims({ { aovId, HdPrimTypeTokens->renderBuffer,
                HdRetainedContainerDataSource::New(HdRenderBufferSchema::GetSchemaToken(),
                    RenderBufferDataSource::New(
                        desc.dimensions, desc.format, desc.multiSampled, msaaCount)) } });
            _aovBufferIds.push_back(aovId);
        }
    }

    // In case, we want to share the AOV buffers between frame passes but they are from different
    // render delegates, we then need to copy the AOV to visualize. But be careful that's not always
    // the color one we visualize.

    // Color AOV always means color & depth AOVs (where depth is optional).
    if (outputToVisualize == pxr::HdAovTokens->color && colorInput.texture)
    {
        _PrepareBuffersFromInputs(colorInput, depthInput, colorDesc, controllerId);
    }
    // But depth AOV only means depth AOV only.
    else if (outputToVisualize == pxr::HdAovTokens->depth && depthInput.texture)
    {
        _PrepareDepthOnlyFromInput(depthInput, depthDesc, controllerId);
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
        HdRenderBuffer* outputBuffer     = static_cast<HdRenderBuffer*>(_pRenderIndex->GetBprim(
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
    _aovTaskCache.aovInputBindings = aovInputBindings;

    _aovTaskCache.hasNoAovInputs = (inputs.size() == 0); // For progressive rendering only?

    const SdfPath volumeId = GetRenderTaskPath(controllerId, HdStMaterialTagTokens->volume);

    if (localOutputs.size() > 0)
    {
        SetViewportRenderOutput(outputToVisualize, controllerId);
    }

    // NOTE: The viewport data plumbed to tasks unfortunately depends on whether aovs are being
    // used.
    return hasRemovedBuffers;
}

void RenderBufferManager::Impl::SetViewportRenderOutput(
    TfToken const& name, const SdfPath& controllerId)
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
            // if we are visualizing the color AOV then we want to set the depth (and Neye?) as
            // well.
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
    const uint32_t newMsaaCount =
        _enableMultisampling ? static_cast<uint32_t>(_msaaSampleCount) : 1;

    for (auto const& id : _aovBufferIds)
    {
        HdSceneIndexPrim prim = _retainedSceneIndex->GetPrim(id);
        if (!prim.dataSource)
        {
            continue;
        }
        RenderBufferDataSourceHandle ds = RenderBufferDataSource::Cast(
            HdRenderBufferSchema::GetFromParent(prim.dataSource).GetContainer());
        if (!ds)
        {
            continue;
        }

        bool dirty = false;
        if (descriptorSpecsChanged &&
            (ds->dimensions != dimensions3 || ds->multiSampled != _enableMultisampling))
        {
            ds->dimensions   = dimensions3;
            ds->multiSampled = _enableMultisampling;
            dirty            = true;
        }
        if (msaaSampleCountChanged && ds->msaaSampleCount != newMsaaCount)
        {
            ds->msaaSampleCount = newMsaaCount;
            dirty               = true;
        }
        if (dirty)
        {
            _retainedSceneIndex->DirtyPrims(
                { { id, HdDataSourceLocatorSet { HdRenderBufferSchema::GetDefaultLocator() } } });
        }
    }
}

RenderBufferManager::RenderBufferManager(SdfPath const& taskManagerUid, HdRenderIndex* pRenderIndex,
    HdRetainedSceneIndexRefPtr const& retainedSceneIndex) :
    _taskManagerUid(taskManagerUid), _pRenderIndex(pRenderIndex)
{
    _impl = std::make_unique<Impl>(_pRenderIndex, retainedSceneIndex);
}

RenderBufferManager::~RenderBufferManager() {}

TfTokenVector RenderBufferManager::GetAllRendererAovs()
{
    return { HdAovTokens->color, HdAovTokens->depth, HdAovTokens->primId, HdAovTokens->elementId,
        HdAovTokens->instanceId };
}

TfTokenVector RenderBufferManager::GetSupportedRendererAovs() const
{
    if (_pRenderIndex->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
    {
        auto const& candidates = GetAllRendererAovs();

        TfTokenVector aovs;
        for (auto const& aov : candidates)
        {
            if (_pRenderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(aov).format !=
                HdFormatInvalid)
            {
                aovs.push_back(aov);
            }
        }
        return aovs;
    }
    return {};
}

HgiTextureHandle RenderBufferManager::GetAovTexture(TfToken const& token, Engine* engine) const
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

bool RenderBufferManager::SetRenderOutputs(TfToken const& visualizeAOV,
    TfTokenVector const& outputs, RenderBufferBindings const& inputs, GfVec4d const& viewport)
{
    return _impl->SetRenderOutputs(visualizeAOV, outputs, inputs, viewport, _taskManagerUid);
}

TfTokenVector const& RenderBufferManager::GetRenderOutputs() const
{
    return _impl->GetRenderOutputs();
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
