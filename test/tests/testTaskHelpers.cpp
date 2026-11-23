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
#include <hvt/tasks/flashPickTask.h>
#include <hvt/tasks/fxaaTask.h>
#include <hvt/tasks/kernelPostProcessingEffect.h>
#include <hvt/tasks/resources.h>
#include <hvt/tasks/ssaoTask.h>
#if !defined(ADSK_OPENUSD_PENDING)
    #include <hvt/tasks/visualizeAovTask.h>
#endif

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>

#include <gtest/gtest.h>

#include <limits>

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
// VisualizeAovTaskParams operators (custom HVT implementation only; skipped when using OpenUSD
// pending)
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

// ===========================================================================
// Task GetMargin (KernelPostProcessingEffect)
// ===========================================================================

namespace
{

// Returns SSAO parameters enabling the effect with the given sample radius.
hvt::SSAOTaskParams MakeSSAOParams(float sampleRadius, bool isDenoiseEnabled)
{
    hvt::SSAOTaskParams params;
    params.ao.isEnabled        = true;
    params.ao.sampleRadius     = sampleRadius;
    params.ao.isDenoiseEnabled = isDenoiseEnabled;
    return params;
}

// Returns a margin context for a 1000 x 1000 image, seen through the given frustum.
hvt::KernelPostProcessingEffect::MarginContext MakeMarginContext(GfFrustum const& frustum)
{
    hvt::KernelPostProcessingEffect::MarginContext context;
    context.imageResolution  = GfVec2i(1000, 1000);
    context.projectionMatrix = frustum.ComputeProjectionMatrix();
    context.viewMatrix       = frustum.ComputeViewMatrix();
    return context;
}

// Returns a perspective frustum with a 90 degree field of view and a near plane at 1, so
// P[1][1] = 1 and a 1000 pixel high image has 500 pixels per scene unit at the near plane.
GfFrustum MakePerspectiveFrustum()
{
    GfFrustum frustum;
    frustum.SetPerspective(90.0, 1.0, 1.0, 100.0);
    return frustum;
}

// Returns an orthographic frustum 20 units high, so a 1000 pixel high image has 50 pixels per
// scene unit at any depth.
GfFrustum MakeOrthographicFrustum()
{
    GfFrustum frustum;
    frustum.SetOrthographic(-10.0, 10.0, -10.0, 10.0, 1.0, 100.0);
    return frustum;
}

} // anonymous namespace

TEST(TestTask, SSAOTask_GetMargin_DisabledByDefault)
{
    // The default parameters disable the effect, so no margin is needed whatever the view.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));

    const auto context = MakeMarginContext(MakePerspectiveFrustum());
    EXPECT_EQ(task.GetMargin(context, VtValue()), 0);
    EXPECT_EQ(task.GetMargin(context, VtValue(hvt::SSAOTaskParams())), 0);
}

TEST(TestTask, SSAOTask_GetMargin_NoSampleRadius)
{
    // An enabled effect that does not look for occluders needs no margin.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));

    const auto context = MakeMarginContext(MakePerspectiveFrustum());
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(0.0f, true))), 0);
}

TEST(TestTask, SSAOTask_GetMargin_Perspective)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    const auto context = MakeMarginContext(MakePerspectiveFrustum());

    // The raw pass reach at the near plane: 2.001 units * 500 pixels per unit / 1 = 1000.5,
    // rounded up to 1001 pixels. The radius keeps the reach away from an integer value, which
    // the rounding would make sensitive to floating-point errors.
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 1001);

    // The denoise blur passes add 8 pixels.
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, true))), 1009);
}

TEST(TestTask, SSAOTask_GetMargin_PerspectiveWorldExtent)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    hvt::KernelPostProcessingEffect::MarginContext context =
        MakeMarginContext(MakePerspectiveFrustum());

    // The camera is at the origin looking down -Z, so the nearest point of these bounds is at a
    // depth of 4: 2.001 units * 500 pixels per unit / 4 = 250.125, rounded up to 251 pixels.
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -10.0), GfVec3d(1.0, 1.0, -4.0));
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 251);
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, true))), 259);

    // Bounds reaching closer than the near plane, or behind the camera, fall back to the near
    // plane.
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -10.0), GfVec3d(1.0, 1.0, 5.0));
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 1001);
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, 2.0), GfVec3d(1.0, 1.0, 5.0));
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 1001);

    // The bounds are transformed into the view: moving the camera back by 2 units moves the
    // nearest point to a depth of 6: 2.001 * 500 / 6 = 166.75, rounded up to 167 pixels.
    context.viewMatrix  = GfMatrix4d().SetTranslate(GfVec3d(0.0, 0.0, -2.0));
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -10.0), GfVec3d(1.0, 1.0, -4.0));
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 167);
}

TEST(TestTask, SSAOTask_GetMargin_Orthographic)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    const auto context = MakeMarginContext(MakeOrthographicFrustum());

    // The raw pass reach does not depend on the depth: 2.001 units * 50 pixels per unit = 100.05,
    // rounded up to 101 pixels.
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 101);
    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, true))), 109);
}

