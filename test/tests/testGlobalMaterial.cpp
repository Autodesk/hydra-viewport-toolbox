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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskBackend.h>

#include <pxr/pxr.h>

#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hdx/tokens.h>

#include <gtest/gtest.h>

PXR_NAMESPACE_USING_DIRECTIVE

#if HVT_SI_TASK_BACKEND_SUPPORTED

namespace
{

// Contract with scene-index consumers that do not run HdxSimpleLightTask (e.g. Flash); must match
// the tokens in source/engine/si/lightingPrimSIBackend.cpp.
TfToken const kGlobalMaterialPrimType("glfGlobalMaterial");
TfToken const kGlobalMaterialName("globalMaterial");

class GlobalMaterialNoticeCounter : public HdSceneIndexObserver
{
public:
    explicit GlobalMaterialNoticeCounter(SdfPath const& path) : _path(path) {}

    int added { 0 };
    int dirtied { 0 };
    int removed { 0 };

    void PrimsAdded(HdSceneIndexBase const&, AddedPrimEntries const& entries) override
    {
        for (auto const& entry : entries)
        {
            if (entry.primPath == _path)
            {
                EXPECT_EQ(entry.primType, kGlobalMaterialPrimType);
                ++added;
            }
        }
    }

    void PrimsRemoved(HdSceneIndexBase const&, RemovedPrimEntries const& entries) override
    {
        for (auto const& entry : entries)
        {
            if (_path.HasPrefix(entry.primPath))
                ++removed;
        }
    }

    void PrimsDirtied(HdSceneIndexBase const&, DirtiedPrimEntries const& entries) override
    {
        for (auto const& entry : entries)
        {
            if (entry.primPath == _path)
                ++dirtied;
        }
    }

    void PrimsRenamed(HdSceneIndexBase const&, RenamedPrimEntries const&) override {}

private:
    SdfPath const _path;
};

GfVec4f GetVec4f(HdContainerDataSourceHandle const& ds, TfToken const& name)
{
    auto typed = HdTypedSampledDataSource<GfVec4f>::Cast(ds ? ds->Get(name) : nullptr);
    return typed ? typed->GetTypedValue(0.0f) : GfVec4f(-1.0f);
}

float GetFloat(HdContainerDataSourceHandle const& ds, TfToken const& name)
{
    auto typed = HdTypedSampledDataSource<float>::Cast(ds ? ds->Get(name) : nullptr);
    return typed ? typed->GetTypedValue(0.0f) : -1.0f;
}

// Checks the published prim carries exactly the material and scene ambient Storm is given.
void ExpectPublished(HdSceneIndexBaseRefPtr const& sceneIndex, SdfPath const& path,
    GlfSimpleMaterial const& material, GfVec4f const& sceneAmbient)
{
    HdSceneIndexPrim const prim = sceneIndex->GetPrim(path);
    ASSERT_EQ(prim.primType, kGlobalMaterialPrimType);
    ASSERT_TRUE(prim.dataSource);

    EXPECT_EQ(GetVec4f(prim.dataSource, TfToken("ambient")), material.GetAmbient());
    EXPECT_EQ(GetVec4f(prim.dataSource, TfToken("diffuse")), material.GetDiffuse());
    EXPECT_EQ(GetVec4f(prim.dataSource, TfToken("specular")), material.GetSpecular());
    EXPECT_EQ(GetVec4f(prim.dataSource, TfToken("emission")), material.GetEmission());
    EXPECT_FLOAT_EQ(GetFloat(prim.dataSource, TfToken("shininess")),
        static_cast<float>(material.GetShininess()));
    EXPECT_EQ(GetVec4f(prim.dataSource, TfToken("sceneAmbient")), sceneAmbient);
}

} // anonymous namespace

