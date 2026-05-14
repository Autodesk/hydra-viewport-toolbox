//
// Copyright 2026 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <hvt/material/material.h>
#include <hvt/tasks/resources.h>

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <gtest/gtest.h>

#include <RenderingFramework/TestFlags.h>

#include <algorithm>
#include <filesystem>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// RAII guard that saves and restores the resource directory across tests,
// since hvt::SetResourceDirectory mutates a file-static variable.
struct ResourceDirGuard
{
    ResourceDirGuard() : _saved(hvt::GetResourceDirectory()) {}
    ~ResourceDirGuard() { hvt::SetResourceDirectory(_saved); }

private:
    std::filesystem::path _saved;
};

// Builds a fully-valid set of matcap creation parameters that tests can mutate
// to exercise a single failure mode at a time.
hvt::MaterialCreationParams MakeValidMatcapParams()
{
    return { "matcap",
        {
            { "shaderFilePath", VtValue(hvt::GetShaderPath("matcap.glslfx").string()) },
            { "textureFilePath", VtValue(hvt::GetShaderPath("matcap.png").string()) },
            { "materialPath", VtValue(SdfPath("/matcap")) },
        } };
}

} // namespace

// =====================================================================
// CreateGLSLMaterial — error paths
// =====================================================================

HVT_TEST(TestMaterial, UnknownType_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.type = "invalid"; // not a registered type

    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, MissingParameter_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters.erase("shaderFilePath");

    // at() will throw inside the function
    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, WrongParameterType_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters["shaderFilePath"] = VtValue(42);
    
    // not a string
    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, EmptyShaderPath_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters["shaderFilePath"] = VtValue(std::string {});

    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, EmptyTexturePath_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters["textureFilePath"] = VtValue(std::string {});

    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, EmptyMaterialPath_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters["materialPath"] = VtValue(SdfPath {});

    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, NonExistentShaderFile_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters["shaderFilePath"] = VtValue(
        std::string("/no/such/file.glslfx"));

    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

HVT_TEST(TestMaterial, NonExistentTextureFile_ReturnsEmpty)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    auto params = MakeValidMatcapParams();
    params.parameters["textureFilePath"] = VtValue(
        std::string("/no/such/file.png"));

    const VtValue v = hvt::CreateGLSLMaterial(params);
    EXPECT_TRUE(v.IsEmpty());
}

// =====================================================================
// CreateGLSLMaterial — happy path
// =====================================================================

HVT_TEST(TestMaterial, BuildsExpectedNetworkMap)
{
    ResourceDirGuard guard;
    hvt::SetResourceDirectory(TOSTRING(HVT_RESOURCE_PATH));

    const SdfPath materialPath("/matcap");
    const VtValue v = hvt::CreateGLSLMaterial({ "matcap",
        {
            { "shaderFilePath", VtValue(hvt::GetShaderPath("matcap.glslfx").string()) },
            { "textureFilePath", VtValue(hvt::GetShaderPath("matcap.png").string()) },
            { "materialPath", VtValue(materialPath) },
        } });

    ASSERT_TRUE(v.IsHolding<HdMaterialNetworkMap>());
    const auto& nm = v.UncheckedGet<HdMaterialNetworkMap>();

    // Surface terminal exists and points at the material path.
    ASSERT_EQ(nm.map.count(HdMaterialTerminalTokens->surface), 1u);
    ASSERT_EQ(nm.terminals.size(), 1u);
    EXPECT_EQ(nm.terminals.front(), materialPath);

    const auto& network = nm.map.at(HdMaterialTerminalTokens->surface);

    // Shader node is present at materialPath.
    auto shaderIt = std::find_if(network.nodes.begin(), network.nodes.end(),
        [&](const auto& n) { return n.path == materialPath; });
    ASSERT_NE(shaderIt, network.nodes.end());

    // At least one UsdUVTexture child node carrying the texture file path.
    auto texIt = std::find_if(network.nodes.begin(), network.nodes.end(),
        [&](const auto& n) { return n.identifier == TfToken("UsdUVTexture"); });
    ASSERT_NE(texIt, network.nodes.end());
    EXPECT_EQ(texIt->path.GetParentPath(), materialPath);
    const auto& fileParam = texIt->parameters.at(TfToken("file"));
    EXPECT_TRUE(fileParam.IsHolding<std::string>());
    EXPECT_NE(fileParam.Get<std::string>().find("matcap.png"), std::string::npos);

    // Texture node is wired into the shader node.
    ASSERT_FALSE(network.relationships.empty());
    EXPECT_EQ(network.relationships.front().outputId, materialPath);
    EXPECT_EQ(network.relationships.front().inputId, texIt->path);
}
