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

#include <hvt/tasks/outline/outlinePrimIdsTask.h>

#include "outlineTextureNames.h"

#include <hvt/tasks/resources.h>

#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/bufferArray.h>
#include <pxr/imaging/hd/bufferSpec.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/imaging/hdSt/binding.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/renderPassShader.h>
#include <pxr/imaging/hdSt/renderPassState.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdSt/volume.h>
#include <pxr/usd/sdf/path.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    HVT_OUTLINE_PRIM_IDS_PARAMS,
    HVT_OUTLINE_PRIM_IDS_RESOURCES,
    HVT_OUTLINE_PRIM_IDS_VALIDATE
);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#endif

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_PARAMS,
        "outline primIds configuration params"
    );
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_RESOURCES,
        "outline primIds resources"
    );
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        HVT_OUTLINE_PRIM_IDS_VALIDATE,
        "outline primIds validate results"
    );
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS::Outline
{

namespace
{

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif

// hvtOutlineTargets is the buffer resource name: the shader reads it through
// HdGet_hvtOutlineTargets() under HD_HAS_hvtOutlineTargets. outlineTargets names the binding
// request on the render pass shader, and outline is the buffer array role.
TF_DEFINE_PRIVATE_TOKENS(_targetTokens,
    (hvtOutlineTargets)
    (outlineTargets)
    (outline)
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

// The instancers drawing an rprim, by level: element L is the instancer at level L, level 0 being
// the rprim's own instancer. This is the order HdStInstancer gathers instance indices in, so the
// index at level L is GetDrawingCoord().instanceIndex[L + 1] in the shader.
SdfPathVector _GetInstancerChain(HdRenderIndex& renderIndex, HdRprim const& rprim)
{
    SdfPathVector chain;
    SdfPath id = rprim.GetInstancerId();
    while (!id.IsEmpty() && std::find(chain.begin(), chain.end(), id) == chain.end())
    {
        chain.push_back(id);
        HdInstancer const* instancer = renderIndex.GetInstancer(id);
        if (!instancer)
        {
            break;
        }
        id = instancer->GetParentId();
    }
    return chain;
}

// The rprims a target path covers: the rprim itself, or the rprims under it.
SdfPathVector _GetRprimsUnder(HdRenderIndex& renderIndex, SdfPath const& path)
{
    if (renderIndex.GetRprim(path))
    {
        return { path };
    }
    return renderIndex.GetRprimSubtree(path);
}

// Bucket bits of a target. The shader writes them to the instanceId AOV, and outlineMask.glslfx
// decodes them: keep the three in step.
constexpr int kBucketSelected = 1;
constexpr int kBucketLead     = 2;
constexpr int kBucketHover    = 4;

// A restricted rprim, found once per resolve: its prim ID and its instancer chain.
struct RestrictedRprim
{
    int primId;
    SdfPathVector chain;
};

// Encodes the targets for the shader (HvtOutlineRecordBuckets in outlinePrimIds.glslfx),
// or returns an empty array when no rprim is restricted to instances. Layout, all int32:
//   [0] min prim ID, [1] prim ID count N,
//   [2 + primId - min] = 0 when the rprim is not restricted, else the offset of its record;
//   record: [target count], then per target [bucket bits][level count], then per level
//           [level][index count][sorted instancer-wide instance indices].
// A target with no level keeps every fragment of the rprim. Records are shared between rprims
// that have the same one. A record offset is never 0, since records follow the N-entry table.
//
// The shader tests every block of a record on every fragment, so the blocks of a record are
// merged per rprim: one level-less block per bucket, and one block per bucket and level for the
// targets with a single level, listing the union of their indices. A single level keeps the
// fragments whose index it lists, so the union keeps exactly what those targets keep together,
// and the loop is bounded by the buckets and levels rather than by the number of targets.
// Targets with several levels keep a block each (duplicates dropped): a union of intersections
// is not the intersection of the unions.
VtIntArray _EncodeTargets(HdRenderIndex& renderIndex, OutlinePrimIdsTaskParams const& params)
{
    struct Bucket
    {
        OutlineTargets const& targets;
        int bits;
    };
    Bucket const buckets[] = { { params.targets, kBucketSelected },
        { params.leadTargets, kBucketLead }, { params.hoverTargets, kBucketHover } };

    // An rprim is restricted when a target with instance levels covers it, whatever its bucket.
    // Sorted by path, so that a level-less target finds the restricted rprims under it without
    // walking its subtree. The rprims under each target path with levels are kept for the second
    // pass, so that each subtree is walked once.
    std::map<SdfPath, RestrictedRprim> restrictedRprims;
    std::unordered_map<SdfPath, SdfPathVector, SdfPath::Hash> rprimsUnder;
    for (Bucket const& bucket : buckets)
    {
        for (OutlineTarget const& target : bucket.targets)
        {
            if (target.instanceLevels.empty())
            {
                continue;
            }
            auto const [it, inserted] = rprimsUnder.try_emplace(target.path);
            if (!inserted)
            {
                continue;
            }
            it->second = _GetRprimsUnder(renderIndex, target.path);
            for (SdfPath const& path : it->second)
            {
                HdRprim const* rprim = renderIndex.GetRprim(path);
                if (rprim && rprim->GetPrimId() >= 0 && restrictedRprims.count(path) == 0)
                {
                    restrictedRprims.emplace(path,
                        RestrictedRprim { rprim->GetPrimId(),
                            _GetInstancerChain(renderIndex, *rprim) });
                }
            }
        }
    }
    if (restrictedRprims.empty())
    {
        return {};
    }

    struct Restriction
    {
        // Bucket bits of the level-less targets covering the rprim: one block per bucket.
        int levelLessBits = 0;
        // (bucket bits, level) -> the union of the indices of the single-level targets.
        std::map<std::pair<int, int>, std::vector<int>> singleLevel;
        // Encoded blocks of the targets with several levels.
        std::set<std::vector<int>> multiLevel;
        // Whether a selected or hover target covers the rprim. Lead targets only recolor.
        bool coveredByKeepingTarget = false;
    };
    // Ordered by prim ID, so the first and last entries bound the table.
    std::map<int, Restriction> restricted;

    std::vector<RestrictedRprim const*> covered;
    for (Bucket const& bucket : buckets)
    {
        for (OutlineTarget const& target : bucket.targets)
        {
            // The restricted rprims the target covers. Every rprim under a target with levels is
            // restricted; a level-less target only looks at the restricted rprims under its path.
            covered.clear();
            if (!target.instanceLevels.empty())
            {
                for (SdfPath const& path : rprimsUnder[target.path])
                {
                    auto const found = restrictedRprims.find(path);
                    if (found != restrictedRprims.end())
                    {
                        covered.push_back(&found->second);
                    }
                }
            }
            else if (renderIndex.GetRprim(target.path))
            {
                // An rprim covers itself only, as in _GetRprimsUnder().
                auto const found = restrictedRprims.find(target.path);
                if (found != restrictedRprims.end())
                {
                    covered.push_back(&found->second);
                }
            }
            else
            {
                auto const range = SdfPathFindPrefixedRange(restrictedRprims.begin(),
                    restrictedRprims.end(), target.path,
                    [](std::pair<SdfPath const, RestrictedRprim> const& entry) -> SdfPath const&
                    { return entry.first; });
                for (auto it = range.first; it != range.second; ++it)
                {
                    covered.push_back(&it->second);
                }
            }
            if (covered.empty())
            {
                continue;
            }

            // Sorted and deduplicated once per target, for the shader's binary search.
            std::vector<std::vector<int>> sortedIndices;
            sortedIndices.reserve(target.instanceLevels.size());
            for (OutlineInstanceLevel const& level : target.instanceLevels)
            {
                std::vector<int> indices(
                    level.instanceIndices.cbegin(), level.instanceIndices.cend());
                std::sort(indices.begin(), indices.end());
                indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
                sortedIndices.push_back(std::move(indices));
            }

            for (RestrictedRprim const* rprim : covered)
            {
                // Covered even when this target keeps none of the rprim's instances: a target
                // with instance levels covers instances only, so an rprim that one of its
                // instancers does not draw is not part of it.
                Restriction& restriction = restricted[rprim->primId];
                if (bucket.bits != kBucketLead)
                {
                    restriction.coveredByKeepingTarget = true;
                }

                if (target.instanceLevels.empty())
                {
                    restriction.levelLessBits |= bucket.bits;
                    continue;
                }

                // The level of each target instancer in the rprim's chain.
                std::vector<int> levels;
                levels.reserve(target.instanceLevels.size());
                for (OutlineInstanceLevel const& level : target.instanceLevels)
                {
                    auto const found =
                        std::find(rprim->chain.begin(), rprim->chain.end(), level.instancer);
                    if (found == rprim->chain.end())
                    {
                        break;
                    }
                    levels.push_back(static_cast<int>(found - rprim->chain.begin()));
                }
                if (levels.size() != target.instanceLevels.size())
                {
                    continue; // Not drawn by every level: the target keeps none of it.
                }

                if (levels.size() == 1)
                {
                    // Created even when empty: a level that lists no index keeps nothing.
                    std::vector<int>& indices =
                        restriction.singleLevel[{ bucket.bits, levels[0] }];
                    indices.insert(indices.end(), sortedIndices[0].begin(), sortedIndices[0].end());
                    continue;
                }

                std::vector<int> block { bucket.bits, static_cast<int>(levels.size()) };
                for (size_t i = 0; i < levels.size(); ++i)
                {
                    block.push_back(levels[i]);
                    block.push_back(static_cast<int>(sortedIndices[i].size()));
                    block.insert(block.end(), sortedIndices[i].begin(), sortedIndices[i].end());
                }
                restriction.multiLevel.insert(std::move(block));
            }
        }
    }

    if (restricted.empty())
    {
        return {};
    }

    int const minId    = restricted.begin()->first;
    int const idCount  = restricted.rbegin()->first - minId + 1;
    std::vector<int> data(2 + static_cast<size_t>(idCount), 0);
    data[0] = minId;
    data[1] = idCount;

    std::map<std::vector<int>, int> recordOffsets;
    for (auto& [primId, restriction] : restricted)
    {
        // A restricted rprim that only lead targets cover is in the collection without being
        // selected or hovered (a raw-task setup; OutlineManager never builds one): drawn whole,
        // as selected.
        if (!restriction.coveredByKeepingTarget)
        {
            restriction.levelLessBits |= kBucketSelected;
        }

        std::vector<int> record { 0 }; // The target count, set once the blocks are in.
        int targetCount = 0;
        for (int const bits : { kBucketSelected, kBucketLead, kBucketHover })
        {
            if (restriction.levelLessBits & bits)
            {
                record.insert(record.end(), { bits, 0 });
                ++targetCount;
            }
        }
        for (auto& [key, indices] : restriction.singleLevel)
        {
            // Each target's indices are sorted, but several targets' are only concatenated.
            if (!std::is_sorted(indices.begin(), indices.end()))
            {
                std::sort(indices.begin(), indices.end());
            }
            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
            record.insert(record.end(),
                { key.first, 1, key.second, static_cast<int>(indices.size()) });
            record.insert(record.end(), indices.begin(), indices.end());
            ++targetCount;
        }
        for (std::vector<int> const& block : restriction.multiLevel)
        {
            record.insert(record.end(), block.begin(), block.end());
            ++targetCount;
        }
        record[0] = targetCount;

        auto const [it, inserted] =
            recordOffsets.try_emplace(std::move(record), static_cast<int>(data.size()));
        if (inserted)
        {
            data.insert(data.end(), it->first.begin(), it->first.end());
        }
        data[2 + static_cast<size_t>(primId - minId)] = it->second;
    }

    return VtIntArray(data.begin(), data.end());
}

bool _HasInstanceLevels(OutlineTargets const& targets)
{
    return std::any_of(targets.begin(), targets.end(),
        [](OutlineTarget const& target) { return !target.instanceLevels.empty(); });
}

// Whether instance isolation is active: some target of some bucket has instance levels.
bool _HasInstanceLevels(OutlinePrimIdsTaskParams const& params)
{
    return _HasInstanceLevels(params.targets) || _HasInstanceLevels(params.leadTargets)
        || _HasInstanceLevels(params.hoverTargets);
}

bool _IsStormRenderer(HdRenderDelegate* renderDelegate)
{
    return dynamic_cast<HdStRenderDelegate*>(renderDelegate) != nullptr;
}

SdfPath _GetAovPath(TfToken const& aovName)
{
    std::string identifier =
        std::string("aov_outlinePrimIds_") + TfMakeValidIdentifier(aovName.GetString());
    return SdfPath(identifier);
}

HdRenderPassStateSharedPtr _InitIdRenderPassState(HdRenderIndex* index, TfToken const& shaderPath)
{
    HdRenderPassStateSharedPtr rps = index->GetRenderDelegate()->CreateRenderPassState();

    if (HdStRenderPassState* extendedState = dynamic_cast<HdStRenderPassState*>(rps.get()))
    {
        if (shaderPath.IsEmpty())
        {
            TF_CODING_ERROR("Cannot initialize render pass state: picking shader path is empty");
            return rps;
        }
        auto pickGlslfx = std::make_shared<HioGlslfx>(shaderPath, HioGlslfxTokens->defVal);
        extendedState->SetRenderPassShader(std::make_shared<HdStRenderPassShader>(pickGlslfx));
    }
    return rps;
}

} // anonymous namespace

OutlinePrimIdsTask::OutlinePrimIdsTask(HdSceneDelegate* /* delegate */, SdfPath const& id) :
    HdxTask(id), _renderIndex(nullptr), _isStormRenderer(false), _vpChanged(false)
{
    TfDebug::Disable(HVT_OUTLINE_PRIM_IDS_PARAMS);
    TfDebug::Disable(HVT_OUTLINE_PRIM_IDS_RESOURCES);
    TfDebug::Disable(HVT_OUTLINE_PRIM_IDS_VALIDATE);
}

OutlinePrimIdsTask::~OutlinePrimIdsTask()
{
    _CleanupAovBindings();
}

bool OutlinePrimIdsTask::_Enabled() const
{
    return _isStormRenderer;
}

bool OutlinePrimIdsTask::_InitIfNeeded()
{
    if (_vpChanged || _aovBuffers.empty())
    {
        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg(
                "(RESOURCES) OutlinePrimIdsTask: Viewport changed or buffers need creation: "
                "%dx%d\n",
                _params.size[0], _params.size[1]);

        // Reported here rather than inferred from _aovBindings: a failure can leave a partial
        // set, which is indistinguishable from success by inspection. Without complete bindings
        // Execute() would run the render pass with nothing, or not enough, attached.
        // _CreateAovBindings() raises its own diagnostics, which are not latched, unlike the two
        // below.
        if (!_CreateAovBindings())
        {
            return false;
        }
        _vpChanged = false;
    }

    // The instanceId AOV comes and goes with instance isolation, on a selection change. Only its
    // binding does: the buffer is kept, so no render buffer is reallocated.
    if (!_UpdateInstanceIdAov())
    {
        return false;
    }

    // Every resource is tested, not just the render pass: a pass that was created before the state
    // failed would otherwise make the next call skip this block and report success with a null
    // _renderPassState, which Prepare() and Execute() dereference unguarded.
    if (!_renderPass || !_renderPassState)
    {
        // Each step is guarded separately so a retry re-attempts only what is missing, and the
        // latch is released as each one succeeds: a pass that comes up on a retry must not silence
        // the diagnostic for a state that then fails.
        if (!_renderPass)
        {
            // The collection created below is just for satisfying the HdRenderPass
            // constructor. The collections for the render passes are set in Query.
            HdRprimCollection col(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));

            _renderPass = _renderIndex->GetRenderDelegate()->CreateRenderPass(&*_renderIndex, col);
            if (!_renderPass)
            {
                if (!_initWarned)
                {
                    TF_CODING_ERROR("Failed to create render pass");
                    _initWarned = true;
                }
                return false;
            }
            _initWarned = false;
        }

        if (!_renderPassState)
        {
            _renderPassState = _InitIdRenderPassState(_renderIndex, _GetShaderFilePath());
            if (!_renderPassState)
            {
                if (!_initWarned)
                {
                    TF_CODING_ERROR("Failed to create render pass state");
                    _initWarned = true;
                }
                return false;
            }
        }
    }

    _initWarned = false;
    return true;
}

bool OutlinePrimIdsTask::_CreateAovBindings()
{
    if (!_renderIndex)
    {
        TF_CODING_ERROR("No render index available for AOV creation");
        return false;
    }

    _CleanupAovBindings();

    if (_params.size[0] <= 0 || _params.size[1] <= 0)
    {
        TF_CODING_ERROR("Invalid buffer dimensions: %dx%d", _params.size[0], _params.size[1]);
        return false;
    }

    try
    {
        // The outline pipeline samples depth only: the render pass disables stencil and the mask
        // shader discards the stencil channel. A combined depth/stencil AOV is therefore never
        // read, and on WebGPU a two-aspect texture cannot be bound as a sampled texture.
        for (TfToken const& aovOutput : { HdAovTokens->primId, HdAovTokens->depth })
        {
            HdRenderPassAovBinding binding;
            if (!_AllocateAov(aovOutput, &binding))
            {
                // Discard the bindings already pushed for earlier AOVs. A partial set survives
                // otherwise: the caller re-enters only when _vpChanged is set or _aovBuffers is
                // empty, and a partial set is neither.
                _CleanupAovBindings();
                return false;
            }
            _aovBindings.push_back(binding);
        }

        _primIdBindingIndex     = 0;
        _depthBindingIndex      = 1;
        _instanceIdBindingIndex = 2;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg(
                "(RESOURCES) OutlinePrimIdsTask: Successfully created %s primId + depth AOV "
                "buffers %dx%d\n",
                _params.bufferPrefix.c_str(), _params.size[0], _params.size[1]);
    }
    catch (std::exception const& e)
    {
        TF_CODING_ERROR("Exception during primId AOV creation: %s", e.what());
        _CleanupAovBindings();
        return false;
    }
    catch (...)
    {
        TF_CODING_ERROR("Unknown exception during primId AOV creation");
        _CleanupAovBindings();
        return false;
    }

    return true;
}

bool OutlinePrimIdsTask::_AllocateAov(TfToken const& aovName, HdRenderPassAovBinding* binding)
{
    HdStResourceRegistrySharedPtr resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(_renderIndex->GetResourceRegistry());
    if (!resourceRegistry)
    {
        TF_CODING_ERROR("No resource registry available");
        return false;
    }

    // make_unique throws rather than returning null, so allocation failure arrives either here as
    // an exception or below as a false Allocate() result.
    SdfPath const aovId = _GetAovPath(aovName);
    auto aovBuffer      = std::make_unique<HdStRenderBuffer>(resourceRegistry.get(), aovId);

    HdAovDescriptor const aovDesc =
        _renderIndex->GetRenderDelegate()->GetDefaultAovDescriptor(aovName);
    if (!aovBuffer->Allocate(GfVec3i(_params.size[0], _params.size[1], 1), aovDesc.format, false))
    {
        TF_CODING_ERROR("Failed to allocate AOV buffer for %s", aovName.GetText());
        return false;
    }

    binding->aovName        = aovName;
    binding->renderBufferId = aovId;
    binding->renderBuffer   = aovBuffer.get();
    binding->aovSettings    = aovDesc.aovSettings;
    binding->clearValue     = aovDesc.clearValue;
    _aovBuffers.push_back(std::move(aovBuffer));

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
        .Msg("(RESOURCES) OutlinePrimIdsTask: Created AOV buffer for %s (%dx%d)\n",
            aovName.GetText(), _params.size[0], _params.size[1]);
    return true;
}

bool OutlinePrimIdsTask::_UpdateInstanceIdAov()
{
    // Only while instance isolation is active: the instances an rprim keeps share its prim ID, and
    // the mask tells touching ones apart by this ID. Without it the pass has the same two
    // attachments as before isolation existed.
    bool const wanted = _HasInstanceLevels(_params);
    bool const bound  = _aovBindings.size() > _instanceIdBindingIndex;
    if (wanted == bound)
    {
        return true;
    }

    if (!wanted)
    {
        // The buffer stays allocated for the next time isolation turns on.
        _aovBindings.resize(_instanceIdBindingIndex);
        return true;
    }

    if (!_instanceIdBinding.renderBuffer)
    {
        try
        {
            HdRenderPassAovBinding binding;
            if (!_AllocateAov(HdAovTokens->instanceId, &binding))
            {
                return false;
            }
            _instanceIdBinding = binding;
        }
        catch (std::exception const& e)
        {
            TF_CODING_ERROR("Exception during instanceId AOV creation: %s", e.what());
            return false;
        }
        catch (...)
        {
            TF_CODING_ERROR("Unknown exception during instanceId AOV creation");
            return false;
        }
    }

    _aovBindings.push_back(_instanceIdBinding);
    return true;
}

void OutlinePrimIdsTask::_CleanupAovBindings()
{
    if (_renderIndex)
    {
        HdRenderParam* renderParam = _renderIndex->GetRenderDelegate()->GetRenderParam();
        for (auto const& aovBuffer : _aovBuffers)
        {
            aovBuffer->Finalize(renderParam);
        }
    }
    _aovBuffers.clear();
    _aovBindings.clear();
    _instanceIdBinding = HdRenderPassAovBinding();
}

void OutlinePrimIdsTask::_Sync(
    HdSceneDelegate* delegate, HdTaskContext* /* ctx */, HdDirtyBits* dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _renderIndex     = &(delegate->GetRenderIndex());
    _isStormRenderer = _IsStormRenderer(_renderIndex->GetRenderDelegate());

    if (!_Enabled())
    {
        // Report the bits as consumed; DirtyParams is the only one this task reads, since the
        // collection travels inside OutlinePrimIdsTaskParams rather than under
        // HdTokens->collection. This does not latch the task off: a later params update re-dirties
        // it, Hydra may sync it even when clean, and an HdRenderIndex's render delegate is fixed at
        // construction, so a renderer switch destroys this task rather than leaving
        // _isStormRenderer stale.
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        OutlinePrimIdsTaskParams params;
        if (!_GetTaskParams(delegate, &params))
        {
            // Leave the dirty bits set so a later re-sync retries the fetch. The previously
            // fetched parameters stay in effect meanwhile. Warn once per failure streak: this
            // path re-runs every frame while the fetch keeps failing.
            if (!_paramsFetchWarned)
            {
                TF_WARN("OutlinePrimIdsTask: could not fetch task parameters; keeping the previous "
                        "values and retrying on the next sync.");
                _paramsFetchWarned = true;
            }
            return;
        }
        _paramsFetchWarned = false;

        if (_params.size != params.size)
        {
            _vpChanged = true;
        }

        if (_params.targets != params.targets || _params.leadTargets != params.leadTargets
            || _params.hoverTargets != params.hoverTargets)
        {
            _targetsResolveNeeded = true;
        }

        _params = params;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_PARAMS)
            .Msg("(PARAMS) OutlinePrimIdsTask: enabled=%s, size=%dx%d, vpChanged=%s\n",
                params.enabled ? "YES" : "NO", params.size[0], params.size[1],
                _vpChanged ? "YES" : "NO");
    }

    if (!_params.enabled)
    {
        // Nothing to sync while disabled; a later enable arrives as a fresh DirtyParams.
        *dirtyBits = HdChangeTracker::Clean;
        return;
    }

    if (!_InitIfNeeded())
    {
        // Initialization failed (e.g. the render pass or ID render-pass-state could not be
        // created). Disable the task so Prepare()/Execute() do not dereference a null
        // _renderPassState. The dirty bits are intentionally left set so a later DirtyParams
        // re-sync retries initialization.
        _params.enabled = false;
        return;
    }

    GfVec4i viewport(0, 0, _params.size[0], _params.size[1]);

    HdCamera const* camera = static_cast<HdCamera const*>(
        _renderIndex->GetSprim(HdPrimTypeTokens->camera, _params.camera));

    if (!camera)
    {
        TF_CODING_ERROR("Failed to get camera");
        return;
    }

    // Get the volume steps sizes in case there is any volume rendering.
    float const stepSize = delegate->GetRenderIndex().GetRenderDelegate()->GetRenderSetting<float>(
        HdStRenderSettingsTokens->volumeRaymarchingStepSize, HdStVolume::defaultStepSize);
    float const stepSizeLighting =
        delegate->GetRenderIndex().GetRenderDelegate()->GetRenderSetting<float>(
            HdStRenderSettingsTokens->volumeRaymarchingStepSizeLighting,
            HdStVolume::defaultStepSizeLighting);

    // Update the render pass states.
    HdRenderPassStateSharedPtr states[] = { _renderPassState };
    for (auto& state : states)
    {
        state->SetStencilEnabled(false);

        state->SetEnableDepthTest(true);
        state->SetEnableDepthMask(true);
        state->SetDepthFunc(HdCmpFuncLEqual);
        // Set alpha threshold, to potentially discard translucent pixels.
        // The default value of 0.0001 allows semi-transparent pixels to be picked,
        // but discards fully transparent ones.
        state->SetAlphaThreshold(0.0001f);
        state->SetAlphaToCoverageEnabled(false);
        state->SetBlendEnabled(false);
        state->SetCullStyle(_params.cullStyle);
        state->SetLightingEnabled(false);
        state->SetVolumeRenderingConstants(stepSize, stepSizeLighting);
        // Disable conservative rasterization to avoid depth artifacts
        // Conservative rasterization can cause Z-fighting at object boundaries
        state->SetConservativeRasterizationEnabled(false);

        if (camera && _params.framing.IsValid())
        {
            state->SetCamera(camera);
            state->SetFraming(_params.framing);
            state->SetOverrideWindowPolicy(_params.overrideWindowPolicy);
        }
        else if (camera)
        {
            state->SetCamera(camera);
            state->SetViewport(viewport);
        }
    }

    _renderPass->SetRprimCollection(_params.collection);

    if (TfDebug::IsEnabled(HVT_OUTLINE_PRIM_IDS_PARAMS))
    {
        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_PARAMS)
            .Msg("(RESOURCES) OutlinePrimIdsTask: Collection prims (count: %zu):\n",
                _params.collection.GetRootPaths().size());
        auto rootPaths = _params.collection.GetRootPaths();
        for (SdfPath const& path : rootPaths)
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_PARAMS)
                .Msg("(RESOURCES) OutlinePrimIdsTask: > path: %s\n", path.GetString().c_str());
        }
    }

    _renderPass->Sync();

    *dirtyBits = HdChangeTracker::Clean;
}

