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
#include <hvt/tasks/pickViewParams.h>
#include <hvt/tasks/screenSpaceKernelEffect.h>
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

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range2f.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdx/tokens.h>

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

// A frame without an AOV buffer must not leave the previous frame's handles in the task context:
// they can dangle once the task that owns the texture destroys it.
TEST(TestTask, AovInputTask_Execute_WithoutAovBuffer_ClearsStaleContextEntries)
{
    hvt::AovInputTask task(nullptr, SdfPath("/aovInput"));

    HdTaskContext ctx;
    ctx[HdAovTokens->color]              = VtValue(HgiTextureHandle());
    ctx[HdAovTokens->depth]              = VtValue(HgiTextureHandle());
    ctx[HdAovTokens->Neye]               = VtValue(HgiTextureHandle());
    ctx[HdxAovTokens->colorIntermediate] = VtValue(HgiTextureHandle());

    task.Execute(&ctx);

    EXPECT_EQ(ctx.count(HdAovTokens->color), 0u);
    EXPECT_EQ(ctx.count(HdAovTokens->depth), 0u);
    EXPECT_EQ(ctx.count(HdAovTokens->Neye), 0u);
    EXPECT_EQ(ctx.count(HdxAovTokens->colorIntermediate), 0u);
    EXPECT_TRUE(task.IsConverged());
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
// Task ComputeRequiredMargin (ScreenSpaceKernelEffect)
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
hvt::ScreenSpaceKernelEffect::MarginContext MakeMarginContext(GfFrustum const& frustum)
{
    hvt::ScreenSpaceKernelEffect::MarginContext context;
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

TEST(TestTask, SSAOTask_ComputeRequiredMargin_DisabledByDefault)
{
    // The default parameters disable the effect, so no margin is needed whatever the view.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));

    const auto context = MakeMarginContext(MakePerspectiveFrustum());
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue()), 0);
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(hvt::SSAOTaskParams())), 0);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_NoSampleRadius)
{
    // An enabled effect that does not look for occluders needs no margin.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));

    const auto context = MakeMarginContext(MakePerspectiveFrustum());
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(0.0f, true))), 0);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_Perspective)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    const auto context = MakeMarginContext(MakePerspectiveFrustum());

    // The raw pass reach at the near plane: 2.001 units * 500 pixels per unit / 1 = 1000.5,
    // rounded up to 1001 pixels. The radius keeps the reach away from an integer value, which
    // the rounding would make sensitive to floating-point errors.
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 1001);

    // The denoise blur passes add 8 pixels.
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, true))), 1009);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_ConformsToTheImageAspect)
{
    // The 1:1 frustum is conformed to the image aspect ratio with the window policy, as the
    // render does, which can change the number of pixels per scene unit.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    hvt::ScreenSpaceKernelEffect::MarginContext context =
        MakeMarginContext(MakePerspectiveFrustum());
    const VtValue params(MakeSSAOParams(2.0001f, false));

    // A 2000 x 1000 image: fitting or matching vertically keeps P[1][1] = 1, so 500 pixels per
    // unit: 2.0001 * 500 = 1000.05, rounded up to 1001 pixels.
    context.imageResolution = GfVec2i(2000, 1000);
    context.windowPolicy    = CameraUtilFit;
    EXPECT_EQ(task.ComputeRequiredMargin(context, params), 1001);
    context.windowPolicy = CameraUtilMatchVertically;
    EXPECT_EQ(task.ComputeRequiredMargin(context, params), 1001);

    // Cropping or matching horizontally halves the vertical extent, so P[1][1] = 2 and 1000
    // pixels per unit: 2.0001 * 1000 = 2000.1, rounded up to 2001 pixels.
    context.windowPolicy = CameraUtilCrop;
    EXPECT_EQ(task.ComputeRequiredMargin(context, params), 2001);
    context.windowPolicy = CameraUtilMatchHorizontally;
    EXPECT_EQ(task.ComputeRequiredMargin(context, params), 2001);

    // A 1000 x 2000 image: fitting doubles the vertical extent, so P[1][1] = 0.5 and 500 pixels
    // per unit, instead of the 1000 the unconformed projection gives.
    context.imageResolution = GfVec2i(1000, 2000);
    context.windowPolicy    = CameraUtilFit;
    EXPECT_EQ(task.ComputeRequiredMargin(context, params), 1001);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_PerspectiveWorldExtent)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    hvt::ScreenSpaceKernelEffect::MarginContext context =
        MakeMarginContext(MakePerspectiveFrustum());

    // The camera is at the origin looking down -Z, so the nearest point of these bounds is at a
    // depth of 4: 2.001 units * 500 pixels per unit / 4 = 250.125, rounded up to 251 pixels.
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -10.0), GfVec3d(1.0, 1.0, -4.0));
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 251);
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, true))), 259);

    // Bounds reaching closer than the near plane, or behind the camera, fall back to the near
    // plane.
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -10.0), GfVec3d(1.0, 1.0, 5.0));
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 1001);
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, 2.0), GfVec3d(1.0, 1.0, 5.0));
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 1001);

    // The bounds are transformed into the view: moving the camera back by 2 units moves the
    // nearest point to a depth of 6: 2.001 * 500 / 6 = 166.75, rounded up to 167 pixels.
    context.viewMatrix  = GfMatrix4d().SetTranslate(GfVec3d(0.0, 0.0, -2.0));
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -10.0), GfVec3d(1.0, 1.0, -4.0));
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 167);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_Orthographic)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    const auto context = MakeMarginContext(MakeOrthographicFrustum());

    // The raw pass reach does not depend on the depth: 2.001 units * 50 pixels per unit = 100.05,
    // rounded up to 101 pixels.
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 101);
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, true))), 109);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_OrthographicIgnoresWorldExtent)
{
    // An orthographic reach does not depend on the depth, so the scene bounds do not change it.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    hvt::ScreenSpaceKernelEffect::MarginContext context =
        MakeMarginContext(MakeOrthographicFrustum());
    context.worldExtent = GfRange3d(GfVec3d(-1.0, -1.0, -50.0), GfVec3d(1.0, 1.0, -40.0));

    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(2.001f, false))), 101);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_ScreenSampleRadius)
{
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));

    // A screen-relative radius is a fraction of the image width, whatever the view:
    // 0.0626 * 1920 = 120.19, rounded up to 121 pixels.
    hvt::ScreenSpaceKernelEffect::MarginContext context =
        MakeMarginContext(MakePerspectiveFrustum());
    context.imageResolution = GfVec2i(1920, 1080);

    hvt::SSAOTaskParams params     = MakeSSAOParams(0.0626f, false);
    params.ao.isScreenSampleRadius = true;
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(params)), 121);

    context                    = MakeMarginContext(MakeOrthographicFrustum());
    context.imageResolution    = GfVec2i(1920, 1080);
    params.ao.isDenoiseEnabled = true;
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(params)), 129);
}

