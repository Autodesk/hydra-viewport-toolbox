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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-W#pragma-messages"
#if __clang_major__ > 11
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#else
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#endif
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4127)
#pragma warning(disable : 4100)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>

#include <map>
#include <string_view>

namespace HVT_NS
{

struct MaterialCreationParams
{
    std::string_view type {};
    std::map<std::string_view, PXR_NS::VtValue> parameters {};
};

struct MatcapCreationParams
{
    std::string_view shaderFilePath {};
    std::string_view textureFilePath {};
    PXR_NS::SdfPath materialPath {};
};

/// \brief Creates a GLSLFX material based on the given material creation parameters.
/// \param materialCreationParams The material creation parameters.
/// \return Returns a Hydra-facing material payload for the given material creation parameters.
HVT_API PXR_NS::VtValue CreateGLSLMaterial(
    const MaterialCreationParams& materialCreationParams);

/// \brief Creates a matcap material based on the given material creation parameters.
/// \param matcapCreationParams The matcap creation parameters.
/// \return Returns a Hydra-facing material payload for the given matcap creation parameters.
PXR_NS::VtValue CreateMatcapMaterial(
    const MatcapCreationParams& matcapCreationParams);

} // namespace HVT_NS