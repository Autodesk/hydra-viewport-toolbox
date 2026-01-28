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
#include <hvt/engine/selectionDelegate.h>
#include <pxr/usdImaging/usdImaging/selectionSceneIndex.h>

namespace HVT_NS
{

/// \class UsdImagingSelectionDelegate
///
/// Concrete implementation of SelectionDelegate that wraps a UsdImagingSelectionSceneIndex.
/// This allows the application to work with USD scene indices.
///
class UsdImagingSelectionDelegate : public SelectionDelegate
{
public:
    /// Construct with a UsdImagingSelectionSceneIndex.
    /// \param selectionSceneIndex The USD imaging selection scene index to wrap.
    explicit UsdImagingSelectionDelegate(PXR_NS::UsdImagingSelectionSceneIndexRefPtr& selectionSceneIndex)
        : _selectionSceneIndex(selectionSceneIndex)
    {
    }

    ~UsdImagingSelectionDelegate() override = default;

    /// Add a prim path to the selection set.
    void AddSelection(const PXR_NS::SdfPath& path) override
    {
        if (_selectionSceneIndex)
        {
            _selectionSceneIndex->AddSelection(path);
        }
    }

    /// Clear all selections.
    void ClearSelection() override
    {
        if (_selectionSceneIndex)
        {
            _selectionSceneIndex->ClearSelection();
        }
    }

private:
    PXR_NS::UsdImagingSelectionSceneIndexRefPtr _selectionSceneIndex;
};

} // namespace HVT_NS