void OutlinePrimIdsTask::Prepare(HdTaskContext* /* ctx */, HdRenderIndex* renderIndex)
{
    if (!_Enabled() || !_params.enabled)
    {
        return;
    }

    // Before the state's Prepare(): the binding is part of the render pass shader. Prepare() is
    // also where the buffer source must be added, so the resource registry commits it before
    // Execute().
    _UpdateTargetsBinding(renderIndex);

    _renderPassState->SetAovBindings(_aovBindings);
    _renderPassState->Prepare(renderIndex->GetResourceRegistry());
}

void OutlinePrimIdsTask::_UpdateTargetsBinding(HdRenderIndex* renderIndex)
{
    auto* stState = dynamic_cast<HdStRenderPassState*>(_renderPassState.get());
    if (!stState || !stState->GetRenderPassShader() || !renderIndex)
    {
        return;
    }
    HdStRenderPassShaderSharedPtr const& shader = stState->GetRenderPassShader();

    auto unbind = [&]()
    {
        if (_targetsBound)
        {
            shader->RemoveBufferBinding(_targetTokens->outlineTargets);
            _targetsBound = false;
        }
    };

    // No restriction to instances: the plain shader, exactly as without targets.
    if (!_HasInstanceLevels(_params))
    {
        unbind();
        return;
    }

    // Prim IDs are reassigned when rprims are inserted or removed, and instancer chains change
    // when instancers are. Quiet frames cost two comparisons. Not caught: an rprim that moves to
    // another instancer while staying in the render index (DirtyInstancer alone) keeps its old
    // chain until the next resolve. Watching for it would take the rprim's dirty bit in _Sync()
    // (tasks sync before rprims) plus a chain comparison here, on every frame an instancer
    // changes, since any instancer change sets DirtyInstancer on its rprims.
    HdChangeTracker const& tracker      = renderIndex->GetChangeTracker();
    unsigned const rprimIndexVersion     = tracker.GetRprimIndexVersion();
    unsigned const instancerIndexVersion = tracker.GetInstancerIndexVersion();
    if (!_targetsResolveNeeded && rprimIndexVersion == _targetsRprimIndexVersion
        && instancerIndexVersion == _targetsInstancerIndexVersion)
    {
        return;
    }
    _targetsResolveNeeded         = false;
    _targetsRprimIndexVersion     = rprimIndexVersion;
    _targetsInstancerIndexVersion = instancerIndexVersion;

    VtIntArray const encoded = _EncodeTargets(*renderIndex, _params);
    if (encoded.empty())
    {
        // Targets with instance levels, but none of their rprims is in the render index (yet).
        unbind();
        return;
    }

    HdStResourceRegistrySharedPtr const registry =
        std::dynamic_pointer_cast<HdStResourceRegistry>(renderIndex->GetResourceRegistry());
    if (!registry)
    {
        unbind();
        return;
    }

    if (!_targetsBar)
    {
        HdBufferSpecVector specs;
        specs.emplace_back(_targetTokens->hvtOutlineTargets, HdTupleType { HdTypeInt32, 1 });
        _targetsBar = registry->AllocateSingleBufferArrayRange(
            _targetTokens->outline, specs, HdBufferArrayUsageHintBitsStorage);
    }
    registry->AddSource(_targetsBar,
        std::make_shared<HdVtBufferSource>(_targetTokens->hvtOutlineTargets, VtValue(encoded)));

    // Re-added on every upload, as HdxRenderTask does for the selection buffer: the request is
    // replaced by name and the shader hash recomputed, which keeps the binding current if the
    // range is reallocated to fit a larger encoding.
    shader->AddBufferBinding(HdStBindingRequest(
        HdStBinding::SSBO, _targetTokens->outlineTargets, _targetsBar, /*interleave=*/false));
    _targetsBound = true;
}

