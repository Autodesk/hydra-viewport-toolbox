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
#include <hvt/tasks/resources.h>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>
#include <RenderingFramework/TestHelpers.h>

#include <algorithm>
#include <filesystem>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// Builds a fully-valid set of matcap creation parameters that tests can mutate
// to exercise a single failure mode at a time.
hvt::MatcapCreationParams _MakeValidMatcapParams()
{
    const auto resourceDir = TestHelpers::getPublicResourceFolder();
    const auto shaderPath  = resourceDir / "shaders" / "matcap.glslfx";
    const auto texturePath = resourceDir / "shaders" / "matcap.png";

    hvt::MatcapCreationParams params;
    params.materialPath     = SdfPath("/matcap");
    params.shaderFilePath   = shaderPath.string();
    params.textureFilePath  = texturePath.string();
    params.textureInputName = TfToken("matcap");
    return params;
}

} // namespace

// =====================================================================
// CreateMaterial — error paths
// =====================================================================

HVT_TEST(TestMaterial, EmptyShaderPath_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    params.shaderFilePath.clear();

    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, EmptyTexturePath_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    params.textureFilePath.clear();

    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, EmptyMaterialPath_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    params.materialPath = {};

    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, NonExistentShaderFile_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    params.shaderFilePath = "/no/such/file.glslfx";

    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, NonExistentTextureFile_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    params.textureFilePath = "/no/such/file.png";

    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, NonGlslfxShaderFile_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    // matcap.png exists but is not a valid .glslfx; SdrRegistry must reject it.
    params.shaderFilePath =
        (TestHelpers::getPublicResourceFolder() / "shaders" / "matcap.png").string();
    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, UnknownTextureInputName_ReturnsEmpty)
{
    auto params = _MakeValidMatcapParams();
    params.textureInputName = TfToken("nonexistent");

    const VtValue v = hvt::CreateMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

// =====================================================================
// CreateMaterial — happy path
// =====================================================================

HVT_TEST(TestMaterial, BuildsExpectedNetworkMap_FromDefaultParams)
{
    // Need to save and override the resource directory to ensure the 
    // shader and texture are found in GetDefaultMatcapCreationParams
    const auto savedResourceDir = hvt::GetResourceDirectory();
    hvt::SetResourceDirectory(TestHelpers::getPublicResourceFolder());

    auto params = hvt::GetDefaultMatcapCreationParams();
    params.materialPath = SdfPath("/matcap");
    const VtValue v = hvt::CreateMaterial(params);

    hvt::SetResourceDirectory(savedResourceDir);

    ASSERT_TRUE(v.IsHolding<HdMaterialNetworkMap>());
    auto const& nm = v.UncheckedGet<HdMaterialNetworkMap>();
    ASSERT_EQ(nm.terminals.size(), 1u);
    EXPECT_EQ(nm.terminals.front(), params.materialPath);
}

HVT_TEST(TestMaterial, BuildsExpectedNetworkMap)
{
    const SdfPath materialPath("/matcap");
    auto params = _MakeValidMatcapParams();
    params.materialPath = materialPath;

    const VtValue v = hvt::CreateMaterial(params);

    ASSERT_TRUE(v.IsHolding<HdMaterialNetworkMap>());
    auto const& nm = v.UncheckedGet<HdMaterialNetworkMap>();

    // Surface terminal exists and points at the material path.
    ASSERT_EQ(nm.map.count(HdMaterialTerminalTokens->surface), 1u);
    ASSERT_EQ(nm.terminals.size(), 1u);
    EXPECT_EQ(nm.terminals.front(), materialPath);

    auto const& network = nm.map.at(HdMaterialTerminalTokens->surface);

    // Shader node is present at materialPath.
    auto shaderIt = std::find_if(network.nodes.begin(), network.nodes.end(),
        [&](auto const& n) { return n.path == materialPath; });
    ASSERT_NE(shaderIt, network.nodes.end());

    // At least one UsdUVTexture child node carrying the texture file path.
    auto texIt = std::find_if(network.nodes.begin(), network.nodes.end(),
        [&](auto const& n) { return n.identifier == TfToken("UsdUVTexture"); });
    ASSERT_NE(texIt, network.nodes.end());
    EXPECT_EQ(texIt->path.GetParentPath(), materialPath);
    auto const& fileParam = texIt->parameters.at(TfToken("file"));
    EXPECT_TRUE(fileParam.IsHolding<std::string>());
    EXPECT_NE(fileParam.Get<std::string>().find("matcap.png"), std::string::npos);

    // Texture node is wired into the shader's matcap input.
    ASSERT_FALSE(network.relationships.empty());
    EXPECT_EQ(network.relationships.front().outputId, materialPath);
    EXPECT_EQ(network.relationships.front().outputName, TfToken("matcap"));
    EXPECT_EQ(network.relationships.front().inputId, texIt->path);
    EXPECT_EQ(texIt->path, materialPath.AppendChild(TfToken("matcap")));
}
