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

#include <pxr/base/tf/token.h>

namespace HVT_NS::Outline
{

// The Flash outline tags protocol, spelled here and nowhere else. Flash's headers are not
// available to HVT, so these mirror Flash's tokens (core/tokens.h) and its decoding of the render
// setting (api/renderDelegate.cpp, _DecodeOutlineTargets). See "Render delegates" in
// docs/outline.md.
//
// - The "hdFlash:outlineTargets" render setting lists the outline targets: a VtDictionary of
//   parallel arrays, "paths" and "bits" one per target, "levelCounts" one per target,
//   "levelInstancers" and "indexCounts" one per instance level, and "indices". Flash tags the rows
//   each target keeps with its bits, before it uploads them, in the frame the setting is pushed.
// - A pass whose primId AOV binding sets "hdFlash:outlineTags" writes to the primId AOV the owner
//   prim ID of the rows (a GeomSubset reads as its mesh), with kFlashPrimIdHoverFlag for the rows
//   with the hover bit, and, when the instanceId AOV is bound, their tags to it: (instance << 4) |
//   8 | bucket bits for a row of an rprim a target with instance levels covers, instance being 0
//   when a level-less drawing target keeps it, and the bucket bits for any other row.
// - "hdFlash:outlineDrawOnly", on the same binding, also restricts the pass to the rows a drawing
//   target keeps.

inline PXR_NS::TfToken const& FlashOutlineTargetsToken()
{
    static PXR_NS::TfToken const token { "hdFlash:outlineTargets", PXR_NS::TfToken::Immortal };
    return token;
}

inline PXR_NS::TfToken const& FlashOutlineTagsToken()
{
    static PXR_NS::TfToken const token { "hdFlash:outlineTags", PXR_NS::TfToken::Immortal };
    return token;
}

inline PXR_NS::TfToken const& FlashOutlineDrawOnlyToken()
{
    static PXR_NS::TfToken const token { "hdFlash:outlineDrawOnly", PXR_NS::TfToken::Immortal };
    return token;
}

// The bits of a target in the render setting: its bucket bits, which Flash writes to the instanceId
// AOV as they are, and kFlashTargetDraw when draw-only passes draw what it keeps.
constexpr int kFlashTargetSelected = 1;
constexpr int kFlashTargetLead     = 2;
constexpr int kFlashTargetHover    = 4;
constexpr int kFlashTargetDraw     = 8;

// The bit of the primId values that Flash sets for the rows with kFlashTargetHover (Flash's
// kOutlinePrimIdHostFlag), on primIds below it only. outlineMask.glslfx strips it from every
// primId it reads in the outline tags variant: keep the two in step.
constexpr int kFlashPrimIdHoverFlag = 1 << 30;

} // namespace HVT_NS::Outline
