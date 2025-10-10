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

#include <hvt/tasks/aovInputTask.h>

#include <hvt/tasks/resources.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

#include "pxr/imaging/hdx/hgiConversions.h"

#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdSt/renderBuffer.h"
#include "pxr/imaging/hdx/tokens.h"
#include "pxr/imaging/hgi/blitCmds.h"
#include "pxr/imaging/hgi/blitCmdsOps.h"
#include "pxr/imaging/hgi/tokens.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

AovInputTask::AovInputTask(HdSceneDelegate* /* delegate */, SdfPath const& id) :
    HdxTask(id), _converged(false), _aovBuffer(nullptr), _depthBuffer(nullptr), _neyeBuffer(nullptr)
{
}

AovInputTask::~AovInputTask()
{
    if (_aovTexture)
    {
        _GetHgi()->DestroyTexture(&_aovTexture);
    }
    if (_aovTextureIntermediate)
    {
        _GetHgi()->DestroyTexture(&_aovTextureIntermediate);
    }
    if (_depthTexture)
    {
        _GetHgi()->DestroyTexture(&_depthTexture);
    }
    if (_neyeTexture)
    {
        _GetHgi()->DestroyTexture(&_neyeTexture);
    }
}

bool AovInputTask::IsConverged() const
{
    return _converged;
}

void AovInputTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        AovInputTaskParams params;

        if (_GetTaskParams(delegate, &params))
        {
            _aovBuffer   = params.aovBuffer;
            _depthBuffer = params.depthBuffer;
            _neyeBuffer  = params.neyeBuffer;
        }
    }
    *dirtyBits = HdChangeTracker::Clean;
}

void AovInputTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* /* renderIndex */)
{
    // Wrap one HdEngine::Execute frame with Hgi StartFrame and EndFrame.
    // EndFrame is currently called in the PresentTask.
    // This is important for Hgi garbage collection to run.
    _GetHgi()->StartFrame();

    // Create / update the texture that will be used to ping-pong between color
    // targets in tasks that wish to read from and write to the color target.
    if (_aovBuffer)
    {
        _UpdateIntermediateTexture(_aovTextureIntermediate, _aovBuffer);
    }
}

void AovInputTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // This task requires an aov buffer to have been set and is immediately
    // converged if there is no aov buffer.
    if (!_aovBuffer)
    {
        _converged = true;
        return;
    }

    // Check converged state of buffer(s)
    _converged = _aovBuffer->IsConverged();
    if (_depthBuffer)
    {
        _converged = _converged && _depthBuffer->IsConverged();
    }
    if (_neyeBuffer)
    {
        _converged = _converged && _neyeBuffer->IsConverged();
    }

    // Resolve the buffers before we read them.
    _aovBuffer->Resolve();
    if (_depthBuffer)
    {
        _depthBuffer->Resolve();
    }
    if (_neyeBuffer)
    {
        _neyeBuffer->Resolve();
    }

    static const TfToken msaaToken("colorMSAA");
    static const TfToken depthMsaaToken("depthMSAA");

    // Start by clearing aov texture handles from task context.
    // These are last frames textures and we may be visualizing different aovs.
    ctx->erase(HdAovTokens->color);
    ctx->erase(HdAovTokens->depth);
    ctx->erase(HdxAovTokens->colorIntermediate);
    ctx->erase(HdAovTokens->Neye);
    ctx->erase(msaaToken);
    ctx->erase(depthMsaaToken);

    // If the aov is already backed by a HgiTexture we skip creating a new
    // GPU HgiTexture for it and place it directly on the shared task context
    // for consecutive tasks to find and operate on.
    // The lifetime management of that HgiTexture remains with the aov.

    bool hgiHandleProvidedByAov = false;
    const bool mulSmp           = false;

    VtValue aov = _aovBuffer->GetResource(mulSmp);
    if (aov.IsHolding<HgiTextureHandle>())
    {
        hgiHandleProvidedByAov     = true;
        (*ctx)[HdAovTokens->color] = aov;
        (*ctx)[msaaToken] = _aovBuffer->IsMultiSampled() ? _aovBuffer->GetResource(true) : aov;
    }

    (*ctx)[HdxAovTokens->colorIntermediate] = VtValue(_aovTextureIntermediate);

    if (_depthBuffer)
    {
        VtValue depth = _depthBuffer->GetResource(mulSmp);
        if (depth.IsHolding<HgiTextureHandle>())
        {
            (*ctx)[HdAovTokens->depth] = depth;
            (*ctx)[depthMsaaToken] = _depthBuffer->IsMultiSampled() ? _depthBuffer->GetResource(true) : depth;
        }
    }

    if (_neyeBuffer)
    {
        VtValue neye = _neyeBuffer->GetResource(mulSmp);
        if (neye.IsHolding<HgiTextureHandle>())
        {
            (*ctx)[HdAovTokens->Neye] = neye;
        }
    }

    if (hgiHandleProvidedByAov)
    {
        return;
    }

    // If the aov is not backed by a HgiTexture (e.g. RenderMan, Embree) we
    // convert the aov pixel data to a HgiTexture and place that new texture
    // in the shared task context.
    // The lifetime of this new HgiTexture is managed by this task.

    _UpdateTexture(ctx, _aovTexture, _aovBuffer, HgiTextureUsageBitsColorTarget);
    if (_aovTexture)
    {
        (*ctx)[HdAovTokens->color] = VtValue(_aovTexture);
    }

    if (_depthBuffer)
    {
        _UpdateTexture(ctx, _depthTexture, _depthBuffer, HgiTextureUsageBitsDepthTarget);
        if (_depthTexture)
        {
            (*ctx)[HdAovTokens->depth] = VtValue(_depthTexture);
        }
    }
    if (_neyeBuffer)
    {
        _UpdateTexture(ctx, _neyeTexture, _neyeBuffer, HgiTextureUsageBitsShaderRead);
        if (_neyeTexture)
        {
            (*ctx)[HdAovTokens->Neye] = VtValue(_neyeTexture);
        }
    }
}

