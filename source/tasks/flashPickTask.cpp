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

#include <hvt/tasks/flashPickTask.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
// clang-format on

#include <pxr/base/vt/array.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/tokens.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
// clang-format on

#include <algorithm>
#include <cmath>


PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

// -------------------------------------------------------------------------- //
// Renderer detection
// -------------------------------------------------------------------------- //
static bool _IsHdFlashRenderer(HdRenderDelegate* renderDelegate)
{
    return renderDelegate && renderDelegate->GetRendererDisplayName() == "HdFlash";
}

// -------------------------------------------------------------------------- //
// FlashPickTask
// -------------------------------------------------------------------------- //
FlashPickTask::FlashPickTask(HdSceneDelegate* /*delegate*/, SdfPath const& id) : HdTask(id) {}

FlashPickTask::~FlashPickTask()
{
    _CleanupIdBuffers();
}

const TfToken& FlashPickTask::GetToken()
{
    static TfToken const token { "FlashPickTask" };
    return token;
}

// -------------------------------------------------------------------------- //
// ID buffer management
// -------------------------------------------------------------------------- //
void FlashPickTask::_CreateIdBuffers()
{
    if (!_index)
        return;

    HdRenderDelegate* rd = _index->GetRenderDelegate();

    auto createBuffer = [&](const char* name) -> HdRenderBuffer*
    {
        SdfPath const bufId = SdfPath(TfToken(name));
        HdBprim* bprim      = rd->CreateBprim(HdPrimTypeTokens->renderBuffer, bufId);
        return dynamic_cast<HdRenderBuffer*>(bprim);
    };

    _primIdBuffer     = createBuffer("flashPick_primId");
    _instanceIdBuffer = createBuffer("flashPick_instanceId");
    _elementIdBuffer  = createBuffer("flashPick_elementId");
    _depthBuffer      = createBuffer("flashPick_depth");
}

void FlashPickTask::_CleanupIdBuffers()
{
    if (!_index)
        return;

    HdRenderDelegate* rd = _index->GetRenderDelegate();

    auto destroy = [&](HdRenderBuffer*& buf)
    {
        if (buf)
        {
            rd->DestroyBprim(buf);
            buf = nullptr;
        }
    };

    destroy(_primIdBuffer);
    destroy(_instanceIdBuffer);
    destroy(_elementIdBuffer);
    destroy(_depthBuffer);

    _pickAovBindings.clear();
    _bufferWidth  = 0;
    _bufferHeight = 0;
}

void FlashPickTask::_ResizeBuffers(int32_t width, int32_t height)
{
    if (width == _bufferWidth && height == _bufferHeight && _primIdBuffer)
        return;

    _bufferWidth  = width;
    _bufferHeight = height;

    HdRenderDelegate* rd = _index->GetRenderDelegate();

    // Allocate ID buffers as Int32 (single sample -- pick doesn't use MSAA)
    auto allocateInt32 = [&](HdRenderBuffer* buf)
    {
        if (buf)
        {
            HdAovDescriptor desc = rd->GetDefaultAovDescriptor(HdAovTokens->primId);
            buf->Allocate(GfVec3i(width, height, 1), desc.format, /*multiSampled=*/false);
        }
    };

    allocateInt32(_primIdBuffer);
    allocateInt32(_instanceIdBuffer);
    allocateInt32(_elementIdBuffer);

    // Depth buffer
    if (_depthBuffer)
    {
        HdAovDescriptor depthDesc = rd->GetDefaultAovDescriptor(HdAovTokens->depth);
        _depthBuffer->Allocate(GfVec3i(width, height, 1), depthDesc.format, /*multiSampled=*/false);
    }

    // Build AOV bindings (ID-only, no color)
    _pickAovBindings.clear();

    auto addBinding = [&](TfToken const& aovName, HdRenderBuffer* buf, VtValue clearVal)
    {
        HdRenderPassAovBinding binding;
        binding.aovName        = aovName;
        binding.renderBufferId = SdfPath(aovName);
        binding.renderBuffer   = buf;
        binding.clearValue     = clearVal;
        _pickAovBindings.push_back(binding);
    };

    addBinding(HdAovTokens->primId, _primIdBuffer, VtValue(-1));
    addBinding(HdAovTokens->instanceId, _instanceIdBuffer, VtValue(-1));
    addBinding(HdAovTokens->elementId, _elementIdBuffer, VtValue(-1));
    addBinding(HdAovTokens->depth, _depthBuffer, VtValue(1.0f));
}

