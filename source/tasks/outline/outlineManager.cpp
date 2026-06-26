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

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace HVT_NS::Outline
{

PXR_NAMESPACE_USING_DIRECTIVE;

namespace
{

TF_DEFINE_PRIVATE_TOKENS(_outlineTaskTokens,
    ((outlineBasePrimIdsTask, "outlineBasePrimIdsTask"))
    ((outlineOverlayPrimIdsTask, "outlineOverlayPrimIdsTask"))
    ((outlineDefaultPrimIdsTask, "outlineDefaultPrimIdsTask"))
    ((outlineMaskTask, "outlineMaskTask"))
    ((outlineOverlayTask, "outlineOverlayTask")));

constexpr char kBasePrefix[]    = "Base";
constexpr char kOverlayPrefix[] = "Overlay";
constexpr char kDefaultPrefix[] = "Default";

std::string _PrimIdsTextureName(char const* prefix)
{
    return std::string("outline") + prefix + "PrimIdsTexture";
}

std::string _DepthTextureName(char const* prefix)
{
    return std::string("outline") + prefix + "DepthTexture";
}

PXR_NS::HdRprimCollection _MakeOutlineCollection(PXR_NS::SdfPathVector const& roots)
{
    PXR_NS::HdRprimCollection collection(PXR_NS::HdTokens->geometry,
        PXR_NS::HdReprSelector(PXR_NS::HdReprTokens->smoothHull),
        PXR_NS::SdfPath::AbsoluteRootPath(),
        /*forcedRepr=*/false);
    collection.SetRootPaths(roots);
    return collection;
}

void _ApplyViewportParams(PXR_NS::GfVec2i& size, PXR_NS::SdfPath& camera,
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

class OutlineManager::Impl
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

    mutable CacheStats stats;
};

OutlineManager::OutlineManager() : _impl(std::make_unique<Impl>()) {}

OutlineManager::~OutlineManager() = default;

void OutlineManager::Install(
    FramePass& framePass, PXR_NS::SdfPath const& atPos, TaskManager::InsertionOrder order)
{
    // Install policy (reverse insertion):
    //   1. Anchor the overlay task at (atPos, order).
    //   2. Insert mask before overlay.
    //   3. Insert each prim-IDs task before mask.
    // Execution order: prim-IDs → mask → overlay.

    if (_impl->framePass != nullptr)
    {
        TF_WARN("hvt::OutlineManager::Install called more than once; ignoring.");
        return;
    }

    auto* taskMgr   = framePass.GetTaskManager().get();
    Impl* impl      = _impl.get();
    impl->framePass = &framePass;

    // Install Overlay Task
    {
        OutlineOverlayTaskParams params;
        params.enabled       = false;
        params.blurMode      = impl->style.blurMode;
        params.blurIntensity = impl->style.blurIntensity;

        auto fnCommit = [impl](TaskManager::GetTaskValueFn const& fnGet,
                            TaskManager::SetTaskValueFn const& fnSet)
        {
            auto params = fnGet(PXR_NS::HdTokens->params).Get<OutlineOverlayTaskParams>();

            bool const hasSelected = !impl->inputs.selectedPaths.empty();
            bool const hasHover    = !impl->inputs.hoverPaths.empty();
            bool const hasOverlay  = !impl->inputs.overlayPaths.empty();

            params.enabled =
                hasSelected || hasHover || hasOverlay || impl->style.enableDefaultOutlines;
            params.blurMode      = impl->style.blurMode;
            params.blurIntensity = impl->style.blurIntensity;

            if (impl->framePass != nullptr)
            {
                params.size = impl->framePass->params().renderBufferSize;
            }

            fnSet(PXR_NS::HdTokens->params, PXR_NS::VtValue(params));
        };

        impl->overlayTaskId = taskMgr->AddTask<OutlineOverlayTask>(
            _outlineTaskTokens->outlineOverlayTask, params, fnCommit, atPos, order);
    }

    // Install Mask Task
    {
        OutlineMaskTaskParams params;
        params.basePrimIdsTexture    = _PrimIdsTextureName(kBasePrefix);
        params.baseDepthTexture      = _DepthTextureName(kBasePrefix);
        params.overlayPrimIdsTexture = _PrimIdsTextureName(kOverlayPrefix);
        params.overlayDepthTexture   = _DepthTextureName(kOverlayPrefix);
        params.defaultPrimIdsTexture = _PrimIdsTextureName(kDefaultPrefix);
        params.defaultDepthTexture   = _DepthTextureName(kDefaultPrefix);

        auto fnCommit = [impl](TaskManager::GetTaskValueFn const& fnGet,
                            TaskManager::SetTaskValueFn const& fnSet)
        {
            auto params = fnGet(PXR_NS::HdTokens->params).Get<OutlineMaskTaskParams>();

            const bool hasSelected = !impl->inputs.selectedPaths.empty();
            const bool hasHover    = !impl->inputs.hoverPaths.empty();
            const bool hasOverlay  = !impl->inputs.overlayPaths.empty();
            const bool hasLead   = !impl->inputs.leadPath.IsEmpty();
            const bool useDefault  = impl->style.enableDefaultOutlines;

            params.enabled = hasSelected || hasHover || hasOverlay || useDefault;

            if (useDefault)
            {
                params.defaultPrimIdsTexture = _PrimIdsTextureName(kDefaultPrefix);
                params.defaultDepthTexture   = _DepthTextureName(kDefaultPrefix);
            }
            else
            {
                static std::once_flag warnedOnce;
                std::call_once(warnedOnce,
                    []()
                    {
                        TF_WARN(
                            "hvt::OutlineManager: default prim-ID / depth textures aliased to base "
                            "(enableDefaultOutlines=false).");
                    });
                params.defaultPrimIdsTexture = _PrimIdsTextureName(kBasePrefix);
                params.defaultDepthTexture   = _DepthTextureName(kBasePrefix);
            }
            if (!hasOverlay)
            {
                params.overlayPrimIdsTexture = _PrimIdsTextureName(kBasePrefix);
                params.overlayDepthTexture   = _DepthTextureName(kBasePrefix);
            }
            else
            {
                params.overlayPrimIdsTexture = _PrimIdsTextureName(kOverlayPrefix);
                params.overlayDepthTexture   = _DepthTextureName(kOverlayPrefix);
            }

            auto& s                     = params.style;
            s.selectedColor             = impl->style.selectedColor;
            s.selectedHoverColor        = impl->style.selectedHoverColor;
            s.selectionLeadColor        = impl->style.selectionLeadColor;
            s.selectionLeadHoverColor   = impl->style.selectionLeadHoverColor;
            s.unselectedHoverColor      = impl->style.unselectedHoverColor;
            s.overlayColor              = impl->style.overlayColor;
            s.overlayHoverColor         = impl->style.overlayHoverColor;
            s.defaultColor              = impl->style.defaultColor;
            s.softnessStrength          = impl->style.softnessStrength;
            s.softnessFalloff           = impl->style.softnessFalloff;
            s.hasDistinctOverlay        = hasOverlay ? 1 : 0;
            s.hasDistinctDefault        = useDefault ? 1 : 0;
            s.isHoverSelected           = impl->inputs.isHoverSelected ? 1 : 0;

            params.maskVisualizationMode = impl->style.maskVisualizationMode;

            // Path lists go straight through.
            params.leadPath   = impl->inputs.leadPath;
            params.hoverPaths   = impl->inputs.hoverPaths;
            params.overlayPaths = impl->inputs.overlayPaths;

            s.leadIdsCount  = hasLead ? 1 : 0;
            s.hoverIdsCount   = static_cast<int>(impl->inputs.hoverPaths.size());
            s.overlayIdsCount = static_cast<int>(impl->inputs.overlayPaths.size());

            // Integer prim-ID arrays are filled by OutlineMaskTask::_Sync(), which has
            // HdRenderIndex access. OutlineManager only passes SdfPath buckets through.
            params.leadIdValues.clear();
            params.hoverIdValues.clear();
            params.overlayIdValues.clear();

            if (impl->framePass != nullptr)
            {
                params.size = impl->framePass->params().renderBufferSize;
            }

            fnSet(PXR_NS::HdTokens->params, PXR_NS::VtValue(params));
        };

        impl->maskTaskId = taskMgr->AddTask<OutlineMaskTask>(_outlineTaskTokens->outlineMaskTask,
            params, fnCommit, impl->overlayTaskId, TaskManager::InsertionOrder::insertBefore);
    }

    // Install PrimIds Tasks
    auto installPrimIds = [&](PXR_NS::TfToken const& taskName, char const* prefix,
                              std::function<bool(Impl const&)> enabledFn,
                              std::function<PXR_NS::HdRprimCollection(Impl const&)> collectionFn)
    {
        OutlinePrimIdsTaskParams initial;
        initial.bufferPrefix = prefix;
        initial.enabled      = false;

        std::string prefixStr(prefix);
        auto fnCommit =
            [impl, prefixStr, enabledFn = std::move(enabledFn),
                collectionFn = std::move(collectionFn)](
                TaskManager::GetTaskValueFn const& fnGet, TaskManager::SetTaskValueFn const& fnSet)
        {
            auto params         = fnGet(PXR_NS::HdTokens->params).Get<OutlinePrimIdsTaskParams>();
            params.bufferPrefix = prefixStr;
            params.enabled      = enabledFn(*impl);
            if (params.enabled)
            {
                params.collection = collectionFn(*impl);
            }
            _ApplyViewportParams(params.size, params.camera, params.framing,
                params.overrideWindowPolicy, impl->framePass);
            fnSet(PXR_NS::HdTokens->params, PXR_NS::VtValue(params));
        };

        return taskMgr->AddTask<OutlinePrimIdsTask>(taskName, initial, fnCommit, impl->maskTaskId,
            TaskManager::InsertionOrder::insertBefore);
    };

    impl->basePrimIdsTaskId = installPrimIds(
        _outlineTaskTokens->outlineBasePrimIdsTask, kBasePrefix,
        [](Impl const& s)
        {
            return !s.inputs.selectedPaths.empty() 
                || !s.inputs.hoverPaths.empty() 
                || !s.inputs.overlayPaths.empty() 
                || s.style.enableDefaultOutlines;
        },
        [](Impl const& s)
        {
            PXR_NS::SdfPathVector roots = s.inputs.selectedPaths;
            roots.insert(roots.end(), s.inputs.hoverPaths.begin(), s.inputs.hoverPaths.end());
            return _MakeOutlineCollection(roots);
        });

    impl->overlayPrimIdsTaskId = installPrimIds(
        _outlineTaskTokens->outlineOverlayPrimIdsTask, kOverlayPrefix,
        [](Impl const& s) { return !s.inputs.overlayPaths.empty(); },
        [](Impl const& s) { return _MakeOutlineCollection(s.inputs.overlayPaths); });

    impl->defaultPrimIdsTaskId = installPrimIds(
        _outlineTaskTokens->outlineDefaultPrimIdsTask, kDefaultPrefix,
        [](Impl const& s) { return s.style.enableDefaultOutlines; },
        [](Impl const& s)
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
    auto& cur = _impl->inputs;

    // Track basic cache stats: a "hit" is a no-op SetInputs, a "miss" triggers
    // re-evaluation of derived collections on the next commit.
    const bool unchanged = cur.selectedPaths == inputs.selectedPaths
                        && cur.leadPath    == inputs.leadPath
                        && cur.hoverPaths    == inputs.hoverPaths
                        && cur.overlayPaths  == inputs.overlayPaths
                        && cur.excludePaths  == inputs.excludePaths
                        && cur.isHoverSelected == inputs.isHoverSelected;

    _impl->stats.totalQueries++;
    if (unchanged)
    {
        _impl->stats.hits++;
        return;
    }
    _impl->stats.misses++;

    const size_t totalSize = inputs.selectedPaths.size() + inputs.hoverPaths.size()
                           + inputs.overlayPaths.size() + (inputs.leadPath.IsEmpty() ? 0 : 1);
    if (totalSize > _impl->stats.maxCollectionSize)
    {
        _impl->stats.maxCollectionSize = totalSize;
    }
    if (_impl->stats.totalQueries > 0)
    {
        _impl->stats.avgCollectionSize =
            (_impl->stats.avgCollectionSize * (_impl->stats.totalQueries - 1) + totalSize)
            / _impl->stats.totalQueries;
    }

    _impl->inputs = std::move(inputs);
}

void OutlineManager::SetStyle(OutlineStyle style)
{
    if (_impl->style == style)
    {
        return;
    }
    _impl->style = std::move(style);
}

OutlineManager::CacheStats OutlineManager::GetCacheStats() const
{
    return _impl->stats;
}

} // namespace HVT_NS::Outline