TEST(TestTask, SSAOTask_ComputeRequiredMargin_SaturatesHugeReaches)
{
    // A huge reach saturates instead of overflowing into a negative margin.
    hvt::SSAOTask task(nullptr, SdfPath("/testSSAOTask"));
    const auto context = MakeMarginContext(MakePerspectiveFrustum());

    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(MakeSSAOParams(1e30f, true))),
        std::numeric_limits<int>::max());

    hvt::SSAOTaskParams params     = MakeSSAOParams(1e30f, false);
    params.ao.isScreenSampleRadius = true;
    EXPECT_EQ(
        task.ComputeRequiredMargin(context, VtValue(params)), std::numeric_limits<int>::max());
}

TEST(TestTask, BlurTask_ComputeRequiredMargin_MatchesBlurAmount)
{
    hvt::BlurTask task(nullptr, SdfPath("/testBlurTask"));
    const auto context = MakeMarginContext(MakePerspectiveFrustum());

    // Without parameters, the synced ones are used: the default blur amount 0.5 needs 1 pixel.
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue()), 1);

    // The blur reach is the blur amount, rounded up, whatever the view.
    hvt::BlurTaskParams params;
    params.blurAmount = 8.0f;
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(params)), 8);
    params.blurAmount = 2.3f;
    EXPECT_EQ(task.ComputeRequiredMargin(context, VtValue(params)), 3);
    EXPECT_EQ(
        task.ComputeRequiredMargin(MakeMarginContext(MakeOrthographicFrustum()), VtValue(params)),
        3);
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

