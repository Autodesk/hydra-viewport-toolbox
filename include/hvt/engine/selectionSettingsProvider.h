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
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/vec4f.h>
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

using SelectionSettingsProviderWeakPtr = std::weak_ptr<class SelectionSettingsProvider>;

/// Struct that holds the paths to the selection buffers.
struct HVT_API SelectionBufferPaths
{
    PXR_NS::SdfPath primIdBufferPath;
    PXR_NS::SdfPath instanceIdBufferPath;
    PXR_NS::SdfPath elementIdBufferPath;
    PXR_NS::SdfPath depthBufferPath;
};

/// Struct that contains selection settings used by multiple tasks.
struct HVT_API SelectionSettings
{
    unsigned int outlineRadius = 5;
    bool enableSelection       = true;
    bool enableOutline         = true;
    PXR_NS::GfVec4f locateColor    = { 0, 0, 1, 1 };
    PXR_NS::GfVec4f selectionColor = { 1, 1, 0, 1 };
};

/// Interface for accessing selection settings.
/// \note This interface is intended to be used by task commit functions.
class HVT_API SelectionSettingsProvider
{
public:
    virtual ~SelectionSettingsProvider() = default;

    /// Return paths to the selection buffers.
    virtual SelectionBufferPaths const& GetBufferPaths() const = 0;

    /// Return common selection task settings.
    virtual SelectionSettings const& GetSettings() const = 0;
};

} // namespace HVT_NS