// -------------------------------------------------------------------------- //
// Projection matrix computation (mirrors HdxPickFromRenderBufferTask)
// -------------------------------------------------------------------------- //
GfMatrix4d FlashPickTask::_ComputeProjectionMatrix() const
{
    if (!_camera)
        return GfMatrix4d(1);

    if (_params.framing.IsValid())
    {
        const CameraUtilConformWindowPolicy policy = _params.overrideWindowPolicy
            ? *_params.overrideWindowPolicy
            : _camera->GetWindowPolicy();
        return _params.framing.ApplyToProjectionMatrix(_camera->ComputeProjectionMatrix(), policy);
    }
    else
    {
        const double aspect =
            (_bufferHeight != 0) ? static_cast<double>(_bufferWidth) / _bufferHeight : 1.0;
        return CameraUtilConformedWindow(
            _camera->ComputeProjectionMatrix(), _camera->GetWindowPolicy(), aspect);
    }
}

// -------------------------------------------------------------------------- //
// HdTask interface
// -------------------------------------------------------------------------- //
void FlashPickTask::Sync(HdSceneDelegate* delegate, HdTaskContext* /*ctx*/, HdDirtyBits* dirtyBits)
{
    if (*dirtyBits & HdChangeTracker::DirtyParams)
    {
        FlashPickTaskParams params;
        if (delegate->Get(GetId(), HdTokens->params).IsHolding<FlashPickTaskParams>())
        {
            params = delegate->Get(GetId(), HdTokens->params).UncheckedGet<FlashPickTaskParams>();
        }
        _params = params;
    }

    if (*dirtyBits & HdChangeTracker::DirtyRenderTags)
    {
        VtValue val = delegate->Get(GetId(), HdTokens->renderTags);
        if (val.IsHolding<TfTokenVector>())
        {
            _renderTags = val.UncheckedGet<TfTokenVector>();
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}

void FlashPickTask::Prepare(HdTaskContext* /*ctx*/, HdRenderIndex* renderIndex)
{
    _index = renderIndex;

    HdRenderDelegate* rd = renderIndex->GetRenderDelegate();
    _isFlashRenderer     = _IsHdFlashRenderer(rd);

    if (!_isFlashRenderer)
        return;

    // Cache the camera pointer for _ComputeProjectionMatrix
    _camera = static_cast<const HdCamera*>(
        renderIndex->GetSprim(HdPrimTypeTokens->camera, _params.cameraId));

    // Create ID buffers if not yet initialized
    if (!_primIdBuffer)
    {
        _CreateIdBuffers();
    }

    // Create the render pass if needed
    if (!_renderPass)
    {
        HdRprimCollection col(HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
        _renderPass      = rd->CreateRenderPass(renderIndex, col);
        _renderPassState = rd->CreateRenderPassState();
    }
}

void FlashPickTask::Execute(HdTaskContext* ctx)
{
    if (!_isFlashRenderer || !_index)
        return;

    // Only execute when pick params are present in the task context
    auto it = ctx->find(HdxPickTokens->pickParams);
    if (it == ctx->end())
        return;

    HdxPickTaskContextParams pickParams = it->second.UncheckedGet<HdxPickTaskContextParams>();

    if (!pickParams.outHits)
        return;

    // Use the camera cached during Prepare
    const HdCamera* camera = _camera;
    if (!camera)
        return;

    // Determine buffer resolution from framing or use a default
    int32_t width  = 128;
    int32_t height = 128;
    if (_params.framing.IsValid())
    {
        const GfRect2i& dw = _params.framing.dataWindow;
        width              = dw.GetWidth();
        height             = dw.GetHeight();
    }

    // Resize/allocate buffers to match resolution
    _ResizeBuffers(width, height);

    // Configure render pass state
    _renderPassState->SetAovBindings(_pickAovBindings);

    // Set camera and framing on the render pass state
    _renderPassState->SetCamera(camera);
    if (_params.framing.IsValid())
    {
        _renderPassState->SetFraming(_params.framing);
        if (_params.overrideWindowPolicy.has_value())
        {
            _renderPassState->SetOverrideWindowPolicy(_params.overrideWindowPolicy);
        }
    }
    else
    {
        _renderPassState->SetViewport(GfVec4i(0, 0, width, height));
    }

    // Set the collection for picking
    if (pickParams.collection.GetName() != TfToken())
    {
        _renderPass->SetRprimCollection(pickParams.collection);
    }
    else
    {
        // Default: all geometry
        HdRprimCollection col(HdTokens->geometry, HdReprSelector(HdReprTokens->hull));
        _renderPass->SetRprimCollection(col);
    }

    // Execute the render pass to fill ID buffers
    _renderPass->Execute(_renderPassState, GetRenderTags());

    // Resolve the render buffers so data is available for readback
    if (_primIdBuffer)
        _primIdBuffer->Resolve();
    if (_instanceIdBuffer)
        _instanceIdBuffer->Resolve();
    if (_elementIdBuffer)
        _elementIdBuffer->Resolve();
    if (_depthBuffer)
        _depthBuffer->Resolve();

    // Map buffers for readback
    int const* primIds = static_cast<int const*>(_primIdBuffer ? _primIdBuffer->Map() : nullptr);
    int const* instanceIds =
        static_cast<int const*>(_instanceIdBuffer ? _instanceIdBuffer->Map() : nullptr);
    int const* elementIds =
        static_cast<int const*>(_elementIdBuffer ? _elementIdBuffer->Map() : nullptr);
    float const* depths = static_cast<float const*>(_depthBuffer ? _depthBuffer->Map() : nullptr);

    if (!primIds)
    {
        // Buffer readback failed
        return;
    }

    // Compute the view and projection matrices that were used to render the ID
    // buffers (i.e. the main camera, NOT the pick frustum).
    const GfMatrix4d renderView = camera->GetTransform().GetInverse();
    const GfMatrix4d renderProj = _ComputeProjectionMatrix();

    // Project the pick frustum near-plane corners from pick NDC space into
    // render-buffer pixel coordinates.  This is the same logic that
    // HdxPickFromRenderBufferTask uses to compute its sub-region.
    GfMatrix4d renderBufferXf;
    renderBufferXf.SetScale(GfVec3d(0.5 * width, 0.5 * height, 1));
    renderBufferXf.SetTranslateOnly(GfVec3d(0.5 * width, 0.5 * height, 0));

    GfMatrix4d pickNdcToRenderBuffer =
        (pickParams.viewMatrix * pickParams.projectionMatrix).GetInverse() * renderView *
        renderProj * renderBufferXf;

    GfVec3d corner0 = pickNdcToRenderBuffer.Transform(GfVec3d(-1, -1, -1));
    GfVec3d corner1 = pickNdcToRenderBuffer.Transform(GfVec3d(1, 1, -1));
    GfVec2d pickMin(std::min(corner0[0], corner1[0]), std::min(corner0[1], corner1[1]));
    GfVec2d pickMax(std::max(corner0[0], corner1[0]), std::max(corner0[1], corner1[1]));
    // Round away from center so we don't miss relevant pixels.
    pickMin = GfVec2d(floor(pickMin[0]), floor(pickMin[1]));
    pickMax = GfVec2d(ceil(pickMax[0]), ceil(pickMax[1]));
    GfVec4i subRect(static_cast<int>(pickMin[0]), static_cast<int>(pickMin[1]),
        static_cast<int>(pickMax[0] - pickMin[0]), static_cast<int>(pickMax[1] - pickMin[1]));

    // Resolve hits using Flash's primId-to-path map (bypasses HdRenderIndex).
    // Resolve code copied from HdxPickResult.
    HdRenderDelegate* rd = _index->GetRenderDelegate();
    VtValue pathMapVal   = rd->GetRenderSetting(TfToken("hdFlash:primIdToPath"));
    if (pathMapVal.IsHolding<VtArray<SdfPath>>())
    {
        const VtArray<SdfPath>& pathMap = pathMapVal.UncheckedGet<VtArray<SdfPath>>();
        const GfMatrix4d ndcToWorld     = (renderView * renderProj).GetInverse();
        const float depthRangeMin       = 0.0f;
        const float depthRangeMax       = 1.0f;

        auto resolveHit = [&](int x, int y, int i, HdxPickHit& hit) -> bool
        {
            const int pid = primIds[i];
            // Make sure the pid is valid and in range of the path map.
            if (pid < 0 || static_cast<size_t>(pid) >= pathMap.size())
                return false;

            const SdfPath& path = pathMap[pid];
            if (path.IsEmpty())
                return false;
            hit.objectId      = path;
            hit.delegateId    = SdfPath();
            hit.instancerId   = SdfPath();
            hit.instanceIndex = instanceIds ? instanceIds[i] : -1;
            hit.elementIndex  = elementIds ? elementIds[i] : -1;
            hit.edgeIndex     = -1;
            hit.pointIndex    = -1;

            float z  = depths ? depths[i] : 0.0f;
            float nz = (z - depthRangeMin) / (depthRangeMax - depthRangeMin);
            GfVec3d ndcHit((static_cast<double>(x) / width) * 2.0 - 1.0,
                (static_cast<double>(y) / height) * 2.0 - 1.0, nz * 2.0 - 1.0);
            hit.worldSpaceHitPoint  = ndcToWorld.Transform(ndcHit);
            hit.worldSpaceHitNormal = GfVec3f(0);
            hit.normalizedDepth     = nz;
            return true;
        };

        auto isValidHit = [&](int i) -> bool { return primIds[i] != -1; };

        HdxPickHitVector* outHits = pickParams.outHits;

        if (pickParams.resolveMode == HdxPickTokens->resolveNearestToCamera)
        {
            int bestIdx = -1;
            int bestX = 0, bestY = 0;
            float bestZ = std::numeric_limits<float>::max();
            if (depths)
            {
                for (int y = subRect[1]; y < subRect[1] + subRect[3]; ++y)
                {
                    for (int x = subRect[0]; x < subRect[0] + subRect[2]; ++x)
                    {
                        int i = y * width + x;
                        if (isValidHit(i) && depths[i] < bestZ)
                        {
                            bestX   = x;
                            bestY   = y;
                            bestZ   = depths[i];
                            bestIdx = i;
                        }
                    }
                }
            }
            if (bestIdx >= 0)
            {
                HdxPickHit hit;
                if (resolveHit(bestX, bestY, bestIdx, hit))
                    outHits->push_back(hit);
            }
        }
        else if (pickParams.resolveMode == HdxPickTokens->resolveNearestToCenter)
        {
            int rw = subRect[2], rh = subRect[3];
            int midW   = (rw % 2 == 0) ? rw / 2 - 1 : rw / 2;
            int midH   = (rh % 2 == 0) ? rh / 2 - 1 : rh / 2;
            bool found = false;
            for (int w = midW, h = midH; w >= 0 && h >= 0 && !found; w--, h--)
            {
                for (int ww = w; ww < rw - w && !found; ww++)
                {
                    for (int hh = h; hh < rh - h && !found; hh++)
                    {
                        int x = ww + subRect[0];
                        int y = hh + subRect[1];
                        int i = y * width + x;
                        if (isValidHit(i))
                        {
                            HdxPickHit hit;
                            if (resolveHit(x, y, i, hit))
                            {
                                outHits->push_back(hit);
                                found = true;
                            }
                        }
                        if (!(ww == w || ww == rw - w - 1) && hh == h)
                            hh = std::max(hh, rh - h - 2);
                    }
                }
            }
        }
        else if (pickParams.resolveMode == HdxPickTokens->resolveAll)
        {
            for (int y = subRect[1]; y < subRect[1] + subRect[3]; ++y)
            {
                for (int x = subRect[0]; x < subRect[0] + subRect[2]; ++x)
                {
                    int i = y * width + x;
                    if (!isValidHit(i))
                        continue;
                    HdxPickHit hit;
                    if (resolveHit(x, y, i, hit))
                        outHits->push_back(hit);
                }
            }
        }
        else // resolveUnique
        {
            std::unordered_map<int, std::pair<int, GfVec2i>> bestByPrimId;
            for (int y = subRect[1]; y < subRect[1] + subRect[3]; ++y)
            {
                for (int x = subRect[0]; x < subRect[0] + subRect[2]; ++x)
                {
                    int i = y * width + x;
                    if (!isValidHit(i))
                        continue;
                    int pid = primIds[i];
                    auto it = bestByPrimId.find(pid);
                    if (it == bestByPrimId.end() ||
                        (depths && depths[i] < depths[it->second.first]))
                    {
                        bestByPrimId[pid] = { i, GfVec2i(x, y) };
                    }
                }
            }
            for (auto& [pid, val] : bestByPrimId)
            {
                auto& [idx, xy] = val;
                HdxPickHit hit;
                if (resolveHit(xy[0], xy[1], idx, hit))
                    outHits->push_back(hit);
            }
        }
    }

    // Unmap buffers
    if (_primIdBuffer)
        _primIdBuffer->Unmap();
    if (_instanceIdBuffer)
        _instanceIdBuffer->Unmap();
    if (_elementIdBuffer)
        _elementIdBuffer->Unmap();
    if (_depthBuffer)
        _depthBuffer->Unmap();
}

TfTokenVector const& FlashPickTask::GetRenderTags() const
{
    return _renderTags;
}

} // namespace HVT_NS