// ===========================================================================
// PickViewParams
// ===========================================================================

TEST(TestTask, PickViewParams_DefaultValues)
{
    hvt::PickViewParams params;

    EXPECT_TRUE(params.cameraId.IsEmpty());
    EXPECT_FALSE(params.framing.IsValid());
    EXPECT_FALSE(params.overrideWindowPolicy.has_value());
    EXPECT_TRUE(params.pickRegion.IsEmpty());
}

TEST(TestTask, PickViewParams_Equality)
{
    hvt::PickViewParams a;
    hvt::PickViewParams b;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);

    b.cameraId = SdfPath("/cameras/top");
    EXPECT_NE(a, b);

    b = a;
    b.framing = CameraUtilFraming(GfRect2i(GfVec2i(0, 0), 640, 480));
    EXPECT_NE(a, b);

    b = a;
    b.overrideWindowPolicy = CameraUtilDontConform;
    EXPECT_NE(a, b);

    b = a;
    b.pickRegion = GfRect2i(GfVec2i(10, 20), 4, 4);
    EXPECT_NE(a, b);
}

TEST(TestTask, PickViewParamsToken)
{
    const auto& token = hvt::PickViewParams::GetToken();
    EXPECT_FALSE(token.IsEmpty());
    EXPECT_EQ(token.GetString(), "hvtPickViewParams");
}

namespace
{

// NDC of a pixel (y axis pointing down) of a framing whose data window is the whole view.
GfVec3d _PixelToNdc(GfVec2d const& pixel, GfRect2i const& dataWindow, double ndcZ)
{
    return GfVec3d((pixel[0] - dataWindow.GetMinX()) / dataWindow.GetWidth() * 2.0 - 1.0,
        1.0 - (pixel[1] - dataWindow.GetMinY()) / dataWindow.GetHeight() * 2.0, ndcZ);
}

// Checks that the pick region framing of a view gives the part of the view's projection that
// covers the pick region.
void _CheckPickRegionFraming(GfMatrix4d const& cameraProjection, GfRange2f const& displayWindow)
{
    const GfRect2i viewDataWindow(GfVec2i(0, 0), 640, 480);

    hvt::PickViewParams view;
    view.framing              = CameraUtilFraming(displayWindow, viewDataWindow);
    view.overrideWindowPolicy = CameraUtilDontConform;
    view.pickRegion           = GfRect2i(GfVec2i(100, 300), 8, 6);

    const CameraUtilFraming pickFraming = hvt::GetPickRegionFraming(view);

    // The buffer holds the pick region only.
    EXPECT_EQ(pickFraming.dataWindow, GfRect2i(GfVec2i(0, 0), 8, 6));
    EXPECT_EQ(pickFraming.displayWindow.GetSize(), displayWindow.GetSize());

    const GfMatrix4d viewProj =
        view.framing.ApplyToProjectionMatrix(cameraProjection, CameraUtilDontConform);
    const GfMatrix4d pickProj =
        pickFraming.ApplyToProjectionMatrix(cameraProjection, CameraUtilDontConform);

    // Same projection as the view framing restricted to the pick region, not moved.
    const GfMatrix4d unmovedProj = CameraUtilFraming(displayWindow, view.pickRegion)
                                       .ApplyToProjectionMatrix(cameraProjection,
                                           CameraUtilDontConform);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_NEAR(pickProj[r][c], unmovedProj[r][c], 1e-9);
        }
    }

    // A point drawn at a pixel of the view lands at the same place in the pick region: the
    // region's center at the NDC origin, its top left corner at (-1, 1).
    const GfMatrix4d viewNdcToEye = viewProj.GetInverse();
    const GfVec2d regionMin(view.pickRegion.GetMinX(), view.pickRegion.GetMinY());
    const GfVec2d regionCenter =
        regionMin + 0.5 * GfVec2d(view.pickRegion.GetWidth(), view.pickRegion.GetHeight());

    const GfVec3d centerEye =
        viewNdcToEye.Transform(_PixelToNdc(regionCenter, viewDataWindow, 0.5));
    const GfVec3d centerNdc = pickProj.Transform(centerEye);
    EXPECT_NEAR(centerNdc[0], 0.0, 1e-6);
    EXPECT_NEAR(centerNdc[1], 0.0, 1e-6);

    const GfVec3d cornerEye = viewNdcToEye.Transform(_PixelToNdc(regionMin, viewDataWindow, 0.5));
    const GfVec3d cornerNdc = pickProj.Transform(cornerEye);
    EXPECT_NEAR(cornerNdc[0], -1.0, 1e-6);
    EXPECT_NEAR(cornerNdc[1], 1.0, 1e-6);
}

} // anonymous namespace

