//
// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#pragma once

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
    explicit UsdImagingSelectionDelegate(PXR_NS::UsdImagingSelectionSceneIndexRefPtr selectionSceneIndex)
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

    /// Get the underlying UsdImagingSelectionSceneIndex (for internal use).
    PXR_NS::UsdImagingSelectionSceneIndexRefPtr GetSelectionSceneIndex() const
    {
        return _selectionSceneIndex;
    }

private:
    PXR_NS::UsdImagingSelectionSceneIndexRefPtr _selectionSceneIndex;
};

} // namespace HVT_NS