void AovInputTask::_UpdateTexture(HdTaskContext* /* ctx */, HgiTextureHandle& texture,
    HdRenderBuffer* buffer, HgiTextureUsageBits usage)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    GfVec3i dim(buffer->GetWidth(), buffer->GetHeight(), buffer->GetDepth());

    HgiFormat bufFormat  = HdxHgiConversions::GetHgiFormat(buffer->GetFormat());
    size_t pixelByteSize = HdDataSizeOfFormat(buffer->GetFormat());
    size_t dataByteSize  = dim[0] * dim[1] * dim[2] * pixelByteSize;

    // Update the existing texture if specs are compatible. This is more
    // efficient than re-creating, because the underlying framebuffer that
    // had the old texture attached would also need to be re-created.
    if (texture && texture->GetDescriptor().dimensions == dim &&
        texture->GetDescriptor().format == bufFormat)
    {
        const void* pixelData = buffer->Map();
        HgiTextureCpuToGpuOp copyOp;
        copyOp.bufferByteSize         = dataByteSize;
        copyOp.cpuSourceBuffer        = pixelData;
        copyOp.gpuDestinationTexture  = texture;
        HgiBlitCmdsUniquePtr blitCmds = _GetHgi()->CreateBlitCmds();
        blitCmds->PushDebugGroup("Upload CPU texels");
        blitCmds->CopyTextureCpuToGpu(copyOp);
        blitCmds->PopDebugGroup();
        _GetHgi()->SubmitCmds(blitCmds.get());
        buffer->Unmap();
    }
    else
    {
        // Destroy old texture
        if (texture)
        {
            _GetHgi()->DestroyTexture(&texture);
        }
        // Create a new texture
        HgiTextureDesc texDesc;
        texDesc.debugName  = "AovInput Texture";
        texDesc.dimensions = dim;

        const void* pixelData = buffer->Map();

        texDesc.format         = bufFormat;
        texDesc.initialData    = pixelData;
        texDesc.layerCount     = 1;
        texDesc.mipLevels      = 1;
        texDesc.pixelsByteSize = dataByteSize;
        texDesc.sampleCount    = HgiSampleCount1;
        texDesc.usage          = usage | HgiTextureUsageBitsShaderRead;

        texture = _GetHgi()->CreateTexture(texDesc);

        buffer->Unmap();
    }
}

void AovInputTask::_UpdateIntermediateTexture(HgiTextureHandle& texture, HdRenderBuffer* buffer)
{
    GfVec3i dim(buffer->GetWidth(), buffer->GetHeight(), buffer->GetDepth());
    HgiFormat hgiFormat = HdxHgiConversions::GetHgiFormat(buffer->GetFormat());

    if (texture)
    {
        HgiTextureDesc const& desc = texture->GetDescriptor();
        if (dim != desc.dimensions || hgiFormat != desc.format)
        {
            _GetHgi()->DestroyTexture(&texture);
        }
    }

    if (!texture)
    {

        HgiTextureDesc texDesc;
        texDesc.debugName  = "AovInput Intermediate Texture";
        texDesc.dimensions = dim;

        texDesc.format      = hgiFormat;
        texDesc.layerCount  = 1;
        texDesc.mipLevels   = 1;
        texDesc.sampleCount = HgiSampleCount1;
        texDesc.usage       = HgiTextureUsageBitsColorTarget | HgiTextureUsageBitsShaderRead;

        texture = _GetHgi()->CreateTexture(texDesc);
    }
}

// --------------------------------------------------------------------------- //
// VtValue Requirements
// --------------------------------------------------------------------------- //

std::ostream& operator<<(std::ostream& out, const HdRenderBuffer* pBuffer)
{
    if (pBuffer)
    {
        out << pBuffer->GetWidth() << "x" << pBuffer->GetHeight() << "x" << pBuffer->GetDepth()
            << " format:" << pBuffer->GetFormat()
            << " IsMultiSampled:" << pBuffer->IsMultiSampled();
    }
    else
    {
        out << "nullptr";
    }

    return out;
}

std::ostream& operator<<(std::ostream& out, const AovInputTaskParams& pv)
{
    out << "AovInputTask Params: (...) " << pv.aovBufferPath << "(" << pv.aovBuffer << ") "
        << pv.depthBufferPath << "(" << pv.depthBuffer << ") ";

    return out;
}

bool operator==(const AovInputTaskParams& lhs, const AovInputTaskParams& rhs)
{
    return lhs.aovBufferPath == rhs.aovBufferPath && lhs.aovBuffer == rhs.aovBuffer &&
        lhs.depthBufferPath == rhs.depthBufferPath && lhs.depthBuffer == rhs.depthBuffer;
}

bool operator!=(const AovInputTaskParams& lhs, const AovInputTaskParams& rhs)
{
    return !(lhs == rhs);
}

} // namespace HVT_NS