TEST(TestTask, GetPickRegionFraming_Perspective)
{
    GfFrustum frustum;
    frustum.SetPerspective(45.0, 640.0 / 480.0, 0.1, 1000.0);

    // A display window matching the view, then an off-center one (pan / zoom, overscan, film
    // offsets solved into the display window).
    _CheckPickRegionFraming(frustum.ComputeProjectionMatrix(),
        GfRange2f(GfVec2f(0.0f, 0.0f), GfVec2f(640.0f, 480.0f)));
    _CheckPickRegionFraming(frustum.ComputeProjectionMatrix(),
        GfRange2f(GfVec2f(-120.0f, 35.0f), GfVec2f(900.0f, 600.0f)));
}

TEST(TestTask, GetPickRegionFraming_Orthographic)
{
    GfFrustum frustum;
    frustum.SetOrthographic(-8.0, 8.0, -6.0, 6.0, 0.1, 2000.0);

    _CheckPickRegionFraming(frustum.ComputeProjectionMatrix(),
        GfRange2f(GfVec2f(0.0f, 0.0f), GfVec2f(640.0f, 480.0f)));
    _CheckPickRegionFraming(frustum.ComputeProjectionMatrix(),
        GfRange2f(GfVec2f(40.0f, -60.0f), GfVec2f(520.0f, 420.0f)));
}

TEST(TestTask, GetPickRegionFraming_DataWindowOrigin)
{
    GfFrustum frustum;
    frustum.SetPerspective(45.0, 640.0 / 480.0, 0.1, 1000.0);
    const GfMatrix4d cameraProjection = frustum.ComputeProjectionMatrix();

    const GfRange2f displayWindow(GfVec2f(0.0f, 0.0f), GfVec2f(640.0f, 480.0f));
    const GfRect2i wholeDataWindow(GfVec2i(0, 0), 640, 480);
    const GfRect2i renderRegion(GfVec2i(100, 50), 200, 100);

    // The same view drawn whole, then through a render region starting at (100, 50).
    hvt::PickViewParams wholeView;
    wholeView.framing              = CameraUtilFraming(displayWindow, wholeDataWindow);
    wholeView.overrideWindowPolicy = CameraUtilDontConform;
    wholeView.pickRegion           = GfRect2i(GfVec2i(150, 80), 8, 6);

    hvt::PickViewParams renderRegionView = wholeView;
    renderRegionView.framing             = CameraUtilFraming(displayWindow, renderRegion);

    // The pick region is in the framing's pixel coordinates, not relative to the data window:
    // the data window origin does not move it.
    const CameraUtilFraming wholePickFraming        = hvt::GetPickRegionFraming(wholeView);
    const CameraUtilFraming renderRegionPickFraming = hvt::GetPickRegionFraming(renderRegionView);
    EXPECT_EQ(renderRegionPickFraming, wholePickFraming);

    // And it projects the same pixels as the unmoved framing restricted to the pick region.
    const GfMatrix4d pickProj =
        renderRegionPickFraming.ApplyToProjectionMatrix(cameraProjection, CameraUtilDontConform);
    const GfMatrix4d unmovedProj = CameraUtilFraming(displayWindow, wholeView.pickRegion)
                                       .ApplyToProjectionMatrix(cameraProjection,
                                           CameraUtilDontConform);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_NEAR(pickProj[r][c], unmovedProj[r][c], 1e-9);
        }
    }
}

TEST(TestTask, GetPickRegionFraming_EmptyRegion)
{
    hvt::PickViewParams view;
    view.framing    = CameraUtilFraming(GfRect2i(GfVec2i(0, 0), 640, 480));
    view.pickRegion = GfRect2i(GfVec2i(10, 10), 0, 0);

    // FlashPickTask does not render an invalid framing.
    EXPECT_FALSE(hvt::GetPickRegionFraming(view).IsValid());
}
