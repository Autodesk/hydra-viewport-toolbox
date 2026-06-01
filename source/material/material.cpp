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

#include <hvt/material/material.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/glslfx.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <filesystem>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((usdUVTexture, "UsdUVTexture"))
    ((fallback, "fallback"))
    ((file, "file"))
    ((rgb, "rgb")));

VtValue _CreateMatcapMaterial(MatcapCreationParams const& matcapCreationParams)
{
    if (!std::filesystem::is_regular_file(matcapCreationParams.shaderFilePath))
    {
        TF_RUNTIME_ERROR(
            "Shader file not found: %s",
            matcapCreationParams.shaderFilePath.c_str());
        return VtValue();
    }
    if (!std::filesystem::is_regular_file(matcapCreationParams.textureFilePath))
    {
        TF_RUNTIME_ERROR(
            "Texture file not found: %s",
            matcapCreationParams.textureFilePath.c_str());
        return VtValue();
    }

    // Create GLSLFX based shader node
    SdrShaderNodeConstPtr sdrNode = SdrRegistry::GetInstance().GetShaderNodeFromAsset(
        SdfAssetPath(matcapCreationParams.shaderFilePath),
        SdrTokenMap(),
        TfToken(),
        HioGlslfxTokens->glslfx);
    if (!sdrNode || !sdrNode->IsValid())
    {
        return VtValue();
    }

    // Build Hydra Material
    HdMaterialNetworkMap networkMap;
    HdMaterialNetwork network;
    HdMaterialNode node;
    node.identifier = sdrNode->GetIdentifier();
    node.path       = matcapCreationParams.materialPath;

    auto const input =
        sdrNode->GetShaderInput(matcapCreationParams.textureInputName);
    if (!input || input->GetType() != SdrPropertyTypes->Color)
    {
        TF_RUNTIME_ERROR(
            "MatCap shader is missing required Color input '%s'",
            matcapCreationParams.textureInputName.GetText());
        return VtValue();
    }

    static GfVec3f const kFallbackColor(0.4f, 0.4f, 0.4f);
    TfToken const inputName = matcapCreationParams.textureInputName;

    node.parameters[inputName] = VtValue(kFallbackColor);

    HdMaterialNode textureNode;
    textureNode.path       = node.path.AppendChild(inputName);
    textureNode.identifier = _tokens->usdUVTexture;
    textureNode.parameters[_tokens->fallback] = VtValue(kFallbackColor);
    textureNode.parameters[_tokens->file] =
        VtValue(matcapCreationParams.textureFilePath);

    HdMaterialRelationship rel;
    rel.inputId    = textureNode.path;
    rel.inputName  = _tokens->rgb;
    rel.outputId   = node.path;
    rel.outputName = inputName;
    network.relationships.emplace_back(std::move(rel));
    network.nodes.emplace_back(std::move(textureNode));

    networkMap.terminals.emplace_back(node.path);
    network.nodes.emplace_back(std::move(node));
    networkMap.map.insert(
        { HdMaterialTerminalTokens->surface, std::move(network) });
    return VtValue(networkMap);
}

VtValue _CreateMaterial(MatcapCreationParams const& parameters)
{
    if (parameters.shaderFilePath.empty()
        || parameters.textureFilePath.empty()
        || parameters.materialPath.IsEmpty())
    {
        TF_RUNTIME_ERROR("Invalid matcap creation parameters");
        return VtValue();
    }
    return _CreateMatcapMaterial(parameters);
}

} // namespace

VtValue CreateStockMaterial(StockMaterialParams const& params)
{
    return std::visit(
        [](auto const& concrete) { return _CreateMaterial(concrete); },
        params);
}

} // namespace HVT_NS
