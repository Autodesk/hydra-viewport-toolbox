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

#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>

#include <algorithm>
#include <vector>

namespace HVT_NS::Outline
{

// Header-only value types: no HVT_API, as for every inline type (see AGENTS.md).

/// The instances of one instancer that an OutlineTarget keeps.
///
/// \c instanceIndices are the instancer-wide indices: the values listed in the instancer's
/// \c instancerTopology.instanceIndices, which index its per-instance primvars. They are not
/// positions in a per-prototype list, the convention HdxSelectionSceneIndexObserver uses for
/// HdInstanceIndicesSchema.
struct OutlineInstanceLevel
{
    /// The instancer, as a render index path.
    PXR_NS::SdfPath instancer;

    /// The kept instances. An empty array keeps none.
    PXR_NS::VtIntArray instanceIndices;

    bool operator==(OutlineInstanceLevel const& other) const
    {
        return instancer == other.instancer && instanceIndices == other.instanceIndices;
    }

    bool operator!=(OutlineInstanceLevel const& other) const { return !(*this == other); }
};

/// A selection target: a subtree, optionally restricted to some instances.
///
/// With no \c instanceLevels, a target is the same as a path in OutlineInputs::selectedPaths: the
/// whole subtree under \c path. Each level restricts the target to the listed instances of its
/// instancer, which draws the rprims directly or through nested instancers. Levels combine as an
/// intersection, so their order does not matter; list each instancer at most once. An rprim under
/// \c path that one of the listed instancers does not draw is not part of the target.
///
/// Example: instance 3 of the point instancer /Root/PI is
/// \code
/// OutlineTarget { SdfPath("/Root/PI"), { { SdfPath("/Root/PI"), VtIntArray { 3 } } } }
/// \endcode
struct OutlineTarget
{
    /// The root of the subtree, as a render index path.
    PXR_NS::SdfPath path;

    /// The instance restrictions, one per instancer. Empty for the whole subtree.
    std::vector<OutlineInstanceLevel> instanceLevels;

    bool operator==(OutlineTarget const& other) const
    {
        return path == other.path && instanceLevels == other.instanceLevels;
    }

    bool operator!=(OutlineTarget const& other) const { return !(*this == other); }
};

using OutlineTargets = std::vector<OutlineTarget>;

/// Returns true when some target is restricted to instances (has instance levels).
inline bool HasInstanceLevels(OutlineTargets const& targets)
{
    return std::any_of(targets.begin(), targets.end(),
        [](OutlineTarget const& target) { return !target.instanceLevels.empty(); });
}

} // namespace HVT_NS::Outline