HVT_TEST(TestGlobalMaterial, publishedOnlyOnChange)
{
    // The scene-index lighting backend publishes the lighting material Storm consumes through
    // HdxSimpleLightTask as a "glfGlobalMaterial" prim, so renderers that do not run that task
    // (e.g. Flash) can match Storm's shading. SetLighting() runs every frame; the prim must carry
    // Storm's values and only be re-published when they change.

    TestHelpers::ScopedSceneDelegateMode si(false);

    auto context = TestHelpers::CreateTestContext();

    TestHelpers::TestStage stage(context->_backend);
    ASSERT_TRUE(stage.open(context->_sceneFilepath));

    TestHelpers::FramePassInstance testFramePassData =
        TestHelpers::FramePassInstance::CreateInstance(stage.stage(), context->_backend);

    hvt::FramePass& framePass = *testFramePassData.sceneFramePass.get();

    HdSceneIndexBaseRefPtr const terminalSceneIndex =
        framePass.GetRenderIndex()->GetTerminalSceneIndex();
    ASSERT_TRUE(terminalSceneIndex);

    SdfPath const materialPath = framePass.GetPath().AppendChild(kGlobalMaterialName);

    GlobalMaterialNoticeCounter counter(materialPath);
    terminalSceneIndex->AddObserver(HdSceneIndexObserverPtr(&counter));

    GlfSimpleMaterial const defaultMaterial = stage.defaultMaterial();
    GfVec4f const defaultAmbient            = stage.defaultAmbient();

    GlfSimpleMaterial changedMaterial = defaultMaterial;
    changedMaterial.SetAmbient(GfVec4f(0.5f, 0.25f, 0.125f, 1.0f));
    GfVec4f const changedAmbient(0.05f, 0.1f, 0.15f, 1.0f);

    constexpr int kUnchangedFrames = 5;
    constexpr int kChangedFrames   = 3;

    int frame                = 0;
    int addedBeforeChange    = -1;
    int dirtiedBeforeChange  = -1;
    bool publishedAfterFirst = false;

    auto render = [&]()
    {
        bool const changed = frame >= kUnchangedFrames;

        hvt::FramePassParams& params = framePass.params();

        params.renderBufferSize = GfVec2i(context->width(), context->height());
        params.viewInfo.framing =
            hvt::ViewParams::GetDefaultFraming(context->width(), context->height());

        params.viewInfo.viewMatrix       = stage.viewMatrix();
        params.viewInfo.projectionMatrix = stage.projectionMatrix();
        params.viewInfo.lights           = stage.defaultLights();
        params.viewInfo.material         = changed ? changedMaterial : defaultMaterial;
        params.viewInfo.ambient          = changed ? changedAmbient : defaultAmbient;

        params.colorspace      = HdxColorCorrectionTokens->sRGB;
        params.backgroundColor = TestHelpers::ColorDarkGrey;

        params.enablePresentation = context->presentationEnabled();

        framePass.Render();
        context->_backend->waitForGPUIdle();

        if (frame == 0)
        {
            publishedAfterFirst =
                terminalSceneIndex->GetPrim(materialPath).primType == kGlobalMaterialPrimType;
            ExpectPublished(terminalSceneIndex, materialPath, defaultMaterial, defaultAmbient);
        }
        if (frame == kUnchangedFrames - 1)
        {
            addedBeforeChange   = counter.added;
            dirtiedBeforeChange = counter.dirtied;
        }

        return ++frame < kUnchangedFrames + kChangedFrames;
    };

    try
    {
        context->run(render, &framePass);
    }
    catch (const std::exception& ex)
    {
        FAIL() << __FILE__ << ":" << __LINE__ << ": " << ex.what() << "\n";
    }

    ASSERT_TRUE(publishedAfterFirst) << "No glfGlobalMaterial prim at " << materialPath;

    // Identical lighting over several frames publishes the prim exactly once.
    EXPECT_EQ(addedBeforeChange, 1);
    EXPECT_EQ(dirtiedBeforeChange, 0);

    // A material/ambient change re-publishes once (AddPrims alone resyncs the prim).
    EXPECT_EQ(counter.added, 2);
    EXPECT_EQ(counter.dirtied, 0);
    EXPECT_EQ(counter.removed, 0);
    ExpectPublished(terminalSceneIndex, materialPath, changedMaterial, changedAmbient);

    terminalSceneIndex->RemoveObserver(HdSceneIndexObserverPtr(&counter));
}

#endif // HVT_SI_TASK_BACKEND_SUPPORTED
