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
#pragma once

#include <hvt/api.h>

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderIndex;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS::Outline
{

/// What the outline tasks need to know about the prims a render delegate draws: the primitive IDs
/// its ID passes write, and the instancers drawing each rprim. A render delegate that does not
/// populate the render index with rprims answers these through another implementation.
class PrimIdResolver
{
public:
    virtual ~PrimIdResolver() = default;

    /// Returns true when the path is an rprim.
    /// \param path The scene path to test.
    virtual bool IsRprim(PXR_NS::SdfPath const& path) const = 0;

    /// Returns the primitive ID of an rprim, or -1 when the path is not an rprim or has no ID.
    /// \param rprimPath The rprim path.
    virtual int GetPrimId(PXR_NS::SdfPath const& rprimPath) const = 0;

    /// Returns the rprims a path covers: the rprim itself, or the rprims under it.
    /// \param path The scene path.
    virtual PXR_NS::SdfPathVector GetRprimsUnder(PXR_NS::SdfPath const& path) const = 0;

    /// Returns the instancers drawing an rprim, by level: element L is the instancer at level L,
    /// level 0 being the rprim's own instancer. Empty when the rprim is not instanced or is not
    /// an rprim.
    /// \param rprimPath The rprim path.
    virtual PXR_NS::SdfPathVector GetInstancerChain(PXR_NS::SdfPath const& rprimPath) const = 0;

    /// Returns a version that changes whenever a primitive ID can have been assigned, reassigned
    /// or released. Results resolved at an equal version are still valid.
    virtual unsigned GetPrimIdsVersion() const = 0;

    /// Returns a version that changes whenever an instancer chain can have changed. Results
    /// resolved at equal versions are still valid.
    virtual unsigned GetInstancersVersion() const = 0;
};

/// Appends the primitive IDs a path resolves to: the ID of the rprim itself, or the IDs of the
/// rprims under it. IDs below 0 are skipped.
/// \param resolver The resolver to query.
/// \param path The scene path to resolve.
/// \param primIds Receives the IDs.
/// \return False when the path is neither an rprim nor has rprims under it.
bool AppendPrimIds(
    PrimIdResolver const& resolver, PXR_NS::SdfPath const& path, std::vector<int>* primIds);

/// Resolver backed by the rprims and instancers of the render index, which Storm populates.
class RenderIndexPrimIdResolver : public PrimIdResolver
{
public:
    /// Constructor.
    /// \param renderIndex The render index to resolve against. It must outlive the resolver.
    explicit RenderIndexPrimIdResolver(PXR_NS::HdRenderIndex& renderIndex);

    bool IsRprim(PXR_NS::SdfPath const& path) const override;
    int GetPrimId(PXR_NS::SdfPath const& rprimPath) const override;
    PXR_NS::SdfPathVector GetRprimsUnder(PXR_NS::SdfPath const& path) const override;
    PXR_NS::SdfPathVector GetInstancerChain(PXR_NS::SdfPath const& rprimPath) const override;

    /// Returns the rprim index version. Every primitive-ID assignment happens inside an rprim
    /// insertion, which bumps it: SetPrimId() is reached only from HdRenderIndex::_AllocatePrimId
    /// and _CompactPrimIds, and both only from _InsertRprim. See docs/outline.md for the chain and
    /// the OpenUSD versions checked.
    unsigned GetPrimIdsVersion() const override;

    /// Returns the instancer index version.
    unsigned GetInstancersVersion() const override;

private:
    PXR_NS::HdRenderIndex* _renderIndex;
};

/// Returns the resolver for the render delegate of a render index.
/// \param renderIndex The render index to resolve against. It must outlive the resolver.
std::unique_ptr<PrimIdResolver> CreatePrimIdResolver(PXR_NS::HdRenderIndex& renderIndex);

} // namespace HVT_NS::Outline
