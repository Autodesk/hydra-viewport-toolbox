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
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>

#include <string>

namespace HVT_NS
{

/// Parameters for creating a matcap material.
/// All fields are required by \ref CreateMaterial. Use 
/// \ref GetDefaultMatcapCreationParams to obtain bundled HVT 
/// resource paths; set \p materialPath before calling \ref CreateMaterial.
struct MatcapCreationParams
{
    PXR_NS::SdfPath materialPath {};
    std::string     shaderFilePath {};
    std::string     textureFilePath {};
    /// Name of the shader input the texture is bound to.
    /// Must exist on the shader at \ref shaderFilePath and must be of type `Color`.
    PXR_NS::TfToken textureInputName {};
};

/// @brief Returns bundled HVT matcap resource paths for material creation.
///
/// Populates \c shaderFilePath (\c matcap.glslfx), 
/// \c textureFilePath (\c matcap.png), and \c textureInputName (\c matcap)
/// from HVT's internal resource tree.
/// \c materialPath is left empty; the caller must set it before calling
/// \ref CreateMaterial.
///
/// @return A \c MatcapCreationParams with bundled resource fields set and an
///         empty \c materialPath.
HVT_API MatcapCreationParams GetDefaultMatcapCreationParams();

/// @brief Creates a matcap Hydra material network from the given parameters.
///
/// Use \ref GetDefaultMatcapCreationParams to obtain bundled HVT resources, or
/// supply all fields in \p params explicitly.
///
/// @param params Fully specified matcap creation parameters. All fields must be
///               set, including \c materialPath.
/// @return On success, a \c VtValue holding an \c HdMaterialNetworkMap for the
///         surface terminal. On failure (invalid parameters, missing files, or
///         invalid shader), an empty \c VtValue.
HVT_API PXR_NS::VtValue CreateMaterial(MatcapCreationParams const& params);

} // namespace HVT_NS
