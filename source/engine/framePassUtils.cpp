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

#include <hvt/engine/framePassUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

SdfPath GetPickedPrim(FramePass* pass, GfMatrix4d const& pickingMatrix,
    ViewportRect const& viewport, GfMatrix4d const& viewMatrix)
{
    if (!pass || !pass->IsInitialized())
    {
        TF_CODING_ERROR("The frame pass is null or not initialized.");
        return {};
    }

    SdfPath hitPrimPath;

    pass->params().viewInfo.viewport         = viewport;
    pass->params().viewInfo.viewMatrix       = viewMatrix;
    pass->params().viewInfo.projectionMatrix = pickingMatrix;

    pass->UpdateScene();

    const HdSelectionSharedPtr hits = pass->Pick(HdxPickTokens->pickPrimsAndInstances);
    const SdfPathVector allPrims    = hits->GetAllSelectedPrimPaths();
    if (allPrims.size() > 0)
    {
        // TODO: Need to process all paths?
        hitPrimPath = SdfPath(allPrims[0]);
    }

    return hitPrimPath;
}

HdSelectionSharedPtr PickObjects(FramePass* pass [[maybe_unused]],
    GfMatrix4d const& pickingMatrix [[maybe_unused]], ViewportRect const& viewport [[maybe_unused]],
    GfMatrix4d const& viewMatrix [[maybe_unused]], TfToken const& objectType [[maybe_unused]])
{
// ADSK: For pending changes to OpenUSD from Autodesk: deep selection.
#if defined(ADSK_OPENUSD_PENDING)
    // deep selection is not supported using the render graph (yet)
    if (pass && pass->IsInitialized())
    {
        pass->params().viewInfo.viewport         = viewport;
        pass->params().viewInfo.viewMatrix       = viewMatrix;
        pass->params().viewInfo.projectionMatrix = pickingMatrix;

        pass->UpdateScene();

        return pass->Pick(objectType, HdxPickTokens->resolveDeep);
    }
#endif

    return {};
}

void HighlightSelection(FramePass* pass, SdfPathSet const& highlightPaths)
{
    if (!pass)
    {
        TF_CODING_ERROR("The frame pass is null.");
        return;
    }

    const HdSelectionSharedPtr selection = ViewportEngine::PrepareSelection(highlightPaths);
    pass->SetSelection(selection);
}

namespace
{

// The private class mimicking part of the HdStRenderBuffer behavior.
class RenderBufferProxy : public HdRenderBuffer
{
public:
    RenderBufferProxy(HdRenderBuffer* buffer, HgiTextureHandle const& handle) :
        HdRenderBuffer(buffer->GetId()), _renderBuffer(buffer), _textureHandle(handle)
    {
    }

    void Sync(
        HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam, HdDirtyBits* dirtyBits) override
    {
        _renderBuffer->Sync(sceneDelegate, renderParam, dirtyBits);
    }
    void Finalize(HdRenderParam* renderParam) override { _renderBuffer->Finalize(renderParam); }
    bool Allocate(GfVec3i const& dimensions, HdFormat format, bool multiSampled) override
    {
        return _renderBuffer->Allocate(dimensions, format, multiSampled);
    }

    // Note: The dimensions and format of CPU buffers and GPU textures are guaranteed to be
    // identical. Refer to the AovInput implementation for details.
    unsigned int GetWidth() const override { return _renderBuffer->GetWidth(); }
    unsigned int GetHeight() const override { return _renderBuffer->GetHeight(); }
    unsigned int GetDepth() const override { return _renderBuffer->GetDepth(); }
    HdFormat GetFormat() const override { return _renderBuffer->GetFormat(); }

    // Expecting the hdTexture status.
    bool IsMultiSampled() const override
    {
        HgiTextureDesc const& desc = _textureHandle->GetDescriptor();
        return desc.sampleCount != HgiSampleCount1;
    }

    void* Map() override { return _renderBuffer->Map(); }
    void Unmap() override { _renderBuffer->Unmap(); }
    bool IsMapped() const override { return _renderBuffer->IsMapped(); }
    bool IsConverged() const override { return _renderBuffer->IsConverged(); }
    void Resolve() override { _renderBuffer->Resolve(); }

    // Link the CPU render buffer with the GPU texture i.e., like HdStRenderBuffer does it.
    VtValue GetResource(const bool /*multiSampled*/) const override
    {
        return VtValue(_textureHandle);
    }

protected:
    void _Deallocate() override { /* void */ }

private:
    // The original render buffer instance.
    HdRenderBuffer* _renderBuffer { nullptr };
    // The GPU texture handle created by AovInputTask.
    HgiTextureHandle _textureHandle;
};

} // anonymous namespace

std::shared_ptr<HdRenderBuffer> CreateRenderBufferProxy(
    FramePassPtr& framePass, const TfToken& aovToken)
{
    auto renderBuffer  = framePass ? framePass->GetRenderBuffer(aovToken) : nullptr;
    auto textureHandle = framePass ? framePass->GetRenderTexture(aovToken) : HgiTextureHandle();

    if (!renderBuffer || !textureHandle)
    {
        TF_CODING_ERROR("The frame pass is not initialized for %s.", aovToken.GetText());
        return {};
    }

    std::shared_ptr<HdRenderBuffer> res(new RenderBufferProxy(renderBuffer, textureHandle));
    return res;
}

} // namespace hvt