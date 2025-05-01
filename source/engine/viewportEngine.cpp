// Copyright 2025 Autodesk, Inc.
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

#include <hvt/engine/viewportEngine.h>

#include <hvt/dataSource/dataSource.h>
#include <hvt/engine/framePass.h>

#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/plane.h>

#include <set>
#include <unordered_map>

#include "engine/shaders/solidSurface.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS::ViewportEngine
{

namespace
{

struct AggregatedHit
{
    explicit AggregatedHit(HdxPickHit const& h) : hit(h) {}

    HdxPickHit const& hit; // Reference to avoid a useless copy.

    std::set<int> elementIndices;
    std::set<int> edgeIndices;
    std::set<int> pointIndices;
};

using AggregatedHits = std::unordered_map<size_t, AggregatedHit>;

// Helper to compute the hash key for a single hit.
size_t GetPartialHitHash(HdxPickHit const& hit)
{
    return TfHash::Combine(hit.delegateId.GetHash(), hit.objectId.GetHash(),
        hit.instancerId.GetHash(), hit.instanceIndex);
}

// Aggregates subprimitive hits to the same prim/instance
AggregatedHits AggregateHits(HdxPickHitVector const& allHits)
{
    AggregatedHits aggrHits;

    for (auto const& hit : allHits)
    {
        const size_t hitHash = GetPartialHitHash(hit);
        const auto& it       = aggrHits.find(hitHash);
        if (it != aggrHits.end())
        {
            // Aggregates the element and edge indices.

            AggregatedHit& aHit = it->second;
            aHit.elementIndices.insert(hit.elementIndex);

            // -1 means none.
            if (hit.edgeIndex >= 0)
            {
                aHit.edgeIndices.insert(hit.edgeIndex);
            }

            // -1 means none.
            if (hit.pointIndex >= 0)
            {
                aHit.pointIndices.insert(hit.pointIndex);
            }

            continue;
        }

        // Adds a new entry.

        AggregatedHit aHitNew(hit);
        aHitNew.elementIndices.insert(hit.elementIndex);

        // -1 means none.
        if (hit.edgeIndex >= 0)
        {
            aHitNew.edgeIndices.insert(hit.edgeIndex);
        }

        // -1 means none.
        if (hit.pointIndex >= 0)
        {
            aHitNew.pointIndices.insert(hit.pointIndex);
        }

        aggrHits.try_emplace(hitHash, aHitNew);
    }

    return aggrHits;
}

// Process a single hit.
void ProcessHit(AggregatedHit const& aHit, TfToken const& pickTarget,
    HdSelection::HighlightMode highlightMode, SelectionFilterFn const& selectionFilterFn,
    /*out*/ HdSelectionSharedPtr& selection)
{
    HdxPickHit const& hit = aHit.hit;

    if (pickTarget == HdxPickTokens->pickPrimsAndInstances)
    {
        if (!hit.instancerId.IsEmpty())
        {
            // FIXME: This doesn't work for nested instancing.

            VtIntArray instanceIndex;
            instanceIndex.push_back(hit.instanceIndex);
            selection->AddInstance(highlightMode, hit.objectId, instanceIndex);
        }
        else
        {
            const SdfPathVector modifiedHitPaths = selectionFilterFn(hit.objectId);
            for (const auto& path : modifiedHitPaths)
            {
                selection->AddRprim(highlightMode, path);
            }
        }
    }
    else if (pickTarget == HdxPickTokens->pickFaces)
    {
        VtIntArray elements(aHit.elementIndices.size());
        elements.assign(aHit.elementIndices.begin(), aHit.elementIndices.end());
        selection->AddElements(highlightMode, hit.objectId, elements);
    }
    else if (pickTarget == HdxPickTokens->pickEdges)
    {
        if (!aHit.edgeIndices.empty())
        {
            VtIntArray edges(aHit.edgeIndices.size());
            edges.assign(aHit.edgeIndices.begin(), aHit.edgeIndices.end());
            selection->AddEdges(highlightMode, hit.objectId, edges);
        }
    }
    else if (pickTarget == HdxPickTokens->pickPoints ||
        pickTarget == HdxPickTokens->pickPointsAndInstances)
    {
        if (!hit.instancerId.IsEmpty())
        {
            // FIXME: This doesn't work for nested instancing.

            VtIntArray instanceIndex;
            instanceIndex.push_back(hit.instanceIndex);
            selection->AddInstance(highlightMode, hit.objectId, instanceIndex);
        }
        else if (!aHit.pointIndices.empty())
        {
            VtIntArray points(aHit.pointIndices.size());
            points.assign(aHit.pointIndices.begin(), aHit.pointIndices.end());
            selection->AddPoints(highlightMode, hit.objectId, points);
        }
    }
    else
    {
        std::string err { "Unsupported picking mode: " };
        err += pickTarget.GetText();
        TF_CODING_ERROR("%s", err.c_str());
    }
}

} // anonymous namespace

void UpdateRendererSettings(RenderIndexProxy* renderIndex)
{
    // turn on shared OpenGL render buffers for the OxideRenderDelegate
    renderIndex->RenderIndex()->GetRenderDelegate()->SetRenderSetting(
        TfToken("OxideSharedOpenGLRenderBuffers"), VtValue(true));

    // FIXME: From the documentation the default values for GfRange3d are "[FLT_MAX,-FLT_MAX]"
    GfRange3d bounds = GfRange3d();

    bool groundPlaneEnabled     = true;
    GfVec3d midPoint            = bounds.GetMidpoint();
    GfVec3f groundPlanePosition = GfVec3f(static_cast<float>(midPoint[0]),
        static_cast<float>(bounds.GetMin()[1]), static_cast<float>(midPoint[2]));
    GfVec3f groundPlaneNormal   = GfVec3f(0.f, 1.f, 0.f);
    float shadowOpacity         = 1.0f;
    GfVec3f shadowColor         = GfVec3f(0.f, 0.f, 0.f);
    float reflectionOpacity     = 0.0f;
    GfVec3f reflectionColor     = GfVec3f(1.f, 1.f, 1.f);
    float reflectionRoughness   = .2f;

    auto rd = renderIndex->RenderIndex()->GetRenderDelegate();
    rd->SetRenderSetting(TfToken("ultra:ground_plane_settings"),
        VtValue(std::tuple(groundPlaneEnabled, groundPlanePosition, groundPlaneNormal,
            shadowOpacity, shadowColor, reflectionOpacity, reflectionColor, reflectionRoughness)));

    //
    // Initialize denoise on and maxSamples to kDefaultMaxSamples.
    //
    constexpr int kDefaultMaxSamples = 50;
    rd->SetRenderSetting(TfToken("OxideMaxSamples"), VtValue(kDefaultMaxSamples));
    rd->SetRenderSetting(TfToken("OxideDenoiseEnabled"), VtValue(true));
    rd->SetRenderSetting(TfToken("OxideResetCounter"), VtValue(false));

    // Don't flip Y.
    rd->SetRenderSetting(TfToken("OxideFlipYOutput"), VtValue(false));

    // Initialize alpha disabled.
    rd->SetRenderSetting(TfToken("ultra:alpha_enabled"), VtValue(false));

    // Initialize background settings.
    rd->SetRenderSetting(TfToken("ultra:use_ibl_as_background"), VtValue(true));
    rd->SetRenderSetting(TfToken("ultra:background_image"), VtValue(SdfAssetPath()));
    GfVec3f defaultColor(1.0f, 1.0f, 1.0f);
    rd->SetRenderSetting(
        TfToken("ultra:background_colors"), VtValue(VtVec3fArray(2, defaultColor)));

    // Initialize ToneMapping settings.
    rd->SetRenderSetting(TfToken("ultra:inverseToneMapping_enabled"), VtValue(false));
    rd->SetRenderSetting(TfToken("ultra:inverseToneMapping_exposure"), VtValue(0.0f));

    // Renderbuffer raw handles as shared memory/texture objects
    rd->SetRenderSetting(TfToken("OxideSharedMemoryBuffers"), VtValue(true));
}

void CreateRenderer(RenderIndexProxyPtr& renderIndex, const RendererDescriptor& desc)
{
    if (renderIndex)
        renderIndex->RenderIndex()->GetRenderDelegate()->Stop();

    // Recreate the render index
    renderIndex = RenderIndexProxyPtr(new RenderIndexProxy(desc.rendererName, desc.hgiDriver));

    if (renderIndex)
    {
        UpdateRendererSettings(renderIndex.get());
        renderIndex->RenderIndex()->GetRenderDelegate()->Restart();
    }
}

void CreateUSDSceneDelegate(SceneDelegatePtr& sceneDelegate, const USDSceneDelegateDescriptor& desc,
    int refineLevelFallback)
{
    bool isPopulated = true;
    if (!sceneDelegate)
    {
        sceneDelegate = std::make_unique<UsdImagingDelegate>(desc.renderIndex, SdfPath("/"));
        isPopulated   = false;
    }

    auto rootPath = SdfPath::AbsoluteRootPath();

    // Prepare Batch
    if (!isPopulated && desc.stage->GetPseudoRoot().GetPath().HasPrefix(rootPath))
    {
        sceneDelegate->SetUsdDrawModesEnabled(true);
        sceneDelegate->Populate(desc.stage->GetPrimAtPath(rootPath), desc.excludedPrimPaths);
        sceneDelegate->SetInvisedPrimPaths(desc.invisedPrimPaths);
    }

    ViewportEngine::UpdateSceneDelegate(
        sceneDelegate, UsdTimeCode::EarliestTime(), refineLevelFallback);
}

void UpdateSceneDelegate(
    SceneDelegatePtr& sceneDelegate, UsdTimeCode frame, int refineLevelFallback)
{
    HD_TRACE_FUNCTION();
    if (!sceneDelegate)
        return;

    // pre set time
    // ******************************************************
    // Set the fallback refine level; if this changes from the
    // existing value, all prim refine levels will be dirtied.
    if (refineLevelFallback >= 0 && refineLevelFallback <= 8)
    {
        sceneDelegate->SetRefineLevelFallback(refineLevelFallback);
    }
    else
    {
        TF_CODING_ERROR("Invalid refineLevel %d, expected range is [0,8]\n", refineLevelFallback);
    }

    // Apply any queued up scene edits.
    sceneDelegate->ApplyPendingUpdates();
    //*******************************************************

    sceneDelegate->SetTime(frame);
}

void UpdateSceneDelegates(std::vector<SceneDelegatePtr>& sceneDelegates, UsdTimeCode frame)
{
    for (auto& delegate : sceneDelegates)
    {
        UpdateSceneDelegate(delegate, frame);
    }
}

void UpdateUSDSceneIndex(UsdImagingStageSceneIndexRefPtr& sceneIndex, UsdTimeCode frame)
{
    if (!sceneIndex)
        return;

    // Pre-set time
    // XXX(USD-7115): fallback refine level
    sceneIndex->ApplyPendingUpdates();

    sceneIndex->SetTime(frame);
}

void CreateUSDSceneIndex(HdSceneIndexBaseRefPtr& sceneIndex,
    UsdImagingStageSceneIndexRefPtr& stageSceneIndex,
    UsdImagingSelectionSceneIndexRefPtr& selectionSceneIndex, USDSceneIndexDescriptor const& desc)
{
    UsdImagingCreateSceneIndicesInfo info;
    info.displayUnloadedPrimsWithBounds = true;
    info.stage                          = desc.stage;
    info.overridesSceneIndexCallback    = AppendOverridesSceneIndices; // Nothing to add

    const UsdImagingSceneIndices sceneIndices = UsdImagingCreateSceneIndices(info);

    stageSceneIndex     = sceneIndices.stageSceneIndex;
    selectionSceneIndex = sceneIndices.selectionSceneIndex;
    sceneIndex          = sceneIndices.finalSceneIndex;

    // sceneIndex = _displayStyleSceneIndex =
    //     HdsiLegacyDisplayStyleOverrideSceneIndex::New(_sceneIndex);

    desc.renderIndex->InsertSceneIndex(sceneIndex, SdfPath::AbsoluteRootPath());
}

HdSceneIndexBaseRefPtr CreateUSDSceneIndex(UsdStageRefPtr& stage,
    UsdImagingCreateSceneIndicesInfo::SceneIndexAppendCallback const& callback)
{
    UsdImagingCreateSceneIndicesInfo info;
    info.displayUnloadedPrimsWithBounds = true;
    info.stage                          = stage;
    info.overridesSceneIndexCallback    = callback;

    const UsdImagingSceneIndices sceneIndices = UsdImagingCreateSceneIndices(info);
    return sceneIndices.finalSceneIndex;
}

FramePassPtr CreateFramePass(const FramePassDescriptor& passDesc)
{
    FramePassPtr framePass = std::make_unique<FramePass>(passDesc.uid.GetText());
    framePass->Initialize(passDesc);
    framePass->CreatePresetTasks(FramePass::PresetTaskLists::Default);
    return framePass;
}

HdSelectionSharedPtr PrepareSelection(HdSceneDelegate* sceneDelegate, SdfPathSet const& hitPaths,
    HdSelection::HighlightMode highlightMode)
{
    HdSelectionSharedPtr selection = std::make_shared<HdSelection>();

    if (hitPaths.size() > 0)
    {
        // populate new selection
        auto usdSceneDelegate = dynamic_cast<UsdImagingDelegate*>(sceneDelegate);

        for (const SdfPath& path : hitPaths)
        {
            if (usdSceneDelegate)
            {
                usdSceneDelegate->PopulateSelection(
                    highlightMode, path, UsdImagingDelegate::ALL_INSTANCES, selection);
            }
            else
            {
                // TODO Custom scene delegates might need better selection processing
                selection->AddRprim(highlightMode, path);
            }
        }
    }

    return selection;
}

HdSelectionSharedPtr PrepareSelection(
    SdfPathSet const& hitPaths, HdSelection::HighlightMode highlightMode)
{
    HdSelectionSharedPtr selection = std::make_shared<HdSelection>();

    if (hitPaths.size() > 0)
    {
        for (const SdfPath& path : hitPaths)
        {
            // TODO Custom scene delegates might need better selection processing
            selection->AddRprim(highlightMode, path);
        }
    }

    return selection;
}

HdSelectionSharedPtr PrepareSelection(HdxPickHitVector const& allHits, TfToken const& pickTarget,
    HdSelection::HighlightMode highlightMode, SelectionFilterFn const& filter)
{
    auto selection = std::make_shared<HdSelection>();

    AggregatedHits aggrHits = AggregateHits(allHits);
    for (const auto& pair : aggrHits)
    {
        ProcessHit(pair.second, pickTarget, highlightMode, filter, selection);
    }

    return selection;
}

UsdStageRefPtr CreateStage(const std::string& stageName)
{
    return UsdStage::CreateInMemory(stageName);
}

UsdStageRefPtr CreateStageFromFile(const std::string& fileName)
{
    return UsdStage::Open(fileName);
}

void CreateGrid(UsdStageRefPtr& stage, SdfPath gridPath, const GfVec3d& position, bool isVisible)
{
    if (!stage)
    {
        stage = ViewportEngine::CreateStage("");
    }

    auto prim = stage->GetPrimAtPath(gridPath);
    if (!prim.IsValid())
    {
        auto xform          = stage->DefinePrim(gridPath, TfToken("Xform"));
        UsdGeomXformable tm = UsdGeomXformable(xform.GetPrim());
        if (tm)
        {
            auto tmOp = tm.AddTranslateOp();
            tmOp.Set(GfVec3d(0.0, 0.0, 0.0));
            auto scaleOp = tm.AddScaleOp();
            scaleOp.Set(GfVec3f(1.0f, 1.0f, 1.0f));
        }

        auto createGrid = [&stage](SdfPath const& parentPath, GfVec3f const& color,
                              GfVec3f const& /* orientation */)
        {
            static const TfToken kCurveName { "grid" };
            static constexpr float kScale     = 100.0f;
            static constexpr int kSegments    = 10;
            static constexpr float kHalfScale = kScale * 0.5f;

            VtVec3fArray vertices;
            VtIntArray curveVertexCounts;
            for (int x = 0; x < kSegments + 1; ++x)
            {
                float dx = (kScale * (float)x) / (float)kSegments;
                vertices.push_back({ dx - kHalfScale, 0.0f, -kHalfScale });
                vertices.push_back({ dx - kHalfScale, 0.0f, kHalfScale });
                curveVertexCounts.push_back(2);

                vertices.push_back({ -kHalfScale, 0.0f, dx - kHalfScale });
                vertices.push_back({ kHalfScale, 0.0f, dx - kHalfScale });
                curveVertexCounts.push_back(2);
            }

            const VtVec3fArray colorArray = { color };

            // add the axis line
            UsdGeomBasisCurves basisCurve =
                UsdGeomBasisCurves::Define(stage, parentPath.AppendChild(kCurveName));
            basisCurve.GetPointsAttr().Set(vertices);
            basisCurve.GetCurveVertexCountsAttr().Set(curveVertexCounts);
            basisCurve.CreateTypeAttr().Set(UsdGeomTokens->linear, UsdTimeCode::Default());
            basisCurve.GetDisplayColorPrimvar().Set(colorArray);
        };

        createGrid(gridPath, { 0.25f, 0.25f, 0.25f }, { 0.0f, 0.0f, 0.0f });

        prim = stage->GetPrimAtPath(gridPath);
    }

    // set the visibility
    if (isVisible)
        UsdGeomImageable(prim).MakeVisible();
    else
        UsdGeomImageable(prim).MakeInvisible();

    // set the position
    bool resetStack = true;
    auto tm         = UsdGeomXformable(prim);
    if (tm)
    {
        auto xFormOps = tm.GetOrderedXformOps(&resetStack);
        xFormOps[0].Set(position);
    }

    // set the scale
    // float len = static_cast<float>(scale * 0.5);
    // xFormOps[1].Set(GfVec3f(len, len, len));
}

void CreateCanvas(UsdStageRefPtr& stage, const SdfPath& canvasPath, const GfVec3d& /*position*/,
    float length, float width, bool useYAxis, bool isVisible)
{
    if (!stage)
    {
        stage = ViewportEngine::CreateStage("");
    }

    auto prim = stage->GetPrimAtPath(canvasPath);
    if (!prim.IsValid())
    {
        auto xform          = stage->DefinePrim(canvasPath, TfToken("Xform"));
        UsdGeomXformable tm = UsdGeomXformable(xform.GetPrim());

        static const TfToken kPlaneName = TfToken("plane");
        UsdGeomPlane plane = UsdGeomPlane::Define(stage, canvasPath.AppendChild(kPlaneName));

        // Set Canvas color
        const VtVec3fArray colorArray = { GfVec3f(1.0f, 1.0f, 1.0f) };
        plane.GetDisplayColorPrimvar().Set(colorArray);
        plane.GetDoubleSidedAttr().Set(false);

        plane.GetLengthAttr().Set((double)length);
        plane.GetWidthAttr().Set((double)width);

        if (useYAxis)
        {
            plane.GetAxisAttr().Set(TfToken("Y"));

            // Z axis, w = x-axis, l = y-axis
            VtVec3fArray extent(2);
            extent[0] = GfVec3f(-1.0f * width, -1.0f * length, 0.0f);
            extent[1] = GfVec3f(width, length, 0.0f);
            plane.GetExtentAttr().Set(extent);
        }
        else // Z axis
        {
            plane.GetAxisAttr().Set(TfToken("Z"));

            // Y axis, w = x-axis, l = z-axis
            VtVec3fArray extent(2);
            extent[0] = GfVec3f(-1.0f * width, 0.0f, -1.0f * length);
            extent[1] = GfVec3f(width, 0.0f, length);
            plane.GetExtentAttr().Set(extent);
        }

        // Apply a custom flat color shader
        auto path         = plane.GetPrim().GetPrimPath();
        auto materialPath = path.AppendPath(SdfPath("material"));
        auto material     = UsdShadeMaterial::Define(stage, materialPath);

        auto pbrShaderPath = materialPath.AppendPath(SdfPath("gizmoCanvasShader"));
        auto pbrShader     = UsdShadeShader::Define(stage, pbrShaderPath);
        pbrShader.GetImplementationSourceAttr().Set(UsdShadeTokens->universalSourceType);
        pbrShader.SetSourceCode(kSolidSurfaceShader, TfToken("glslfx"));
        material.CreateSurfaceOutput().ConnectToSource(
            pbrShader.ConnectableAPI(), TfToken("surface"));
        UsdShadeMaterialBindingAPI::Apply(stage->GetPrimAtPath(path)).Bind(material);

        prim = stage->GetPrimAtPath(canvasPath);
    }

    // set the visibility
    if (isVisible)
        UsdGeomImageable(prim).MakeVisible();
    else
        UsdGeomImageable(prim).MakeInvisible();
}

void CreateSelectBox(UsdStageRefPtr& stage, SdfPath selectBoxPath, bool isVisible)
{
    if (!stage)
    {
        stage = ViewportEngine::CreateStage("");
    }

    auto prim = stage->GetPrimAtPath(selectBoxPath);
    if (!prim.IsValid())
    {
        auto xform          = stage->DefinePrim(selectBoxPath, TfToken("Xform"));
        UsdGeomXformable tm = UsdGeomXformable(xform.GetPrim());
        if (tm)
        {
            auto tmOp = tm.AddTranslateOp();
            tmOp.Set(GfVec3d(0.0, 0.0, 0.0));
            auto scaleOp = tm.AddScaleOp();
            scaleOp.Set(GfVec3f(1.0f, 1.0f, 1.0f));
        }

        auto createSelectBox = [&stage](SdfPath parentPath, GfVec3f color)
        {
            static const TfToken kCurveName = TfToken("selectBox");

            VtVec3fArray vertices;
            VtIntArray selectBoxVertexCounts;
            {
                vertices.push_back({ 1.0f, -1.0f, -1.0f });
                vertices.push_back({ 1.0f, 0.0f, -1.0f });
                vertices.push_back({ 0.0f, 0.0f, -1.0f });
                vertices.push_back({ 0.0f, -1.0f, -1.0f });
                vertices.push_back({ 1.0f, -1.0f, -1.0f });
                selectBoxVertexCounts.push_back(5);
            }

            const VtVec3fArray colorArray = { color };

            // add the select boxes
            UsdGeomBasisCurves basisCurve =
                UsdGeomBasisCurves::Define(stage, parentPath.AppendChild(kCurveName));
            basisCurve.GetPointsAttr().Set(vertices);
            basisCurve.GetCurveVertexCountsAttr().Set(selectBoxVertexCounts);
            basisCurve.CreateTypeAttr().Set(UsdGeomTokens->linear, UsdTimeCode::Default());
            basisCurve.GetDisplayColorPrimvar().Set(colorArray);
        };

        // red select box
        createSelectBox(selectBoxPath, { 1.0f, 0.0f, 0.0f });

        prim = stage->GetPrimAtPath(selectBoxPath);
    }

    // Set the visibility
    if (isVisible)
        UsdGeomImageable(prim).MakeVisible();
    else
        UsdGeomImageable(prim).MakeInvisible();
}

void CreateAxisTripod(
    UsdStageRefPtr& stage, SdfPath path, const GfVec3d& position, float scale, bool isVisible)
{
    if (!stage)
    {
        stage = ViewportEngine::CreateStage("");
    }

    auto prim = stage->GetPrimAtPath(path);
    if (!prim.IsValid())
    {
        auto xform          = stage->DefinePrim(path, TfToken("Xform"));
        UsdGeomXformable tm = UsdGeomXformable(xform.GetPrim());
        if (tm)
        {
            auto tmOp = tm.AddTranslateOp();
            tmOp.Set(GfVec3d(0.0, 0.0, 0.0));
            auto scaleOp = tm.AddScaleOp();
            scaleOp.Set(GfVec3f(1.0f, 1.0f, 1.0f));
        }

        auto createArrow = [&stage](SdfPath parentPath, std::string label, GfVec3f color,
                               GfVec3f orientation, float scale)
        {
            auto setScreenScale = [](const UsdGeomPrimvarsAPI& api, float screenScale,
                                      const GfVec3d& center, bool cameraFacing)
            {
                if (cameraFacing)
                    api.CreatePrimvar(
                           TfToken("cameraFacing"), SdfValueTypeNames->Int, UsdGeomTokens->constant)
                        .Set(1);

                api.CreatePrimvar(
                       TfToken("pixelScale"), SdfValueTypeNames->Bool, UsdGeomTokens->constant)
                    .Set(true);
                api.CreatePrimvar(
                       TfToken("scaleCenter"), SdfValueTypeNames->Float3, UsdGeomTokens->constant)
                    .Set(GfVec3f(center));
                api.CreatePrimvar(TfToken("screenSpaceSize"), SdfValueTypeNames->Float,
                       UsdGeomTokens->constant)
                    .Set(screenScale);
                api.CreatePrimvar(
                       TfToken("modelSpaceSize"), SdfValueTypeNames->Float, UsdGeomTokens->constant)
                    .Set(1.0f);
            };

            static const GfVec3d kCurveOffset = { 0.0, 0.0, 0.0 };
            static const GfVec3d kConeOffset  = { 0.0, 0.0, 1.0 };
            static const TfToken kCurveName   = TfToken("curve");
            static const TfToken kConeName    = TfToken("cone");
            static const TfToken kLabelName   = TfToken("label");

            static VtVec3fArray kVertices = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
            const VtVec3fArray colorArray = { color };

            // orient the arrow in the correct direction
            SdfPath path = parentPath.AppendChild(TfToken(label));
            auto xform   = stage->DefinePrim(path, TfToken("Xform"));
            UsdGeomXformable(xform.GetPrim()).AddRotateXYZOp().Set(orientation);

            // add the axis line
            UsdGeomBasisCurves basisCurve =
                UsdGeomBasisCurves::Define(stage, path.AppendChild(kCurveName));
            basisCurve.GetPointsAttr().Set(kVertices);
            basisCurve.GetCurveVertexCountsAttr().Set(VtIntArray(1, 2));
            basisCurve.CreateTypeAttr().Set(UsdGeomTokens->linear, UsdTimeCode::Default());
            basisCurve.GetDisplayColorPrimvar().Set(colorArray);
            setScreenScale(UsdGeomPrimvarsAPI(basisCurve), scale, kCurveOffset, false);

            // add the arrow head
            auto cone = UsdGeomCone::Define(stage, path.AppendChild(kConeName));
            cone.GetRadiusAttr().Set(.05);
            cone.GetHeightAttr().Set(.1);
            cone.GetDisplayColorPrimvar().Set(colorArray);
            cone.AddTranslateOp().Set(kConeOffset);
            setScreenScale(UsdGeomPrimvarsAPI(cone), scale, -kConeOffset, false);

#ifdef PXR_TEXTSYSTEM_SUPPORT_ENABLED
            static const GfVec3d kLabelOffset { -1.0, 0.0, 11.0 };

            float textScale = scale * .1f;

            // add the text label
            UsdGeomSimpleText simpleText =
                UsdGeomSimpleText::Define(stage, path.AppendChild(kLabelName));
            simpleText.GetTextDataAttr().Set(label);
            simpleText.GetTypefaceAttr().Set("Times New Roman");
            simpleText.GetTextHeightAttr().Set(1);
            simpleText.GetDisplayColorAttr().Set(colorArray);
            simpleText.AddTranslateOp().Set(kLabelOffset);
            setScreenScale(UsdGeomPrimvarsAPI(simpleText), textScale, -kLabelOffset, true);
#endif
        };

        createArrow(path, "X", { 1.0f, 0.0f, 0.0f }, { 0.0f, 90.0f, 0.0f }, scale);
        createArrow(path, "Y", { 0.0f, 1.0f, 0.0f }, { -90.0f, 0.0f, 0.0f }, scale);
        createArrow(path, "Z", { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, scale);

        prim = stage->GetPrimAtPath(path);
    }

    // set the visibility
    if (isVisible)
        UsdGeomImageable(prim).MakeVisible();
    else
        UsdGeomImageable(prim).MakeInvisible();

    // set the position
    bool resetStack = true;
    auto tm         = UsdGeomXformable(prim);
    if (tm)
    {
        auto xFormOps = tm.GetOrderedXformOps(&resetStack);
        xFormOps[0].Set(position);
    }

    // set the scale
    // float len = static_cast<float>(scale * 0.5);
    // xFormOps[1].Set(GfVec3f(len, len, len));
}

void UpdatePrim(UsdStageRefPtr& stage, const SdfPath& path, const GfVec3d& position, float scale,
    bool isVisible)
{
    if (!stage)
    {
        return;
    }

    auto prim = stage->GetPrimAtPath(path);
    if (!prim.IsValid())
    {
        return;
    }

    // set the visibility
    if (isVisible)
    {
        UsdGeomImageable(prim).MakeVisible();
    }
    else
    {
        UsdGeomImageable(prim).MakeInvisible();
    }

    bool resetStack = true;
    auto tm         = UsdGeomXformable(prim);
    if (!tm)
    {
        return;
    }

    bool translationFound = false;
    bool scaleFound       = false;

    auto xFormOps = tm.GetOrderedXformOps(&resetStack);
    for (const auto& xFormOp : xFormOps)
    {
        // Set the translation.

        if (xFormOp.GetOpType() == UsdGeomXformOp::TypeTranslate)
        {
            if (xFormOp.GetPrecision() == xFormOp.PrecisionFloat)
            {
                xFormOp.Set(GfVec3f(position));
            }
            else
            {
                // expect double
                xFormOp.Set(position);
            }
            translationFound = true;
        }

        // Set the scale factor if any.

        else if (xFormOp.GetOpType() == UsdGeomXformOp::TypeScale)
        {
            if (scale > 0.0f)
            {
                // set the scale
                xFormOp.Set(GfVec3f(scale * 0.5f));
            }
            scaleFound = true;
        }
    }

    if (!translationFound)
    {
        TF_RUNTIME_ERROR("ViewportEngine::UpdatePrim failed to update the prim's translation.");
    }

    if (!scaleFound && scale > 0.0f)
    {
        TF_RUNTIME_ERROR("ViewportEngine::UpdatePrim failed to update the prim's scale.");
    }
}

DataSourceRegistry& GetDataSourceRegistry()
{
    return DataSourceRegistry::registry();
}

} // namespace HVT_NS::ViewportEngine
