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

#include "outlinePrimIdResolver.h"

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hdSt/renderDelegate.h>

#include <algorithm>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS::Outline
{

namespace
{

// The instancing depth Flash flattens at most (scene/instanceUtils.h).
constexpr size_t kFlashMaxInstancingDepth = 8;

} // anonymous namespace

bool IsStormRenderer(HdRenderDelegate* renderDelegate)
{
    return dynamic_cast<HdStRenderDelegate*>(renderDelegate) != nullptr;
}

bool IsFlashRenderer(HdRenderDelegate* renderDelegate)
{
    // As hvt::IsFlashRenderDelegate(): Flash's headers are not available to HVT.
    return renderDelegate && renderDelegate->GetRendererDisplayName() == "HdFlash";
}

bool IsOutlineSupportedRenderer(HdRenderDelegate* renderDelegate)
{
    return IsStormRenderer(renderDelegate) || IsFlashRenderer(renderDelegate);
}

VtIntArray const& PrimIdResolver::GetPrimIdRemap() const
{
    static VtIntArray const empty;
    return empty;
}

bool AppendPrimIds(PrimIdResolver const& resolver, SdfPath const& path, std::vector<int>* primIds)
{
    if (resolver.IsRprim(path))
    {
        int const primId = resolver.GetPrimId(path);
        if (primId >= 0)
        {
            primIds->push_back(primId);
        }
        return true;
    }

    SdfPathVector const rprims = resolver.GetRprimsUnder(path);
    for (SdfPath const& rprimPath : rprims)
    {
        int const primId = resolver.GetPrimId(rprimPath);
        if (primId >= 0)
        {
            primIds->push_back(primId);
        }
    }
    return !rprims.empty();
}

RenderIndexPrimIdResolver::RenderIndexPrimIdResolver(HdRenderIndex& renderIndex) :
    _renderIndex(&renderIndex)
{
}

bool RenderIndexPrimIdResolver::IsRprim(SdfPath const& path) const
{
    return _renderIndex->GetRprim(path) != nullptr;
}

int RenderIndexPrimIdResolver::GetPrimId(SdfPath const& rprimPath) const
{
    HdRprim const* rprim = _renderIndex->GetRprim(rprimPath);
    return rprim ? rprim->GetPrimId() : -1;
}

SdfPathVector RenderIndexPrimIdResolver::GetRprimsUnder(SdfPath const& path) const
{
    if (_renderIndex->GetRprim(path))
    {
        return { path };
    }
    return _renderIndex->GetRprimSubtree(path);
}

// This is the order HdStInstancer gathers instance indices in, so the index at level L is
// GetDrawingCoord().instanceIndex[L + 1] in the shader.
SdfPathVector RenderIndexPrimIdResolver::GetInstancerChain(SdfPath const& rprimPath) const
{
    SdfPathVector chain;
    HdRprim const* rprim = _renderIndex->GetRprim(rprimPath);
    if (!rprim)
    {
        return chain;
    }

    SdfPath id = rprim->GetInstancerId();
    while (!id.IsEmpty() && std::find(chain.begin(), chain.end(), id) == chain.end())
    {
        chain.push_back(id);
        HdInstancer const* instancer = _renderIndex->GetInstancer(id);
        if (!instancer)
        {
            break;
        }
        id = instancer->GetParentId();
    }
    return chain;
}

unsigned RenderIndexPrimIdResolver::GetPrimIdsVersion() const
{
    return _renderIndex->GetChangeTracker().GetRprimIndexVersion();
}

unsigned RenderIndexPrimIdResolver::GetInstancersVersion() const
{
    return _renderIndex->GetChangeTracker().GetInstancerIndexVersion();
}

FlashPrimIdResolver::FlashPrimIdResolver(HdRenderIndex& renderIndex) : _renderIndex(&renderIndex)
{
}

void FlashPrimIdResolver::Update()
{
    static TfToken const primIdToPathToken("hdFlash:primIdToPath", TfToken::Immortal);

    // A view over Flash's own storage: read it now, never keep it. Flash returns no value before
    // it has assigned any ID.
    VtValue const value = _renderIndex->GetRenderDelegate()->GetRenderSetting(primIdToPathToken);
    VtArray<SdfPath> const table = value.IsHolding<VtArray<SdfPath>>()
        ? value.UncheckedGet<VtArray<SdfPath>>()
        : VtArray<SdfPath>();

    SdfPath const* const data = table.cdata();
    if (table.size() == _tableSize && data == _tableData)
    {
        return;
    }
    _tableSize = table.size();
    _tableData = data;
    ++_version;

    // IDs ascend, so the first ID of a path is its lowest.
    _primIds.clear();
    std::vector<std::pair<int, int>> folded; // (ID, ID it folds onto), by ascending ID.
    for (size_t id = 0; id < _tableSize; ++id)
    {
        SdfPath const& path = data[id];
        if (path.IsEmpty())
        {
            continue;
        }
        auto const [it, inserted] = _primIds.try_emplace(path, static_cast<int>(id));
        if (!inserted)
        {
            folded.emplace_back(static_cast<int>(id), it->second);
        }
    }

    _primIdRemap = VtIntArray();
    if (folded.empty())
    {
        return;
    }

    int const firstId = folded.front().first;
    int const idCount = folded.back().first - firstId + 1;
    std::vector<int> remap(2 + static_cast<size_t>(idCount));
    remap[0] = firstId;
    remap[1] = idCount;
    for (int i = 0; i < idCount; ++i)
    {
        remap[2 + static_cast<size_t>(i)] = firstId + i;
    }
    for (auto const& [id, ownerId] : folded)
    {
        remap[2 + static_cast<size_t>(id - firstId)] = ownerId;
    }
    _primIdRemap = VtIntArray(remap.begin(), remap.end());
}

bool FlashPrimIdResolver::IsRprim(SdfPath const& path) const
{
    return _primIds.count(path) != 0;
}

int FlashPrimIdResolver::GetPrimId(SdfPath const& rprimPath) const
{
    auto const found = _primIds.find(rprimPath);
    return found != _primIds.end() ? found->second : -1;
}

SdfPathVector FlashPrimIdResolver::GetRprimsUnder(SdfPath const& path) const
{
    SdfPathVector rprims;
    auto const range = SdfPathFindPrefixedRange(_primIds.begin(), _primIds.end(), path,
        [](std::pair<SdfPath const, int> const& entry) -> SdfPath const& { return entry.first; });
    for (auto it = range.first; it != range.second; ++it)
    {
        rprims.push_back(it->first);
    }
    return rprims;
}

SdfPathVector FlashPrimIdResolver::GetInstancerChain(SdfPath const& rprimPath) const
{
    SdfPathVector chain;
    HdSceneIndexBaseRefPtr const sceneIndex = _renderIndex->GetTerminalSceneIndex();
    if (!sceneIndex || !IsRprim(rprimPath))
    {
        return chain;
    }

    SdfPath path = rprimPath;
    while (chain.size() < kFlashMaxInstancingDepth)
    {
        HdSceneIndexPrim const prim = sceneIndex->GetPrim(path);
        HdPathArrayDataSourceHandle const pathsDs =
            HdInstancedBySchema::GetFromParent(prim.dataSource).GetPaths();
        if (!pathsDs)
        {
            break;
        }
        VtArray<SdfPath> const instancers = pathsDs->GetTypedValue(0.0f);
        if (instancers.empty() || instancers.cdata()[0].IsEmpty()
            || std::find(chain.begin(), chain.end(), instancers.cdata()[0]) != chain.end())
        {
            break;
        }
        path = instancers.cdata()[0];
        chain.push_back(path);
    }
    return chain;
}

unsigned FlashPrimIdResolver::GetPrimIdsVersion() const
{
    return _version;
}

unsigned FlashPrimIdResolver::GetInstancersVersion() const
{
    return _version;
}

VtIntArray const& FlashPrimIdResolver::GetPrimIdRemap() const
{
    return _primIdRemap;
}

std::unique_ptr<PrimIdResolver> CreatePrimIdResolver(HdRenderIndex& renderIndex)
{
    if (IsFlashRenderer(renderIndex.GetRenderDelegate()))
    {
        return std::make_unique<FlashPrimIdResolver>(renderIndex);
    }
    return std::make_unique<RenderIndexPrimIdResolver>(renderIndex);
}

} // namespace HVT_NS::Outline