HgiTextureHandle OutlinePrimIdsTask::_GetTextureHandleForBinding(size_t bindingIndex) const
{
    if (_aovBindings.empty())
    {
        TF_CODING_ERROR("No AOV bindings available");
        return HgiTextureHandle();
    }

    if (bindingIndex >= _aovBindings.size())
    {
        TF_CODING_ERROR("Binding index out of bounds: %zu", bindingIndex);
        return HgiTextureHandle();
    }

    HdRenderPassAovBinding const& aovBinding = _aovBindings[bindingIndex];
    if (!aovBinding.renderBuffer)
    {
        TF_CODING_ERROR("No render buffer available for binding index %zu", bindingIndex);
        return HgiTextureHandle();
    }

    VtValue resource = aovBinding.renderBuffer->GetResource(false);
    if (!resource.IsHolding<HgiTextureHandle>())
    {
        TF_CODING_ERROR(
            "Resource is not a valid texture handle for binding index %zu", bindingIndex);
        return HgiTextureHandle();
    }

    HgiTextureHandle textureHandle = resource.UncheckedGet<HgiTextureHandle>();
    if (!textureHandle)
    {
        TF_CODING_ERROR("Null texture handle in resource for binding index %zu", bindingIndex);
        return HgiTextureHandle();
    }

    return textureHandle;
}

