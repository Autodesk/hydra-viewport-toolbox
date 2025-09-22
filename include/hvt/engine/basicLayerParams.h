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
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4267)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

// OpenUSD headers.
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/repr.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hgi/enums.h>
#include <pxr/imaging/hgiPresent/interopHandle.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <memory>
#include <string>

namespace HVT_NS
{

/// Default display mode token.
static const PXR_NS::TfToken defaultReprToken { PXR_NS::HdReprTokens->smoothHull };

/// Default viewport dimensions when the viewport is unused.
/// \note The viewport mechanism is deprecated and was replaced by framing.
static const PXR_NS::GfVec4d kDefaultViewport { 0, 0, 1, 1 };

/// Default layer color space.
/// \note Using pxr::HdxColorCorrectionTokens->sRGB would require linking with lib\usd_hdx.lib.
static const PXR_NS::TfToken kDefaultColorspace("sRGB");

/// Contains basic layer parameters. These parameters can be consulted by
/// the various tasks used to render the scene.
struct HVT_API BasicLayerParams
{
    /// Common render tasks settings.
    PXR_NS::HdxRenderTaskParams renderParams;

    /// The color correction mode.
    PXR_NS::TfToken colorspace = kDefaultColorspace;

    /// Enable (or not) the hdxPresentTask (i.e., not yet supported for Metal).
    bool enablePresentation { true };
    PXR_NS::HgiCompareFunction depthCompare { PXR_NS::HgiCompareFunctionLEqual };
    PXR_NS::HgiPresentInteropHandle presentDestination;


    /// The render tags to control what is rendered.
    PXR_NS::TfTokenVector renderTags { PXR_NS::HdRenderTagTokens->geometry,
        PXR_NS::HdRenderTagTokens->render, PXR_NS::HdRenderTagTokens->guide,
        PXR_NS::HdRenderTagTokens->proxy };

    /// Defines the representation (i.e., repr) for the geometry.
    PXR_NS::HdRprimCollection collection = PXR_NS::HdRprimCollection(
        PXR_NS::HdTokens->geometry, PXR_NS::HdReprSelector(defaultReprToken));

    /// Defines the render buffer size.
    PXR_NS::GfVec2i renderBufferSize;

    /// The AOV buffer ID to visualize (color or depth).
    PXR_NS::TfToken visualizeAOV;

    /// Enable selection is on by default.
    bool enableSelection { true };

    /// Enable outline is off by default.
    bool enableOutline { false };

    /// When enableSelection on selections objects are highlighted as a different color.
    /// The selectionColor is used to tint selected objects.
    PXR_NS::GfVec4f selectionColor { 1.0f, 1.0f, 0.0f, 1.0f };
    /// The locateColor is used to tint rollover objects.
    PXR_NS::GfVec4f locateColor { 1.0f, 1.0f, 0.0f, 1.0f };
};

} // namespace HVT_NS
