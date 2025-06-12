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
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/declarePtrs.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

class BoundingBoxSceneIndex;
using BoundingBoxSceneIndexRefPtr      = PXR_NS::TfRefPtr<BoundingBoxSceneIndex>;
using BoundingBoxSceneIndexConstRefPtr = PXR_NS::TfRefPtr<const BoundingBoxSceneIndex>;

/// \class BoundingBoxSceneIndex
///
/// A filtering scene index that converts geometries into a bounding box using the extent attribute.
/// \note If the extent attribute is not present, it does not draw anything for that prim.
///
/// NOTE: We have found that putting the export symbol (HVT_API) at the class level causes a
/// build failure with certain OpenUSD versions, on subclasses of
/// HdSingleInputFilteringSceneIndexBase. To avoid this, we specify the export symbol on the public
/// functions.
class BoundingBoxSceneIndex : public PXR_NS::HdSingleInputFilteringSceneIndexBase
{
public:
    HVT_API
    static BoundingBoxSceneIndexRefPtr New(const PXR_NS::HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return PXR_NS::TfCreateRefPtr(new BoundingBoxSceneIndex(inputSceneIndex));
    }

    /// \name From PXR_NS::HdSceneIndexBase
    /// @{

    HVT_API
    PXR_NS::HdSceneIndexPrim GetPrim(const PXR_NS::SdfPath& primPath) const override;

    HVT_API
    PXR_NS::SdfPathVector GetChildPrimPaths(const PXR_NS::SdfPath& primPath) const override;

    /// @}

protected:
    HVT_API
    explicit BoundingBoxSceneIndex(const PXR_NS::HdSceneIndexBaseRefPtr& inputSceneIndex);

    HVT_API
    ~BoundingBoxSceneIndex() override = default;

    /// \name From PXR_NS::HdSingleInputFilteringSceneIndexBase
    /// @{

    HVT_API
    void _PrimsAdded(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::AddedPrimEntries& entries) override;

    HVT_API
    void _PrimsRemoved(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    HVT_API
    void _PrimsDirtied(const PXR_NS::HdSceneIndexBase& sender,
        const PXR_NS::HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

    /// @}

private:
    /// Excludes or not a prim from the bounding box.
    /// \param primPath The prim to validate.
    /// \return True if the prim is excluded.
    HVT_API
    virtual bool _IsExcluded(const PXR_NS::SdfPath& /*primPath*/) const { return false; }

    /// Gets the color of the bounding box lines.
    HVT_API
    virtual PXR_NS::GfVec4f _GetColor() const { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
};

} // namespace HVT_NS