void OutlinePrimIdsTask::_RefreshTextureTokensIfNeeded()
{
    if (!_primIdsTextureToken.IsEmpty() && _textureTokenPrefix == _params.bufferPrefix)
    {
        return;
    }

    // Not Immortal: these are derived from mutable params, and the members hold them for as long as
    // this task needs them. Immortal would pin one registry entry per prefix ever seen. (The fixed
    // names in outlineTextureNames.h are constants, so Immortal is right for those.)
    _textureTokenPrefix      = _params.bufferPrefix;
    _primIdsTextureToken     = TfToken(OutlinePrimIdsTextureName(_textureTokenPrefix));
    _depthTextureToken       = TfToken(OutlineDepthTextureName(_textureTokenPrefix));
    _instanceIdsTextureToken = TfToken(OutlineInstanceIdsTextureName(_textureTokenPrefix));
}

void OutlinePrimIdsTask::Execute(HdTaskContext* ctx)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!ctx)
    {
        TF_CODING_ERROR("No task context available");
        return;
    }

    // Keep the cached tokens in step with the buffer prefix before either branch below uses them.
    _RefreshTextureTokensIfNeeded();

    // When disabled, clear our textures from the task context so downstream
    // tasks don't use stale data from previous frames
    if (!_Enabled() || !_params.enabled)
    {
        ctx->erase(_primIdsTextureToken);
        ctx->erase(_depthTextureToken);
        ctx->erase(_instanceIdsTextureToken);
        return;
    }

    if (!_renderIndex)
    {
        TF_CODING_ERROR("No render index available");
        return;
    }

    _renderPassState->SetAovBindings(_aovBindings);
    _renderPass->Execute(_renderPassState, GetRenderTags());

    // Export the rendered primId texture for other tasks to consume
    HgiTextureHandle textureHandle = _GetTextureHandleForBinding(_primIdBindingIndex);
    if (textureHandle)
    {
        HdRenderPassAovBinding const& aovBinding = _aovBindings[_primIdBindingIndex];
        VtValue resource                         = aovBinding.renderBuffer->GetResource(false);

        (*ctx)[_primIdsTextureToken] = resource;

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                _primIdsTextureToken.GetText());

