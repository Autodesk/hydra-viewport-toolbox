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
#pragma once

#include <hvt/api.h>

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif _MSC_VER
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/declarePtrs.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

namespace hvt
{

class BoundingBoxSceneIndex;
using BoundingBoxSceneIndexRefPtr      = PXR_NS::TfRefPtr<BoundingBoxSceneIndex>;
using BoundingBoxSceneIndexConstRefPtr = PXR_NS::TfRefPtr<const BoundingBoxSceneIndex>;

/// \class BoundingBoxSceneIndex
///
/// A filtering scene index that converts geometries into a bounding box using the extent attribute.
/// \note If the extent attribute is not present, it does not draw anything for that prim.
///
class HVT_API BoundingBoxSceneIndex : public PXR_NS::HdSingleInputFilteringSceneIndexBase
{
public:
    static BoundingBoxSceneIndexRefPtr New(const PXR_NS::HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return PXR_NS::TfCreateRefPtr(new BoundingBoxSceneIndex(inputSceneIndex));
    }

    /// \name From PXR_NS::HdSceneIndexBase
    /// @{

    PXR_NS::HdSceneIndexPrim GetPrim(const PXR_NS::SdfPath& primPath) const override;

    PXR_NS::SdfPathVector GetChildPrimPaths(const PXR_NS::SdfPath& primPath) const override;

    /// @}

protected:
    explicit BoundingBoxSceneIndex(const PXR_NS::HdSceneIndexBaseRefPtr& inputSceneIndex);
    ~BoundingBoxSceneIndex() override = default;

    /// \name From PXR_NS::HdSingleInputFilteringSceneIndexBase
    /// @{

    void _PrimsAdded(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::AddedPrimEntries& entries) override;

    void _PrimsRemoved(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    void _PrimsDirtied(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

    /// @}

private:
    /// Excludes or not a prim from the bounding box.
    /// \param primPath The prim to validate.
    /// \return True if the prim is excluded.
    virtual bool _IsExcluded(const PXR_NS::SdfPath& /*primPath*/) const { return false; }

    /// Gets the color of the bounding box lines.
    virtual PXR_NS::GfVec4f _GetColor() const { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
};

} // namespace hvt
