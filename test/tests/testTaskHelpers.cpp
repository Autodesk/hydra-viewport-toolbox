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

#include <hvt/tasks/ambientOcclusion.h>
#include <hvt/tasks/aovInputTask.h>
#include <hvt/tasks/blurTask.h>
#include <hvt/tasks/composeTask.h>
#include <hvt/tasks/fxaaTask.h>
#include <hvt/tasks/resources.h>
#include <hvt/tasks/ssaoTask.h>
#if !defined(ADSK_OPENUSD_PENDING)
#include <hvt/tasks/visualizeAovTask.h>
#endif

#include <pxr/pxr.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ===========================================================================
// Resources
// ===========================================================================

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

} // anonymous namespace

TEST(TestTask, Resources_SetAndGetResourceDirectory)
{
    ResourceDirGuard guard;

    const std::filesystem::path dir("/tmp/test_resources");
    hvt::SetResourceDirectory(dir);
    EXPECT_EQ(hvt::GetResourceDirectory(), dir);
}

TEST(TestTask, Resources_GetShaderPath)
{
    ResourceDirGuard guard;

    const std::filesystem::path dir("/tmp/res");
    hvt::SetResourceDirectory(dir);

    auto shaderPath = hvt::GetShaderPath("myShader.glslfx");
    EXPECT_EQ(shaderPath, std::filesystem::path("/tmp/res/shaders/myShader.glslfx"));
}

TEST(TestTask, Resources_GetGizmoPath)
{
    ResourceDirGuard guard;

    const std::filesystem::path dir("/tmp/res");
    hvt::SetResourceDirectory(dir);

    auto gizmoPath = hvt::GetGizmoPath("arrow.usda");
    EXPECT_EQ(gizmoPath, std::filesystem::path("/tmp/res/gizmos/arrow.usda"));
}

TEST(TestTask, Resources_OverwriteResourceDirectory)
{
    ResourceDirGuard guard;

    hvt::SetResourceDirectory("/first");
    EXPECT_EQ(hvt::GetResourceDirectory(), std::filesystem::path("/first"));

    hvt::SetResourceDirectory("/second");
    EXPECT_EQ(hvt::GetResourceDirectory(), std::filesystem::path("/second"));
}

// ===========================================================================
// AmbientOcclusionProperties
// ===========================================================================

TEST(TestTask, AmbientOcclusion_DefaultValues)
{
    hvt::AmbientOcclusionProperties props;

    EXPECT_FALSE(props.isEnabled);
    EXPECT_FALSE(props.isShowOnlyEnabled);
    EXPECT_FLOAT_EQ(props.amount, 1.0f);
    EXPECT_FLOAT_EQ(props.sampleRadius, 1.0f);
    EXPECT_FALSE(props.isScreenSampleRadius);
    EXPECT_EQ(props.sampleCount, 8);
    EXPECT_TRUE(props.isDenoiseEnabled);
    EXPECT_FLOAT_EQ(props.denoiseEdgeSharpness, 1.0f);
}

TEST(TestTask, AmbientOcclusion_Equality_Defaults)
{
    hvt::AmbientOcclusionProperties a;
    hvt::AmbientOcclusionProperties b;
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, AmbientOcclusion_Inequality_DifferentEnabled)
{
    hvt::AmbientOcclusionProperties a;
    hvt::AmbientOcclusionProperties b;
    b.isEnabled = true;
    EXPECT_NE(a, b);
}

TEST(TestTask, AmbientOcclusion_Inequality_DifferentAmount)
{
    hvt::AmbientOcclusionProperties a;
    hvt::AmbientOcclusionProperties b;
    b.amount = 0.5f;
    EXPECT_NE(a, b);
}

TEST(TestTask, AmbientOcclusion_Inequality_DifferentSampleCount)
{
    hvt::AmbientOcclusionProperties a;
    hvt::AmbientOcclusionProperties b;
    b.sampleCount = 32;
    EXPECT_NE(a, b);
}

TEST(TestTask, AmbientOcclusion_Inequality_DifferentDenoise)
{
    hvt::AmbientOcclusionProperties a;
    hvt::AmbientOcclusionProperties b;
    b.isDenoiseEnabled = false;
    EXPECT_NE(a, b);
}

// ===========================================================================
// BlurTaskParams operators
// ===========================================================================

TEST(TestTask, BlurTaskParams_Equality_Defaults)
{
    hvt::BlurTaskParams a;
    hvt::BlurTaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, BlurTaskParams_Inequality_DifferentBlurAmount)
{
    hvt::BlurTaskParams a;
    hvt::BlurTaskParams b;
    b.blurAmount = 1.0f;
    EXPECT_TRUE(a != b);
    EXPECT_FALSE(a == b);
}

TEST(TestTask, BlurTaskParams_Inequality_DifferentAovName)
{
    hvt::BlurTaskParams a;
    hvt::BlurTaskParams b;
    b.aovName = HdAovTokens->depth;
    EXPECT_NE(a, b);
}

TEST(TestTask, BlurTaskParams_DefaultValues)
{
    hvt::BlurTaskParams params;
    EXPECT_FLOAT_EQ(params.blurAmount, 0.5f);
    EXPECT_EQ(params.aovName, HdAovTokens->color);
}

// ===========================================================================
// FXAATaskParams operators
// ===========================================================================

TEST(TestTask, FXAATaskParams_Equality_Defaults)
{
    hvt::FXAATaskParams a;
    hvt::FXAATaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, FXAATaskParams_Inequality_DifferentPixelToUV)
{
    hvt::FXAATaskParams a;
    hvt::FXAATaskParams b;
    b.pixelToUV = GfVec2f(0.005f, 0.005f);
    EXPECT_NE(a, b);
}