#ifndef __EMSCRIPTEN__
        // Note: this option is not exposed for web as it requires getting the buffer
        // from GPU to CPU and would require adopting the async texture readback API.
        // This is for debugging purposes and can be used in a desktop build.
        if (TfDebug::IsEnabled(HVT_OUTLINE_PRIM_IDS_VALIDATE))
        {
            // Validate the primId buffer to ensure correct integer values
            _ValidatePrimIdBuffer(aovBinding, resource);
        }
#endif
    }

    if (_depthBindingIndex < _aovBindings.size())
    {
        textureHandle = _GetTextureHandleForBinding(_depthBindingIndex);
        if (textureHandle)
        {
            HdRenderPassAovBinding const& aovBinding = _aovBindings[_depthBindingIndex];
            VtValue resource                         = aovBinding.renderBuffer->GetResource(false);

            (*ctx)[_depthTextureToken] = resource;

            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
                .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                    _depthTextureToken.GetText());
        }
    }

    // Optional: erased while isolation is off (the AOV is then not bound, although its buffer is
    // kept), so the mask does not read a stale buffer and falls back to drawing no instance seams.
    textureHandle = _instanceIdBindingIndex < _aovBindings.size()
        ? _GetTextureHandleForBinding(_instanceIdBindingIndex)
        : HgiTextureHandle();
    if (textureHandle)
    {
        (*ctx)[_instanceIdsTextureToken] =
            _aovBindings[_instanceIdBindingIndex].renderBuffer->GetResource(false);

        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_RESOURCES)
            .Msg("(RESOURCES) OutlinePrimIdsTask: Successfully exported %s\n",
                _instanceIdsTextureToken.GetText());
    }
    else
    {
        ctx->erase(_instanceIdsTextureToken);
    }
}

