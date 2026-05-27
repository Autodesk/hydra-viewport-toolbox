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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#endif
// clang-format on

#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/glslfx.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <filesystem>

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{
    
PXR_NS::VtValue CreateMatcapMaterial(const MatcapCreationParams& matcapCreationParams)
{
    if (!std::filesystem::is_regular_file(
        matcapCreationParams.shaderFilePath))
    {
        TF_RUNTIME_ERROR("Shader file not found: %s",
            std::string(matcapCreationParams.shaderFilePath).c_str());
    
        return VtValue();
    }
    if (!std::filesystem::is_regular_file(
        matcapCreationParams.textureFilePath))
    {
        TF_RUNTIME_ERROR("Texture file not found: %s", 
            std::string(matcapCreationParams.textureFilePath).c_str());
        return VtValue();
    }

    // Create GLSLFX based shader node
    SdrShaderNodeConstPtr sdrNode = SdrRegistry::GetInstance()
        .GetShaderNodeFromAsset(
            SdfAssetPath(std::string(matcapCreationParams.shaderFilePath)),
            SdrTokenMap(), TfToken(), HioGlslfxTokens->glslfx);
    if (!sdrNode || !sdrNode->IsValid())
    {
        return VtValue();
    }

    // Build Hydra Material
    PXR_NS::HdMaterialNetworkMap networkMap;
    PXR_NS::HdMaterialNetwork network;
    PXR_NS::HdMaterialNode node;
    node.identifier = sdrNode->GetIdentifier();
    node.path       = matcapCreationParams.materialPath;

    // Build network based on inputs
    for (const auto& inputName : sdrNode->GetShaderInputNames())
    {
        if (auto input = sdrNode->GetShaderInput(inputName))
        {
            // Handle more types if needed
            if (input->GetType() == SdrPropertyTypes->Color)
            {
                static const GfVec3f kFallbackColor = GfVec3f(0.4f, 0.4f, 0.4f);
                // Create a shader input for the texture
                node.parameters[inputName] = VtValue(kFallbackColor);

                // Create a texture node with the texture file
                HdMaterialNode textureNode;
                textureNode.path                            = node.path.AppendChild(inputName);
                textureNode.identifier                      = TfToken("UsdUVTexture");
                textureNode.parameters[TfToken("fallback")] = VtValue(kFallbackColor);
                textureNode.parameters[TfToken("file")] =
                    VtValue(std::string(matcapCreationParams.textureFilePath));

                // Connect the texture node to the shader input
                HdMaterialRelationship rel;
                rel.inputId    = textureNode.path;
                rel.inputName  = TfToken("rgb");
                rel.outputId   = node.path;
                rel.outputName = inputName;
                network.relationships.emplace_back(std::move(rel));
                network.nodes.emplace_back(std::move(textureNode));
            }
        }
    }

    networkMap.terminals.emplace_back(node.path);
    network.nodes.emplace_back(std::move(node));
    networkMap.map.insert(
        { HdMaterialTerminalTokens->surface, std::move(network) });
    return VtValue(networkMap);
}

PXR_NS::VtValue dispatch(const MatcapCreationParams& parameters)
{
    if (parameters.shaderFilePath.empty()
        || parameters.textureFilePath.empty()
        || parameters.materialPath.IsEmpty()) {
        TF_RUNTIME_ERROR("Invalid matcap creation parameters");
        return PXR_NS::VtValue();
    }
    return CreateMatcapMaterial(parameters);
}

}

PXR_NS::VtValue CreateStockMaterial(const StockMaterialParams& params)
{
    return std::visit(
        [](const auto& concrete) { return dispatch(concrete); },
        params);
}

} // namespace HVT_NS