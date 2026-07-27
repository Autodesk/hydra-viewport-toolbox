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

#include <hvt/tasks/outline/outlineManager.h>

#include "outlineTextureNames.h"

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskManager.h>
#include <hvt/tasks/outline/outlineMaskTask.h>
#include <hvt/tasks/outline/outlineOverlayTask.h>
#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace HVT_NS::Outline
{

PXR_NAMESPACE_USING_DIRECTIVE;

namespace
{

constexpr char kBasePrefix[]    = "Base";
constexpr char kOverlayPrefix[] = "Overlay";
constexpr char kDefaultPrefix[] = "Default";

PXR_NS::HdRprimCollection _MakeOutlineCollection(PXR_NS::SdfPathVector const& roots)
{
    PXR_NS::HdRprimCollection collection(PXR_NS::HdTokens->geometry,
        PXR_NS::HdReprSelector(PXR_NS::HdReprTokens->smoothHull),
        PXR_NS::SdfPath::AbsoluteRootPath(),
        /*forcedRepr=*/false);
    collection.SetRootPaths(roots);
    return collection;
}

// Per-task cache of the derived HdRprimCollection. The collection depends only on the
// path inputs, so it is rebuilt lazily and reused across commits whose inputs are unchanged
// (keyed against SharedState::inputsGeneration, which SetInputs bumps on every real change).
struct CollectionCache
{
    uint64_t generation = std::numeric_limits<uint64_t>::max();
    PXR_NS::HdRprimCollection collection;
};

void _GetViewportParams(
    PXR_NS::GfVec2i& size,
    PXR_NS::SdfPath& camera,
    PXR_NS::CameraUtilFraming& framing,
    std::optional<PXR_NS::CameraUtilConformWindowPolicy>& overrideWindowPolicy,
    FramePass const* framePass)
{
    if (framePass == nullptr)
    {
        return;
    }
    auto const& pp       = framePass->params();
    size                 = pp.renderBufferSize;
    camera               = pp.renderParams.camera;
    framing              = pp.renderParams.framing;
    overrideWindowPolicy = pp.renderParams.overrideWindowPolicy;
}

} // namespace

class OutlineManager::SharedState
{
public:
    OutlineStyle style;
    OutlineInputs inputs;

    FramePass* framePass = nullptr;

    PXR_NS::SdfPath basePrimIdsTaskId;
    PXR_NS::SdfPath overlayPrimIdsTaskId;
    PXR_NS::SdfPath defaultPrimIdsTaskId;
    PXR_NS::SdfPath maskTaskId;
    PXR_NS::SdfPath overlayTaskId;

    // Bumped by SetInputs() whenever the inputs actually change; the per-task
    // CollectionCache compares against this to decide when to rebuild.
    uint64_t inputsGeneration = 0;

    // Running sum of the per-query input-path count across every SetInputs() call (hits and
    // misses alike). Kept exact so CacheStats::avgInputPathCount is a single integer division
    // at the end rather than a compounding running mean.
    size_t collectionSizeSum = 0;

    // Accumulated debug read-back counters; see GetCacheStats().
    CacheStats stats;
};

OutlineManager::OutlineManager() : _state(std::make_shared<SharedState>()) {}

OutlineManager::~OutlineManager() = default;

void OutlineManager::Install(
    FramePass& framePass, 
    PXR_NS::SdfPath const& atPos, 
    TaskManager::InsertionOrder order)
{
    // Install policy (reverse insertion):
    //   1. Anchor the overlay task at (atPos, order).
    //   2. Insert mask before overlay.
    //   3. Insert each prim-IDs task before mask.
    // Execution order: prim-IDs -> mask -> overlay.

    if (_state->framePass != nullptr)
    {
        TF_WARN("hvt::OutlineManager::Install called more than once; ignoring.");
        return;
    }

    // With no anchor there is nothing to sit before/after, so insertBefore/insertAfter degrade
    // to insertAtEnd (the documented Install() contract). Normalize here: the TaskManager
    // positions relative to atPos only when it is non-empty, and asking it to insertAfter an
    // empty anchor would advance past end().
    if (atPos.IsEmpty())
    {
        order = TaskManager::InsertionOrder::insertAtEnd;
    }

    auto* taskMgr    = framePass.GetTaskManager().get();
    SharedState* state = _state.get();
    state->framePass   = &framePass;

    // The task commit callbacks capture this weak_ptr and lock() it on each commit (matching
    // the built-in HVT tasks' weak-pointer pattern). If this OutlineManager is destroyed while
    // its tasks are still installed in the frame pass, the lock() fails and the commit safely
    // no-ops instead of dereferencing freed state, so no explicit task removal is required.
    std::weak_ptr<SharedState> stateWeak = _state;

    // Install Overlay Task
    {
        OutlineOverlayTaskParams params;
        params.enabled       = false;
        params.blurMode      = state->style.blurMode;
        params.blurIntensity = state->style.blurIntensity;

        auto fnCommit = [stateWeak](TaskManager::GetTaskValueFn const& fnGet,
                            TaskManager::SetTaskValueFn const& fnSet)
        {
            auto state = stateWeak.lock();
            if (!state)
            {
                return;
            }

            auto params = fnGet(PXR_NS::HdTokens->params).Get<OutlineOverlayTaskParams>();

            bool const hasSelected = !state->inputs.selectedPaths.empty();
            bool const hasHover    = !state->inputs.hoverPaths.empty();
            bool const hasOverlay  = !state->inputs.overlayPaths.empty();

            params.enabled =
                hasSelected || hasHover || hasOverlay || state->style.enableDefaultOutlines;
            params.blurMode      = state->style.blurMode;
            params.blurIntensity = state->style.blurIntensity;

            if (state->framePass != nullptr)
            {
                params.size = state->framePass->params().renderBufferSize;
            }

            fnSet(PXR_NS::HdTokens->params, PXR_NS::VtValue(params));
        };

        state->overlayTaskId = taskMgr->AddTask<OutlineOverlayTask>(
            OutlineOverlayTask::GetToken(), params, fnCommit, atPos, order);
    }

    // Install Mask Task
    {
        OutlineMaskTaskParams params;
        params.basePrimIdsTexture    = OutlinePrimIdsTextureName(kBasePrefix);
        params.baseDepthTexture      = OutlineDepthTextureName(kBasePrefix);
        params.overlayPrimIdsTexture = OutlinePrimIdsTextureName(kOverlayPrefix);
        params.overlayDepthTexture   = OutlineDepthTextureName(kOverlayPrefix);
        params.defaultPrimIdsTexture = OutlinePrimIdsTextureName(kDefaultPrefix);
        params.defaultDepthTexture   = OutlineDepthTextureName(kDefaultPrefix);

        auto fnCommit = [stateWeak](TaskManager::GetTaskValueFn const& fnGet,
                            TaskManager::SetTaskValueFn const& fnSet)
        {
            auto state = stateWeak.lock();
            if (!state)
            {
                return;
            }

            auto params = fnGet(PXR_NS::HdTokens->params).Get<OutlineMaskTaskParams>();

            const bool hasSelected = !state->inputs.selectedPaths.empty();
            const bool hasHover    = !state->inputs.hoverPaths.empty();
            const bool hasOverlay  = !state->inputs.overlayPaths.empty();
            const bool useDefault  = state->style.enableDefaultOutlines;

            params.enabled = hasSelected || hasHover || hasOverlay || useDefault;

            if (useDefault)
            {
                params.defaultPrimIdsTexture = OutlinePrimIdsTextureName(kDefaultPrefix);
                params.defaultDepthTexture   = OutlineDepthTextureName(kDefaultPrefix);
            }
            else
            {
                // enableDefaultOutlines is off (a supported, first-class configuration): the
                // default prim-ID / depth inputs are aliased to the base textures so the mask
                // shader has valid handles. This is intentional, so it is silent.
                params.defaultPrimIdsTexture = OutlinePrimIdsTextureName(kBasePrefix);
                params.defaultDepthTexture   = OutlineDepthTextureName(kBasePrefix);
            }
            if (!hasOverlay)
            {
                params.overlayPrimIdsTexture = OutlinePrimIdsTextureName(kBasePrefix);
                params.overlayDepthTexture   = OutlineDepthTextureName(kBasePrefix);
            }
            else
            {
                params.overlayPrimIdsTexture = OutlinePrimIdsTextureName(kOverlayPrefix);
                params.overlayDepthTexture   = OutlineDepthTextureName(kOverlayPrefix);
            }

            auto& s                     = params.style;
            s.selectedColor             = state->style.selectedColor;
            s.selectedHoverColor        = state->style.selectedHoverColor;
            s.selectionLeadColor        = state->style.selectionLeadColor;
            s.selectionLeadHoverColor   = state->style.selectionLeadHoverColor;
            s.unselectedHoverColor      = state->style.unselectedHoverColor;
            s.overlayColor              = state->style.overlayColor;
            s.overlayHoverColor         = state->style.overlayHoverColor;
            s.defaultColor              = state->style.defaultColor;
            s.softnessStrength          = state->style.softnessStrength;
            s.softnessFalloff           = state->style.softnessFalloff;
            s.hasDistinctOverlay        = hasOverlay ? 1 : 0;
            s.hasDistinctDefault        = useDefault ? 1 : 0;
            s.isHoverSelected           = state->inputs.isHoverSelected ? 1 : 0;

            params.maskVisualizationMode = state->style.maskVisualizationMode;

            // Path lists go straight through.
            params.leadPath     = state->inputs.leadPath;
            params.hoverPaths   = state->inputs.hoverPaths;
            params.overlayPaths = state->inputs.overlayPaths;

            // The lead/hover/overlay ID counts and the integer prim-ID arrays are all resolved
            // by OutlineMaskTask::_Sync(), which has HdRenderIndex access and expands each path
            // to its subtree of Rprim IDs. A single path can map to zero or many prim IDs, so the
            // counts cannot be derived from the bucket sizes here; _Sync() resets and recomputes
            // them from the render index every frame. OutlineManager only passes SdfPath buckets
            // through and leaves the resolved arrays empty for _Sync() to fill.
            params.leadIdValues.clear();
            params.hoverIdValues.clear();
            params.overlayIdValues.clear();

            if (state->framePass != nullptr)
            {
                params.size = state->framePass->params().renderBufferSize;
            }

            fnSet(PXR_NS::HdTokens->params, PXR_NS::VtValue(params));
        };

        state->maskTaskId = taskMgr->AddTask<OutlineMaskTask>(OutlineMaskTask::GetToken(),
            params, fnCommit, state->overlayTaskId, TaskManager::InsertionOrder::insertBefore);
    }

    // Install PrimIds Tasks
    auto installPrimIds = [&](PXR_NS::TfToken const& taskName, char const* prefix,
                              std::function<bool(SharedState const&)> enabledFn,
                              std::function<PXR_NS::HdRprimCollection(SharedState const&)> collectionFn)
    {
        OutlinePrimIdsTaskParams initial;
        initial.bufferPrefix = prefix;
        initial.enabled      = false;

        std::string prefixStr(prefix);
        auto collectionCache = std::make_shared<CollectionCache>();
        auto fnCommit =
            [stateWeak, prefixStr, collectionCache, enabledFn = std::move(enabledFn),
                collectionFn = std::move(collectionFn)](
                TaskManager::GetTaskValueFn const& fnGet, TaskManager::SetTaskValueFn const& fnSet)
        {
            auto state = stateWeak.lock();
            if (!state)
            {
                return;
            }

            auto params         = fnGet(PXR_NS::HdTokens->params).Get<OutlinePrimIdsTaskParams>();
            params.bufferPrefix = prefixStr;
            params.enabled      = enabledFn(*state);
            if (params.enabled)
            {
                // Rebuild the derived collection only when the inputs actually changed;
                // otherwise reuse the cached collection from the previous commit.
                if (collectionCache->generation != state->inputsGeneration)
                {
                    collectionCache->collection = collectionFn(*state);
                    collectionCache->generation = state->inputsGeneration;
                }
                params.collection = collectionCache->collection;
            }
            _GetViewportParams(params.size, params.camera, params.framing,
                params.overrideWindowPolicy, state->framePass);
            fnSet(PXR_NS::HdTokens->params, PXR_NS::VtValue(params));
        };

        return taskMgr->AddTask<OutlinePrimIdsTask>(taskName, initial, fnCommit, state->maskTaskId,
            TaskManager::InsertionOrder::insertBefore);
    };

    state->basePrimIdsTaskId = installPrimIds(
        OutlinePrimIdsTask::GetToken(kBasePrefix), kBasePrefix,
        [](SharedState const& s)
        {
            return !s.inputs.selectedPaths.empty() 
                || !s.inputs.hoverPaths.empty() 
                || !s.inputs.overlayPaths.empty() 
                || s.style.enableDefaultOutlines;
        },
        [](SharedState const& s)
        {
            // Base roots are the selected + hover paths. leadPath is intentionally NOT added
            // here: it is expected to be a subset of selectedPaths (the contract documented on
            // OutlineInputs::leadPath), so it is already rasterized into the base prim-ID
            // texture. If a host ever sets a leadPath that is not in selectedPaths/hoverPaths,
            // its primId would be absent from this texture and the lead outline would not draw.
            PXR_NS::SdfPathVector roots = s.inputs.selectedPaths;
            roots.insert(roots.end(), s.inputs.hoverPaths.begin(), s.inputs.hoverPaths.end());
            // Deduplicate: a hovered path already in the selection (the isHoverSelected state)
            // appears in both buckets, and SetRootPaths sorts but does not unique, so the shared
            // prim would otherwise be gathered/rasterized twice.
            std::sort(roots.begin(), roots.end());
            roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
            return _MakeOutlineCollection(roots);
        });

    state->overlayPrimIdsTaskId = installPrimIds(
        OutlinePrimIdsTask::GetToken(kOverlayPrefix), kOverlayPrefix,
        [](SharedState const& s) { return !s.inputs.overlayPaths.empty(); },
        [](SharedState const& s) { return _MakeOutlineCollection(s.inputs.overlayPaths); });

    state->defaultPrimIdsTaskId = installPrimIds(
        OutlinePrimIdsTask::GetToken(kDefaultPrefix), kDefaultPrefix,
        [](SharedState const& s) { return s.style.enableDefaultOutlines; },
        [](SharedState const& s)
        {
            auto c = _MakeOutlineCollection(
                PXR_NS::SdfPathVector{ PXR_NS::SdfPath::AbsoluteRootPath() });
            if (!s.inputs.excludePaths.empty())
            {
                c.SetExcludePaths(s.inputs.excludePaths);
            }
            return c;
        });
}

void OutlineManager::SetInputs(OutlineInputs inputs)
{
    auto& cur = _state->inputs;

    // Track basic cache stats: a "hit" is a no-op SetInputs, a "miss" triggers
    // re-evaluation of derived collections on the next commit. Compare through
    // OutlineInputs::operator== so a newly-added field is covered automatically.
    const bool unchanged = (cur == inputs);

    // Size stats cover every query (hits and misses): on a hit the inputs are unchanged, so
    // their size still contributes to the running average / maximum.
    const size_t totalSize = inputs.selectedPaths.size() + inputs.hoverPaths.size()
                           + inputs.overlayPaths.size() + (inputs.leadPath.IsEmpty() ? 0 : 1);

    _state->stats.totalQueries++;
    _state->collectionSizeSum += totalSize;
    if (totalSize > _state->stats.maxInputPathCount)
    {
        _state->stats.maxInputPathCount = totalSize;
    }
    _state->stats.avgInputPathCount = _state->collectionSizeSum / _state->stats.totalQueries;

    if (unchanged)
    {
        _state->stats.hits++;
        return;
    }
    _state->stats.misses++;

    _state->inputs = std::move(inputs);

    // Invalidate the per-task derived-collection caches; they rebuild on the next commit.
    _state->inputsGeneration++;
}

void OutlineManager::SetStyle(OutlineStyle style)
{
    if (_state->style == style)
    {
        return;
    }
    _state->style = std::move(style);
}

OutlineManager::CacheStats OutlineManager::GetCacheStats() const
{
    return _state->stats;
}

} // namespace HVT_NS::Outline
