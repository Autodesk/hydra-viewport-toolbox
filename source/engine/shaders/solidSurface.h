#pragma once

namespace hvt::ViewportEngine
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

} // namespace hvt::ViewportEngine
