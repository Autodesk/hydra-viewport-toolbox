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
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <memory>

namespace HVT_NS
{

/// \class SelectionDelegate
///
/// Abstract interface for managing selection state in a scene.
/// This interface decouples HVT from specific imaging implementations (e.g., UsdImaging)
/// by providing a generic API for adding and clearing selections.
///
class SelectionDelegate
{
public:
    virtual ~SelectionDelegate() = default;

    /// Add a prim path to the selection set.
    /// \param path The scene path to mark as selected.
    virtual void AddSelection(const PXR_NS::SdfPath& path) = 0;

    /// Clear all selections.
    virtual void ClearSelection() = 0;
};

using SelectionDelegateSharedPtr = std::shared_ptr<SelectionDelegate>;

} // namespace HVT_NS

