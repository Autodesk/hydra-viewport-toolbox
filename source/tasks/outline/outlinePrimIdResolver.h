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
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderDelegate;
class HdRenderIndex;
PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS::Outline
{

/// Returns true when the render delegate is Storm.
/// \param renderDelegate The render delegate to test.
bool IsStormRenderer(PXR_NS::HdRenderDelegate* renderDelegate);

/// Returns true when the render delegate is Flash.
/// \param renderDelegate The render delegate to test.
bool IsFlashRenderer(PXR_NS::HdRenderDelegate* renderDelegate);

/// Returns true when the outline tasks support the render delegate: Storm and Flash. The tasks
/// disable themselves on any other render delegate.
/// \param renderDelegate The render delegate to test.
bool IsOutlineSupportedRenderer(PXR_NS::HdRenderDelegate* renderDelegate);

/// What the outline tasks need to know about the prims a render delegate draws: the primitive IDs
/// its ID passes write, and the instancers drawing each rprim. A render delegate that does not
/// populate the render index with rprims answers these through another implementation.
class PrimIdResolver
{
public:
    virtual ~PrimIdResolver() = default;

    /// Brings the resolver up to date with its render delegate. Call it once per frame, from
    /// Prepare(), before any other query.
    virtual void Update() {}

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

    /// Returns the primitive-ID remap table, or an empty array when every primitive ID the ID
    /// passes write is already the one GetPrimId() returns for its path. A render delegate that
    /// writes several IDs for one path (Flash GeomSubsets) folds them onto that ID through it.
    /// Layout, all int32: [0] first remapped ID F, [1] ID count N, [2 + id - F] = the ID to
    /// compare id as, for F <= id < F + N; IDs outside that range are left as they are.
    /// It changes only when GetPrimIdsVersion() does.
    virtual PXR_NS::VtIntArray const& GetPrimIdRemap() const;
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

/// Resolver for Flash, which inserts no rprims or instancers into the render index. Primitive IDs
/// come from Flash's "hdFlash:primIdToPath" render setting, which maps every ID Flash writes to
/// the path a pick reports for it: a GeomSubset ID maps to its mesh path. The IDs of one path are
/// folded onto the lowest of them (GetPrimId(), GetPrimIdRemap()), so that the subsets of a mesh
/// outline as one prim. Instancer chains come from the terminal scene index.
class FlashPrimIdResolver : public PrimIdResolver
{
public:
    /// Constructor.
    /// \param renderIndex The render index of a Flash render delegate. It must outlive the
    /// resolver.
    explicit FlashPrimIdResolver(PXR_NS::HdRenderIndex& renderIndex);

    /// Rebuilds the path -> ID map when the primIdToPath table has changed. Flash never reuses an
    /// ID and only appends to the table, and a removal only blanks an entry, so the table changes
    /// in a way that matters only when its size or storage does. A blanked entry is left mapped:
    /// Flash draws no pixel with that ID any more.
    void Update() override;

    bool IsRprim(PXR_NS::SdfPath const& path) const override;
    int GetPrimId(PXR_NS::SdfPath const& rprimPath) const override;
    PXR_NS::SdfPathVector GetRprimsUnder(PXR_NS::SdfPath const& path) const override;

    /// Follows the first instancedBy path of the rprim, then of each instancer, as Flash
    /// flattens instances (at most 8 levels).
    PXR_NS::SdfPathVector GetInstancerChain(PXR_NS::SdfPath const& rprimPath) const override;

    /// Returns a version bumped by Update() whenever it rebuilds the map.
    unsigned GetPrimIdsVersion() const override;

    /// Returns GetPrimIdsVersion(). An instancer edit that assigns no ID is not detected.
    unsigned GetInstancersVersion() const override;

    PXR_NS::VtIntArray const& GetPrimIdRemap() const override;

private:
    PXR_NS::HdRenderIndex* _renderIndex;

    /// Path -> its lowest primitive ID. Sorted, so that a path finds the rprims under it.
    std::map<PXR_NS::SdfPath, int> _primIds;
    PXR_NS::VtIntArray _primIdRemap;

    /// The table the map was built from, to detect a change.
    size_t _tableSize { 0 };
    PXR_NS::SdfPath const* _tableData { nullptr };
    unsigned _version { 0 };
};

/// Returns the resolver for the render delegate of a render index.
/// \param renderIndex The render index to resolve against. It must outlive the resolver.
std::unique_ptr<PrimIdResolver> CreatePrimIdResolver(PXR_NS::HdRenderIndex& renderIndex);

} // namespace HVT_NS::Outline