TfToken const& OutlinePrimIdsTask::GetToken(const std::string& prefix)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, TfToken> tokens;

    const std::string name = "outline" + prefix + "PrimIdsTask";

    // Not Immortal: the map owns each token for the life of the process, which is what lets this
    // return a reference. Immortal is for genuine constants, and would add nothing here.
    std::lock_guard<std::mutex> lock(mutex);
    return tokens.try_emplace(name, name).first->second;
}

void OutlinePrimIdsTask::_ValidatePrimIdBuffer(
    HdRenderPassAovBinding /* binding */, VtValue resource)
{
    constexpr int kMaxValidationOutputCount = 10;

    HgiTextureHandle texture = resource.UncheckedGet<HgiTextureHandle>();

    if (!texture || !_renderIndex)
    {
        return;
    }

    Hgi* hgi = _GetHgi();
    if (!hgi)
    {
        TF_CODING_ERROR("No Hgi instance available\n");
        return;
    }

    SdfPathVector const& primIds = _renderIndex->GetRprimIds();

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: All prims in RenderIndex (%zu prims):\n",
            primIds.size());
    for (size_t i = 0; i < primIds.size(); ++i)
    {
        HdRprim const* rPrim = _renderIndex->GetRprim(primIds[i]);
        if (rPrim)
        {
            int32_t primId = rPrim->GetPrimId();
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > [%d]: %s\n", primId,
                    primIds[i].GetString().c_str());
        }
        else
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > [<INVALID>]: %s\n",
                    primIds[i].GetString().c_str());
        }

        if (i >= kMaxValidationOutputCount)
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > ... (truncated)\n");
            break;
        }
    }

    HgiTextureDesc const& texDesc = texture->GetDescriptor();
    int width                     = texDesc.dimensions[0];
    int height                    = texDesc.dimensions[1];

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: PrimId buffer dimensions: %dx%d\n", width, height);

    // Expected data size
    size_t dataSize = width * height * sizeof(int32_t);

    // Get the primId buffer using HgiTextureReadback
    size_t bufferSize = 0;
    HdStTextureUtils::AlignedBuffer<int> primIdsBuffer =
        HdStTextureUtils::HgiTextureReadback<int>(hgi, texture, &bufferSize);

    if (bufferSize != dataSize)
    {
        TF_CODING_ERROR("invalid bufferSize: %zu, expected %zu\n", bufferSize, dataSize);
        return;
    }

    int const* pixelData = primIdsBuffer.get();

    if (!pixelData)
    {
        TF_CODING_ERROR("No primIds buffer available\n");
        return;
    }

    // Count occurrences of each primId value
    std::map<int32_t, int> validPrimIdCounts;
    int invalidNegativeCount = 0;
    int invalidPositiveCount = 0;
    int validPrimIdCount     = 0;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int32_t primId = pixelData[y * width + x];

            if (primId < -1)
            {
                if (invalidNegativeCount < kMaxValidationOutputCount)
                {
                    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                        .Msg(
                            "(VALIDATE) OutlinePrimIdsTask: (%d, %d) - invalid negative value "
                            "(%d)\n",
                            x, y, primId);
                }
                invalidNegativeCount++;
            }
            else if (primId == -1)
            {
                validPrimIdCounts[primId]++;
                validPrimIdCount++;
            }
            else
            {
                SdfPath primPath = _renderIndex->GetRprimPathFromPrimId(primId);
                if (primPath.IsEmpty())
                {
                    if (invalidPositiveCount < kMaxValidationOutputCount)
                    {
                        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                            .Msg("(VALIDATE) OutlinePrimIdsTask: (%d, %d) - invalid primId (%d)\n",
                                x, y, primId);
                    }
                    invalidPositiveCount++;
                }
                else
                {
                    if (validPrimIdCount < kMaxValidationOutputCount)
                    {
                        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                            .Msg(
                                "(VALIDATE) OutlinePrimIdsTask: (%d, %d) - valid primId (%d): %s\n",
                                x, y, primId, primPath.GetString().c_str());
                    }
                    validPrimIdCounts[primId]++;
                    validPrimIdCount++;
                }
            }
        }
    }

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Count of valid pixels: %d/%d (%.4f%%)\n",
            validPrimIdCount, width * height, (validPrimIdCount * 100.0) / (width * height));

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Counts per valid primId (%d):\n", validPrimIdCount);
    for (auto const& [primId, count] : validPrimIdCounts)
    {
        if (primId == -1)
        {
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > Empty (primId -1): %d pixels (%.4f%%)\n",
                    count, (count * 100.0) / (width * height));
        }
        else
        {
            SdfPath const& primPath = _renderIndex->GetRprimPathFromPrimId(primId);
            TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
                .Msg("(VALIDATE) OutlinePrimIdsTask: > PrimId %d: %d pixels (%.4f%%) (%s)\n",
                    primId, count, (count * 100.0) / (width * height),
                    primPath.GetString().c_str());
        }
    }

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Count of invalid negative primIds: %d/%d (%.4f%%)\n",
            invalidNegativeCount, width * height,
            (invalidNegativeCount * 100.0) / (width * height));

    TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
        .Msg("(VALIDATE) OutlinePrimIdsTask: Count of invalid positive primIds: %d/%d (%.4f%%)\n",
            invalidPositiveCount, width * height,
            (invalidPositiveCount * 100.0) / (width * height));

    if (invalidNegativeCount == 0 && invalidPositiveCount == 0)
    {
        TF_DEBUG(HVT_OUTLINE_PRIM_IDS_VALIDATE)
            .Msg("(VALIDATE) OutlinePrimIdsTask: PrimId buffer validation passed!\n");
    }
}

TfToken OutlinePrimIdsTask::_GetShaderFilePath()
{
    auto shaderFilePath = GetShaderPath("outlinePrimIds.glslfx");
    if (!std::filesystem::is_regular_file(shaderFilePath))
    {
        TF_RUNTIME_ERROR("Shader file not found: %s", shaderFilePath.string().c_str());
        return TfToken {};
    }

    // generic_u8string() is UTF-8 on every platform (lossless for non-ASCII install paths),
    // unlike generic_string() which is the native narrow encoding (lossy ANSI on Windows).
    // The begin/end copy yields a std::string under both C++17 (char) and C++20 (char8_t).
    auto const u8str = shaderFilePath.generic_u8string();
    std::string const shaderStr(u8str.begin(), u8str.end());
    static TfToken const shader { shaderStr, TfToken::Immortal };
    return shader;
}

} // namespace HVT_NS::Outline
