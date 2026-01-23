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

#include <hvt/tasks/clearBufferTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
// clang-format on

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hgi/graphicsCmds.h>
#include <pxr/imaging/hgi/graphicsCmdsDesc.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hd/primTypeIndex.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

// -------------------------------------------------------------------------- //
// ClearBufferTask
// -------------------------------------------------------------------------- //
ClearBufferTask::ClearBufferTask(HdSceneDelegate* /*delegate*/, SdfPath const& id) :
    HdTask(id), _index(nullptr), _hgi(nullptr)
{
}

ClearBufferTask::~ClearBufferTask() {}

void ClearBufferTask::Sync(
    HdSceneDelegate* delegate, HdTaskContext* /*ctx*/, HdDirtyBits* dirtyBits)
{
    // Store the render index
    _index = &(delegate->GetRenderIndex());

    // Get the HGI instance from the render delegate
    if (HdStRenderDelegate* stRenderDelegate =
            dynamic_cast<HdStRenderDelegate*>(_index->GetRenderDelegate()))
    {
        _hgi = stRenderDelegate->GetHgi();
    }

    // Gather params from the scene delegate
    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        _GetTaskParams(delegate, &_params);
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyRenderTags)
    {
        _renderTags = _GetTaskRenderTags(delegate);
    }

    // Clear the dirty bits after sync has completed
    *dirtyBits = HdChangeTracker::Clean;
}

void ClearBufferTask::Prepare(HdTaskContext* /*ctx*/, HdRenderIndex* /*renderIndex*/)
{
    // Textures will be retrieved from AOV bindings in Execute
}

void ClearBufferTask::Execute(HdTaskContext* ctx)
{
    if (!_hgi || !_index)
    {
        return;
    }

    // Get textures by looking up render buffers from the render index using Bprim paths
    HgiTextureHandle colorTexture, depthTexture;
    bool useMultisampling = false;

    // Find color and depth render buffers from AOV bindings
    for (const HdRenderPassAovBinding& binding : _params.aovBindings)
    {
        if (binding.renderBufferId.IsEmpty())
        {
            continue;
        }

        // Get the render buffer from the render index using the Bprim path
        HdRenderBuffer* renderBuffer = static_cast<HdRenderBuffer*>(
            _index->GetBprim(HdPrimTypeTokens->renderBuffer, binding.renderBufferId));

        if (!renderBuffer)
        {
            continue;
        }

        // Check if this buffer uses multisampling
        useMultisampling = renderBuffer->IsMultiSampled();

        if (binding.aovName == HdAovTokens->color)
        {
            // Get the texture resource, requesting MSAA version if multisampling is enabled
            VtValue resource = renderBuffer->GetResource(useMultisampling);
            if (resource.IsHolding<HgiTextureHandle>())
            {
                colorTexture = resource.UncheckedGet<HgiTextureHandle>();
            }
        }
        else if (binding.aovName == HdAovTokens->depth)
        {
            // Get the texture resource, requesting MSAA version if multisampling is enabled
            VtValue resource = renderBuffer->GetResource(useMultisampling);
            if (resource.IsHolding<HgiTextureHandle>())
            {
                depthTexture = resource.UncheckedGet<HgiTextureHandle>();
            }
        }
    }

    // If we couldn't get textures from bindings, try the task context as a fallback
    if (!colorTexture && ctx)
    {
        VtValue colorValue = (*ctx)[HdAovTokens->color];
        if (colorValue.IsHolding<HgiTextureHandle>())
        {
            colorTexture = colorValue.UncheckedGet<HgiTextureHandle>();
        }
    }

    if (!depthTexture && ctx)
    {
        VtValue depthValue = (*ctx)[HdAovTokens->depth];
        if (depthValue.IsHolding<HgiTextureHandle>())
        {
            depthTexture = depthValue.UncheckedGet<HgiTextureHandle>();
        }
    }

    if (!colorTexture || !depthTexture)
    {
        return;
    }

    // Setup color attachment descriptor with clear operation
    HgiAttachmentDesc colorAttachment;
    colorAttachment.blendEnabled = false;
    colorAttachment.loadOp       = HgiAttachmentLoadOpClear;
    colorAttachment.storeOp      = HgiAttachmentStoreOpStore;
    colorAttachment.clearValue   = _params.clearColor;
    colorAttachment.format       = colorTexture->GetDescriptor().format;
    colorAttachment.usage        = colorTexture->GetDescriptor().usage;

    // Setup depth attachment descriptor with clear operation
    HgiAttachmentDesc depthAttachment;
    depthAttachment.loadOp      = HgiAttachmentLoadOpClear;
    depthAttachment.storeOp     = HgiAttachmentStoreOpStore;
    depthAttachment.clearValue  = GfVec4f(_params.clearDepth);
    depthAttachment.format      = depthTexture->GetDescriptor().format;
    depthAttachment.usage       = depthTexture->GetDescriptor().usage;

    // Prepare graphics command descriptor
    HgiGraphicsCmdsDesc gfxDesc;
    gfxDesc.colorAttachmentDescs.push_back(colorAttachment);
    gfxDesc.colorTextures.push_back(colorTexture);
    gfxDesc.depthAttachmentDesc = depthAttachment;
    gfxDesc.depthTexture        = depthTexture;

    // Create graphics commands - the clear happens during command creation
    // due to HgiAttachmentLoadOpClear
    HgiGraphicsCmdsUniquePtr gfxCmds = _hgi->CreateGraphicsCmds(gfxDesc);
    gfxCmds->PushDebugGroup("ClearBuffers");

    // No draw calls needed - the clear operation happens automatically
    // when the render pass begins due to the LoadOpClear setting

    gfxCmds->PopDebugGroup();

    // Submit commands to execute the clear
    _hgi->SubmitCmds(gfxCmds.get());
}

const TfTokenVector& ClearBufferTask::GetRenderTags() const
{
    return _renderTags;
}

const TfToken& ClearBufferTask::GetToken()
{
    static TfToken const token { "clearBufferTask" };
    return token;
}

} // namespace HVT_NS
