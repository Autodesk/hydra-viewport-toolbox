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

#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>

#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS::Outline
{

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

std::unique_ptr<PrimIdResolver> CreatePrimIdResolver(HdRenderIndex& renderIndex)
{
    return std::make_unique<RenderIndexPrimIdResolver>(renderIndex);
}

} // namespace HVT_NS::Outline