TEST(TestTask, SSAOTask_GetMargin_OrthographicIgnoresWorldExtent)
{
    // An orthographic reach does not depend on the depth, so the scene bounds do not change it.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    hvt::KernelPostProcessingEffect::MarginContext context =
        MakeMarginContext(MakeOrthographicFrustum());
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -50.0), GfVec3d(1.0, 1.0, -40.0));

    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 101);
}

TEST(TestTask, SSAOTask_GetMargin_ScreenSampleRadius)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));

    // A screen-relative radius is a fraction of the image width, whatever the view:
    // 0.0626 * 1920 = 120.19, rounded up to 121 pixels.
    hvt::KernelPostProcessingEffect::MarginContext context =
        MakeMarginContext(MakePerspectiveFrustum());
    context.imageResolution = GfVec2i(1920, 1080);

    hvt::SSAOTaskParams params     = MakeSSAOParams(0.0626f, false);
    params.ao.isScreenSampleRadius = true;
    EXPECT_EQ(task.GetMargin(context, VtValue(params)), 121);

    context                    = MakeMarginContext(MakeOrthographicFrustum());
    context.imageResolution    = GfVec2i(1920, 1080);
    params.ao.isDenoiseEnabled = true;
    EXPECT_EQ(task.GetMargin(context, VtValue(params)), 129);
}

TEST(TestTask, SSAOTask_GetMargin_SaturatesHugeReaches)
{
    // A huge reach saturates instead of overflowing into a negative margin.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    const auto context = MakeMarginContext(MakePerspectiveFrustum());

    EXPECT_EQ(task.GetMargin(context, VtValue(MakeSSAOParams(1e30f, true))),
        std::numeric_limits<int>::max());

    hvt::SSAOTaskParams params     = MakeSSAOParams(1e30f, false);
    params.ao.isScreenSampleRadius = true;
    EXPECT_EQ(task.GetMargin(context, VtValue(params)), std::numeric_limits<int>::max());
}

TEST(TestTask, BlurTask_GetMargin_MatchesBlurAmount)
{
    hvt::BlurTask task(nullptr, SdfPath("/testBlurTask"));
    const auto context = MakeMarginContext(MakePerspectiveFrustum());

    // Without parameters, the synced ones are used: the default blur amount 0.5 needs 1 pixel.
    EXPECT_EQ(task.GetMargin(context, VtValue()), 1);

    // The blur reach is the blur amount, rounded up, whatever the view.
    hvt::BlurTaskParams params;
    params.blurAmount = 8.0f;
    EXPECT_EQ(task.GetMargin(context, VtValue(params)), 8);
    params.blurAmount = 2.3f;
    EXPECT_EQ(task.GetMargin(context, VtValue(params)), 3);
    EXPECT_EQ(task.GetMargin(MakeMarginContext(MakeOrthographicFrustum()), VtValue(params)), 3);
}

// ===========================================================================
// FlashPickTaskParams
// ===========================================================================

TEST(TestTask, FlashPickTaskParams_DefaultValues)
{
    hvt::FlashPickTaskParams params;

    EXPECT_EQ(params.cullStyle, HdCullStyleNothing);
    EXPECT_TRUE(params.renderTags.empty());
    EXPECT_TRUE(params.cameraId.IsEmpty());
    EXPECT_FALSE(params.overrideWindowPolicy.has_value());
}

TEST(TestTask, FlashPickTaskParams_Equality_Defaults)
{
    hvt::FlashPickTaskParams a;
    hvt::FlashPickTaskParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(TestTask, FlashPickTaskParams_Inequality_DifferentCullStyle)
{
    hvt::FlashPickTaskParams a;
    hvt::FlashPickTaskParams b;
    b.cullStyle = HdCullStyleBack;
    EXPECT_NE(a, b);
}

TEST(TestTask, FlashPickTaskParams_Inequality_DifferentRenderTags)
{
    hvt::FlashPickTaskParams a;
    hvt::FlashPickTaskParams b;
    b.renderTags = { TfToken("geometry"), TfToken("guide") };
    EXPECT_NE(a, b);
}

TEST(TestTask, FlashPickTaskParams_Inequality_DifferentCameraId)
{
    hvt::FlashPickTaskParams a;
    hvt::FlashPickTaskParams b;
    b.cameraId = SdfPath("/cameras/main");
    EXPECT_NE(a, b);
}

TEST(TestTask, FlashPickTaskParams_Inequality_DifferentOverridePolicy)
{
    hvt::FlashPickTaskParams a;
    hvt::FlashPickTaskParams b;
    b.overrideWindowPolicy = CameraUtilMatchVertically;
    EXPECT_NE(a, b);
}

TEST(TestTask, FlashPickTaskToken)
{
    const auto& token = hvt::FlashPickTask::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "flashPickTask");
}