TEST(TestTask, FXAATaskParams_DefaultValues)
{
    hvt::FXAATaskParams params;
    EXPECT_EQ(params.pixelToUV, GfVec2f(0.01f, 0.01f));
}

// ===========================================================================
// VisualizeAovTaskParams operators (custom HVT implementation only; skipped when using OpenUSD pending)
// ===========================================================================
#if !defined(ADSK_OPENUSD_PENDING)

TEST(TestTask, VisualizeAovTaskParams_Equality_Defaults)
{
    hvt::VisualizeAovTaskParams a;
    hvt::VisualizeAovTaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, VisualizeAovTaskParams_Inequality_DifferentAovName)
{
    hvt::VisualizeAovTaskParams a;
    hvt::VisualizeAovTaskParams b;
    b.aovName = HdAovTokens->depth;
    EXPECT_NE(a, b);
}

TEST(TestTask, VisualizeAovTaskParams_Equality_SameNonDefault)
{
    hvt::VisualizeAovTaskParams a;
    hvt::VisualizeAovTaskParams b;
    a.aovName = HdAovTokens->depth;
    b.aovName = HdAovTokens->depth;
    EXPECT_EQ(a, b);
}

#endif // !ADSK_OPENUSD_PENDING

// ===========================================================================
// AovInputTaskParams operators
// ===========================================================================

TEST(TestTask, AovInputTaskParams_Equality_Defaults)
{
    hvt::AovInputTaskParams a;
    hvt::AovInputTaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, AovInputTaskParams_Inequality_DifferentAovBufferPath)
{
    hvt::AovInputTaskParams a;
    hvt::AovInputTaskParams b;
    b.aovBufferPath = SdfPath("/aov/color");
    EXPECT_NE(a, b);
}

TEST(TestTask, AovInputTaskParams_Inequality_DifferentDepthBufferPath)
{
    hvt::AovInputTaskParams a;
    hvt::AovInputTaskParams b;
    b.depthBufferPath = SdfPath("/aov/depth");
    EXPECT_NE(a, b);
}

TEST(TestTask, AovInputTaskParams_DefaultValues)
{
    hvt::AovInputTaskParams params;
    EXPECT_TRUE(params.aovBufferPath.IsEmpty());
    EXPECT_TRUE(params.depthBufferPath.IsEmpty());
    EXPECT_TRUE(params.neyeBufferPath.IsEmpty());
    EXPECT_EQ(params.aovBuffer, nullptr);
    EXPECT_EQ(params.depthBuffer, nullptr);
    EXPECT_EQ(params.neyeBuffer, nullptr);
}

// ===========================================================================
// ComposeTaskParams operators
// ===========================================================================

TEST(TestTask, ComposeTaskParams_Equality_Defaults)
{
    hvt::ComposeTaskParams a;
    hvt::ComposeTaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, ComposeTaskParams_Inequality_DifferentAovToken)
{
    hvt::ComposeTaskParams a;
    hvt::ComposeTaskParams b;
    b.aovToken = HdAovTokens->color;
    EXPECT_NE(a, b);
}

// ===========================================================================
// SSAOTaskParams / ViewProperties operators
// ===========================================================================

TEST(TestTask, SSAOTaskParams_Equality_SameValues)
{
    hvt::SSAOTaskParams a;
    hvt::SSAOTaskParams b;
    a.view.viewport = GfVec4d(0, 0, 800, 600);
    a.view.cameraID = SdfPath("/cam");
    b.view.viewport = GfVec4d(0, 0, 800, 600);
    b.view.cameraID = SdfPath("/cam");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, SSAOTaskParams_Inequality_DifferentAO)
{
    hvt::SSAOTaskParams a;
    hvt::SSAOTaskParams b;
    b.ao.isEnabled = true;
    EXPECT_NE(a, b);
}

TEST(TestTask, SSAOTaskParams_Inequality_DifferentView)
{
    hvt::SSAOTaskParams a;
    hvt::SSAOTaskParams b;
    b.view.cameraID = SdfPath("/camera");
    EXPECT_NE(a, b);
}

TEST(TestTask, ViewProperties_Equality_SameValues)
{
    hvt::ViewProperties a;
    hvt::ViewProperties b;
    a.viewport = GfVec4d(0, 0, 1920, 1080);
    a.cameraID = SdfPath("/cam");
    b.viewport = GfVec4d(0, 0, 1920, 1080);
    b.cameraID = SdfPath("/cam");
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, ViewProperties_Inequality_DifferentCameraID)
{
    hvt::ViewProperties a;
    hvt::ViewProperties b;
    b.cameraID = SdfPath("/myCamera");
    EXPECT_NE(a, b);
}

TEST(TestTask, ViewProperties_Inequality_DifferentViewport)
{
    hvt::ViewProperties a;
    hvt::ViewProperties b;
    b.viewport = GfVec4d(0, 0, 800, 600);
    EXPECT_NE(a, b);
}

// ===========================================================================
// Task GetToken() validation
// ===========================================================================

TEST(TestTask, BlurTaskToken)
{
    const auto& token = hvt::BlurTask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "blurTask");
}

TEST(TestTask, FXAATaskToken)
{
    const auto& token = hvt::FXAATask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "fxaaTask");
}

#if !defined(ADSK_OPENUSD_PENDING)
TEST(TestTask, VisualizeAovTaskToken)
{
    const auto& token = hvt::VisualizeAovTask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "visualizeAovTask");
}
#endif // !ADSK_OPENUSD_PENDING

TEST(TestTask, ComposeTaskToken)
{
    const auto& token = hvt::ComposeTask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "composeTask");
}

TEST(TestTask, SSAOTaskToken)
{
    const auto& token = hvt::SSAOTask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "ssaoTask");
}
