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
#endif
// clang-format on

#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/declarePtrs.h>
#include <pxr/imaging/hd/filteringSceneIndex.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hvt
{

class WireFrameSceneIndex;
using WireFrameSceneIndexRefPtr      = PXR_NS::TfRefPtr<WireFrameSceneIndex>;
using WireFrameSceneIndexConstRefPtr = PXR_NS::TfRefPtr<const WireFrameSceneIndex>;

/// \class WireFrameSceneIndex
///
/// A scene index displaying a wireframe and using the display style for the color.
///
class HVT_API WireFrameSceneIndex : public PXR_NS::HdSingleInputFilteringSceneIndexBase
{
public:
    static WireFrameSceneIndexRefPtr New(const PXR_NS::HdSceneIndexBaseRefPtr& inputScene)
    {
        return PXR_NS::TfCreateRefPtr(new WireFrameSceneIndex(inputScene));
    }

    /// \name From PXR_NS::HdSceneIndexBase
    /// @{
    PXR_NS::HdSceneIndexPrim GetPrim(const PXR_NS::SdfPath& primPath) const override;

    PXR_NS::SdfPathVector GetChildPrimPaths(const PXR_NS::SdfPath& primPath) const override;

    /// @}

protected:
    explicit WireFrameSceneIndex(const PXR_NS::HdSceneIndexBaseRefPtr& inputScene);
    ~WireFrameSceneIndex() override = default;

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
    /// Excludes or not a prim from the wireframe.
    /// \param primPath The prim to validate.
    /// \return True if the prim is excluded.
    virtual bool _IsExcluded(const PXR_NS::SdfPath& /*primPath*/) const { return false; }

    /// Gets the color of the wireframe.
    virtual PXR_NS::GfVec4f _GetColor() const { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
};

} // namespace hvt
