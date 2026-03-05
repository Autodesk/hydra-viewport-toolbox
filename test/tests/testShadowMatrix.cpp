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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4305)
#endif
// clang-format on

#include <pxr/pxr.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/glf/simpleLight.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

// shadowMatrixComputation.h is a private header in source/shadow/.
// The test target includes source/ in its include path.
#include "shadow/shadowMatrixComputation.h"

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

GlfSimpleLight MakeDirectionalLight(GfVec3f const& direction)
{
    GlfSimpleLight light;
    light.SetPosition(GfVec4f(direction[0], direction[1], direction[2], 0.0f));
    light.SetSpotDirection(direction);
    return light;
}

GlfSimpleLight MakePointLight(GfVec3f const& position)
{
    GlfSimpleLight light;
    light.SetPosition(GfVec4f(position[0], position[1], position[2], 1.0f));
    return light;
}

} // anonymous namespace

// ===========================================================================
// ShadowMatrixComputation -- construction and Compute
// ===========================================================================

TEST(TestShadowMatrix, Compute_ReturnsOneMatrix)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto light = MakeDirectionalLight(GfVec3f(0, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);

    auto matrices = smc.Compute();
    ASSERT_EQ(matrices.size(), 1u);
    EXPECT_NE(matrices[0], GfMatrix4d(0.0));
}

TEST(TestShadowMatrix, Compute_WithViewport_ReturnsOneMatrix)
{
    GfRange3f worldBox(GfVec3f(-5, -5, -5), GfVec3f(5, 5, 5));
    auto light = MakeDirectionalLight(GfVec3f(0, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);

    // All Compute overloads should return the same matrix for the same inputs,
    // since viewport/framing/policy are currently unused by the implementation.
    auto baseMatrices = smc.Compute();
    ASSERT_EQ(baseMatrices.size(), 1u);

    GfVec4f viewport(0, 0, 800, 600);
    auto matrices = smc.Compute(viewport, CameraUtilFit);
    ASSERT_EQ(matrices.size(), 1u);
    EXPECT_NE(matrices[0], GfMatrix4d(0.0));
    EXPECT_EQ(matrices[0], baseMatrices[0]);
}

TEST(TestShadowMatrix, Compute_WithFraming_ReturnsOneMatrix)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto light = MakeDirectionalLight(GfVec3f(1, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);

    auto baseMatrices = smc.Compute();
    ASSERT_EQ(baseMatrices.size(), 1u);

    CameraUtilFraming framing;
    auto matrices = smc.Compute(framing, CameraUtilFit);
    ASSERT_EQ(matrices.size(), 1u);
    EXPECT_NE(matrices[0], GfMatrix4d(0.0));
    EXPECT_EQ(matrices[0], baseMatrices[0]);
}

// ===========================================================================
// ShadowMatrixComputation -- update and dirty tracking
// ===========================================================================

TEST(TestShadowMatrix, Update_SameValues_NotDirty)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto light = MakeDirectionalLight(GfVec3f(0, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);

    // Compute clears the dirty flag.
    smc.Compute();

    // Updating with the same values should not mark dirty.
    EXPECT_FALSE(smc.update(worldBox, light));
}

TEST(TestShadowMatrix, Update_DifferentWorldBox_MarksDirty)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto light = MakeDirectionalLight(GfVec3f(0, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);
    smc.Compute();

    GfRange3f newBox(GfVec3f(-2, -2, -2), GfVec3f(2, 2, 2));
    EXPECT_TRUE(smc.update(newBox));
}

TEST(TestShadowMatrix, Update_DifferentLight_MarksDirty)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto light = MakeDirectionalLight(GfVec3f(0, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);
    smc.Compute();

    auto newLight = MakeDirectionalLight(GfVec3f(1, -1, 0));
    EXPECT_TRUE(smc.update(newLight));
}

TEST(TestShadowMatrix, Update_Recompute_ClearsDirty)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto light = MakeDirectionalLight(GfVec3f(0, -1, 0));

    hvt::ShadowMatrixComputation smc(worldBox, light);
    smc.Compute();

    GfRange3f newBox(GfVec3f(-3, -3, -3), GfVec3f(3, 3, 3));
    smc.update(newBox);

    // After recompute, same values should not be dirty.
    smc.Compute();
    EXPECT_FALSE(smc.update(newBox));
}

// ===========================================================================
// ShadowMatrixComputation -- point light vs directional light
// ===========================================================================

TEST(TestShadowMatrix, PointLight_ProducesDifferentMatrix)
{
    GfRange3f worldBox(GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1));
    auto directional = MakeDirectionalLight(GfVec3f(0, -1, 0));
    auto point       = MakePointLight(GfVec3f(0, 5, 0));

    hvt::ShadowMatrixComputation smcDir(worldBox, directional);
    hvt::ShadowMatrixComputation smcPt(worldBox, point);

    auto matDir = smcDir.Compute();
    auto matPt  = smcPt.Compute();

    ASSERT_EQ(matDir.size(), 1u);
    ASSERT_EQ(matPt.size(), 1u);
    EXPECT_NE(matDir[0], matPt[0]);
}
