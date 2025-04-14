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
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/glf/simpleLightingContext.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <memory>

namespace hvt
{

using LightingSettingsProviderWeakPtr = std::weak_ptr<class LightingSettingsProvider>;

/// The LightingSettingsProvider class provides an interface for accessingvvarious lighting settings
/// used in the rendering context. This includes retrieving the lighting context, excluded lights,
/// and shadow settings.
///
/// \note This interface is intended to be used by task commit functions to ensure that the correct
/// lighting settings are applied during rendering.
class HVT_API LightingSettingsProvider
{
public:
    virtual ~LightingSettingsProvider() = default;

    /// Returns the lighting context.
    virtual const PXR_NS::GlfSimpleLightingContextRefPtr& GetLightingContext() const = 0;

    /// Returns the SdfPaths of excluded lights.
    virtual const PXR_NS::SdfPathVector& GetExcludedLights() const = 0;

    /// Returns whether shadows are enabled or not.
    virtual bool GetShadowsEnabled() const = 0;
};

} // namespace hvt