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

#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>

#include <map>
#include <variant>

namespace HVT_NS
{

/// Hold necessary information for creating matcap materials.
struct MatcapCreationParams
{
    std::string shaderFilePath {};
    std::string textureFilePath {};
    PXR_NS::SdfPath materialPath {};
};

/// Can hold parameters for different types of materials.
using StockMaterialParams = std::variant<MatcapCreationParams>;

/// \brief Creates a material based on the given material creation parameters.
/// Dispatches on \p params (currently only \ref MatcapCreationParams is supported).
/// Additional material types can be added as further alternatives in \ref StockMaterialParams.
/// \param materialCreationParams The material creation parameters.
/// \return On success, a \c VtValue holding an \c HdMaterialNetworkMap for the surface terminal.
///         On failure (invalid parameters, missing files, invalid shader), an empty \c VtValue.
HVT_API PXR_NS::VtValue CreateStockMaterial(
    StockMaterialParams const& materialCreationParams);

} // namespace HVT_NS
