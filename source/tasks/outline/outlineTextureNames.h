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

#include <string>

namespace HVT_NS::Outline
{

// Single source of truth for the outline AOV texture task-context names.
//
// OutlinePrimIdsTask publishes these textures into the task context and OutlineManager wires the
// mask task's input texture names to the same strings, so producer and consumer must agree
// exactly. Both derive every name through these helpers, and the "outline" / "PrimIdsTexture" /
// "DepthTexture" spellings live here and nowhere else -- so the two sides cannot silently drift.
//
// Names are "outline<prefix>PrimIdsTexture" / "outline<prefix>DepthTexture", where prefix is one
// of "Base" / "Overlay" / "Default", e.g. OutlinePrimIdsTextureName("Base") ->
// "outlineBasePrimIdsTexture".

inline std::string OutlinePrimIdsTextureName(std::string const& prefix)
{
    return "outline" + prefix + "PrimIdsTexture";
}

inline std::string OutlineDepthTextureName(std::string const& prefix)
{
    return "outline" + prefix + "DepthTexture";
}

// Task-context key under which OutlineMaskTask publishes its composited mask texture and
// OutlineOverlayTask reads it. This is the task-to-task handoff name -- distinct from the
// identically-spelled GLSL writable-texture binding in outlineMask.glslfx, which is the
// shader-source contract and must stay coupled to the shader, not to this header.
inline std::string OutlineMaskTextureName()
{
    return "outlineMaskTexture";
}

// Token form of OutlineMaskTextureName(), for the per-frame task-context lookups in
// OutlineMaskTask::Execute() (publish) and OutlineOverlayTask::Execute() (consume). Interned once
// rather than per call, and immortal so the registry entry outlives static destruction -- matching
// every other TfToken static in these tasks.
inline PXR_NS::TfToken const& OutlineMaskTextureToken()
{
    static PXR_NS::TfToken const token { OutlineMaskTextureName(), PXR_NS::TfToken::Immortal };
    return token;
}

} // namespace HVT_NS::Outline
