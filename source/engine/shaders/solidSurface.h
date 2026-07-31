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

namespace HVT_NS::ViewportEngine
{

// Shader to emit primvar color without any shading.
// clang-format off
static const char kSolidSurfaceShader[] = R"(-- glslfx version 0.1
-- configuration
{
   "metadata" : { "materialTag" : "defaultMaterialTag" },
   "techniques": {
       "default":
       {
           "surfaceShader": { "source" : ["Solid.Surface"] }
       }
   }
}
-- glsl Solid.Surface
vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
{
    return color;
}
)";
// clang-format on

} // namespace HVT_NS::ViewportEngine
