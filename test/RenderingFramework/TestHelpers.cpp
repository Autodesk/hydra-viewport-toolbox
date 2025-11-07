//
// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <RenderingFramework/TestHelpers.h>

#if TARGET_OS_IPHONE
#include <RenderingFramework/MetalTestContext.h>
#else
#include <RenderingFramework/OpenGLTestContext.h>
#endif

#include <RenderingUtils/ImageUtils.h>

#include <hvt/tasks/resources.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#include <pxr/base/gf/frustum.h>
#include <pxr/imaging/hgi/tokens.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
#define STBI_MSC_SECURE_CRT
#endif

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{

#if TARGET_OS_IPHONE
const std::filesystem::path outFullpath  = TestHelpers::documentDirectoryPath() + "/Data";
const std::filesystem::path inAssetsPath = TestHelpers::mainBundlePath() + "/data/assets";
const std::string resFullpath            = TestHelpers::mainBundlePath() + "/data";
std::filesystem::path inBaselinePath     = TestHelpers::mainBundlePath() + "/data/baselines";
#elif __ANDROID__
const std::filesystem::path outFullpath  = getenv("APP_CACHE_PATH");
const std::filesystem::path inAssetsPath = getenv("HVT_TEST_ASSETS");
const std::string resFullpath            = getenv("HVT_RESOURCES");
std::filesystem::path inBaselinePath     = getenv("HVT_BASELINES");
#else
const auto outFullpath = std::filesystem::path(TOSTRING(TEST_DATA_OUTPUT_PATH)) / "computed";
const std::filesystem::path inAssetsPath = TOSTRING(HVT_TEST_DATA_PATH) + "/data/assets";
const std::string resFullpath            = TOSTRING(HVT_RESOURCE_PATH);
std::filesystem::path inBaselinePath     = TOSTRING(HVT_TEST_DATA_PATH) + "/data/baselines";
#endif

} // anonymous namespace

namespace TestHelpers
{

std::string HydraRendererContext::readImage(
    const std::string& fileName, int& width, int& height, int& channels)
{
    const auto dataPath        = getAssetsDataFolder();
    const std::string filePath = (dataPath / fileName).string();
    return RenderingUtils::readImage(filePath, width, height, channels);
}

bool beginsWithUpperCase(const std::string& name)
{
    if (name.empty())
    {
        return false;
    }

    return name[0] == static_cast<char>(std::toupper(name[0]));
}

// Function that converts the filename to camel case (first letter lower case, remainder untouched).
std::string toCamelCase(const std::string& filename)
{
    std::string camelCaseFilename = filename;
    if (!camelCaseFilename.empty())
    {
        camelCaseFilename[0] = static_cast<char>(std::tolower(camelCaseFilename[0]));
    }
    return camelCaseFilename;
}

std::string HydraRendererContext::getFilename(
    const std::filesystem::path& filePath, const std::string& filename)
{
    std::string fullFilepath = filePath.string() + "/" + filename;

#ifdef __ANDROID__
    fullFilepath += "_android";
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    // Default baselines are for real devices which is the typical case in a local development
    // environment Using Design-For-Ipad in pipeline to easy setup and track regressions
    const char* dest = getenv("DESTINATION");
    if (dest != nullptr && std::string(dest).find("macOS") != std::string::npos)
    {
        fullFilepath += "_designforipad";
    }
    fullFilepath += "_ios";
#else
    fullFilepath += "_osx";
#endif // TARGET_OS_IPHONE
#endif // __ANDROID__
    fullFilepath += ".png";

    // Test a camel case variant if the filename does not exist. Many unit tests are using their
    // test name as the baseline image name, but the casing has not yet been standardized for all
    // test names and baseline images, so we try both versions.
    // TODO: Remove this part once the casing of all assets it standardized.
    if (!std::filesystem::exists(fullFilepath) && beginsWithUpperCase(filename))
    {
        const std::string fullFilepathCamelCase = getFilename(filePath, toCamelCase(filename));

        if (std::filesystem::exists(fullFilepathCamelCase))
        {
            return fullFilepathCamelCase;
        }
    }

    return fullFilepath;
};

bool HydraRendererContext::compareImages(
    const std::string& fileName, const uint8_t threshold, const uint16_t pixelCountThreshold)
{
    std::string inFileName    = fileName;
    const auto baselinePath   = getBaselineFolder();
    const std::string inFile  = getFilename(baselinePath, inFileName);
    const std::string outFile = getFilename(outFullpath, fileName + "_computed");

    return compareImages(inFile, outFile, threshold, pixelCountThreshold);
}

bool HydraRendererContext::compareImage(const std::string& computedFilename,
    const std::string& baselineFilename, const uint8_t threshold, const uint16_t pixelCountThreshold)
{
    const auto baselinePath    = getBaselineFolder();
    const std::string baseline = getFilename(baselinePath, baselineFilename);
    const std::string computed = getFilename(outFullpath, computedFilename + "_computed");
    return compareImages(computed, baseline, threshold, pixelCountThreshold);
}

bool HydraRendererContext::compareOutputImages(const std::string& fileName1,
    const std::string& fileName2, const uint8_t threshold, const uint16_t pixelCountThreshold)
{
    const std::string file1 = getFilename(outFullpath, fileName1 + "_computed");
    const std::string file2 = getFilename(outFullpath, fileName2 + "_computed");

    return compareImages(file1, file2, threshold, pixelCountThreshold);
}

bool HydraRendererContext::compareImages(const std::string& inFile, const std::string& outFile,
    const uint8_t threshold, const uint16_t pixelCountThreshold)
{
    return RenderingUtils::compareImages(inFile, outFile, threshold, pixelCountThreshold);
}

void HydraRendererContext::createHGI([[maybe_unused]] pxr::TfToken type)
{
    if (!type.IsEmpty())
        _hgi = pxr::Hgi::CreateNamedHgi(type);
    else
    {
#if defined(ADSK_OPENUSD_PENDING)
        _hgi = pxr::Hgi::CreatePlatformDefaultHgi();
#elif defined(_WIN32)
        _hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->OpenGL);
#elif defined(__APPLE__)
        _hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->Metal);
#elif defined(__linux__)
        _hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->OpenGL);
#else
    #error "The platform is not supported"
#endif
    }

    if (!_hgi->IsBackendSupported())
    {
        throw std::runtime_error("HGI initialization succeeded but backend is not supported!");
    }
    else if (_hgiDriver.driver.IsEmpty())
    {
        _hgiDriver.name   = pxr::HgiTokens->renderDriver;
        _hgiDriver.driver = pxr::VtValue(_hgi.get());
    }
    else
    {
        throw std::runtime_error("HGI initialization already done!");
    }
}

void HydraRendererContext::destroyHGI()
{
    _hgi       = nullptr;
    _hgiDriver = {};
}

TestView::TestView(std::shared_ptr<HydraRendererContext>& context) : _context(context)
{
    // Tell the Viewport Toolbox where to find its resources.
    hvt::SetResourceDirectory(resFullpath);

    // Create a basic material.
    _defaultMaterial.SetAmbient(pxr::GfVec4f(0.2f, 0.2f, 0.2f, 1.0f));
    _defaultMaterial.SetSpecular(pxr::GfVec4f(0.1f, 0.1f, 0.1f, 1.0f));
    _defaultMaterial.SetShininess(32.0f);
}

bool TestView::updateCameraAndLights(pxr::GfRange3d const& world)
{
    // Compute bounds and diameter.
    const auto dimensions = world.GetSize();
    const auto diameter   = std::max(dimensions[0], std::max(dimensions[1], dimensions[2]));

    // Define view matrix.
    pxr::GfVec3d centerPoint = world.GetMidpoint();
    pxr::GfVec3d eyePoint    = centerPoint - pxr::GfVec3d(0, 0, 2.0 * diameter);
    pxr::GfVec3d upDir(0, 1, 0);
    _viewMatrix = pxr::GfMatrix4d().SetLookAt(eyePoint, centerPoint, upDir);

    pxr::GfFrustum frustum;
    frustum.SetPerspective(
        45.0, _context->width() / _context->height(), diameter / 100, diameter * 10);
    _projectionMatrix = frustum.ComputeProjectionMatrix();

    // Set up basic lighting.
    _defaultLights.clear();
    pxr::GlfSimpleLight light;
    light.SetPosition(
        pxr::GfVec4f(float(eyePoint[0]), float(eyePoint[1]), float(eyePoint[2]), 1.0f));
    light.SetAmbient(pxr::GfVec4f(0));
    _defaultLights.push_back(light);

    return true;
}

TestStage::TestStage(std::shared_ptr<HydraRendererContext>& context, bool createSessionLayer) :
    TestView(context), _createSessionLayer(createSessionLayer)
{
}

TestStage::~TestStage()
{
    if (_stage && _createSessionLayer)
    {
        // Clear the entire session layer (i.e., removes all temporary prims).
        _sessionLayer->Clear();

        // Restore edit target to root layer.
        _stage->SetEditTarget(_stage->GetRootLayer());
    }
}

bool TestStage::open(const std::string& path)
{
    _stage = pxr::UsdStage::Open(path);
    if (_stage == nullptr)
    {
        return false;
    }

    if (_createSessionLayer)
    {
        // Get or create a session layer for temporary prims.
        _sessionLayer = _stage->GetSessionLayer();

        // Set the session layer as edit target (i.e., all new prims go here).
        _stage->SetEditTarget(pxr::UsdEditTarget(_sessionLayer));
    }

    // Compute bounds and diameter.
    auto world = computeStageBounds();
    return updateCameraAndLights(world);
}

pxr::GfRange3d TestStage::computeStageBounds() const
{
    pxr::TfTokenVector purposes;
    purposes.push_back(pxr::UsdGeomTokens->default_);
    purposes.push_back(pxr::UsdGeomTokens->proxy);
    bool useExtentHints = false;
    pxr::UsdGeomBBoxCache bboxCache(pxr::UsdTimeCode::Default(), purposes, useExtentHints);
    pxr::GfBBox3d bbox = bboxCache.ComputeWorldBound(stage()->GetPseudoRoot());

    return bbox.ComputeAlignedRange();
}

std::vector<char> readDataFile(const std::string& filename)
{
    const auto dataPath        = getAssetsDataFolder();
    const std::string filePath = (dataPath / filename).string();

    // Open the file.
    std::basic_ifstream<char> file(filePath, std::ios::binary);

    // Read the file content.
    return std::vector<char>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path const& getOutputDataFolder()
{
    return outFullpath;
}

std::filesystem::path const& getAssetsDataFolder()
{
    return inAssetsPath;
}

std::filesystem::path const& getBaselineFolder()
{
    return inBaselinePath;
}

void _SetBaselineFolder(std::filesystem::path const& inputPath)
{
    inBaselinePath = inputPath;
}

void TestContext::run(std::function<bool()> render, hvt::FramePass* framePass)
{
    _backend->run(render, framePass);
}

void TestContext::run(TestHelpers::TestStage& stage, hvt::Viewport* viewport, size_t frameCount)
{
    // Reset the viewport.

    hvt::RenderIndexProxyPtr renderIndex;
    viewport->Create(renderIndex, _is3DCamera);

    // Render the viewport.

    auto render = [&]()
    {
        // Update the viewport

        hvt::ViewParams viewInfo;
        viewInfo.viewMatrix       = stage.viewMatrix();
        viewInfo.projectionMatrix = stage.projectionMatrix();
        viewInfo.is3DCamera       = _is3DCamera;
        viewInfo.lights           = stage.defaultLights();
        viewInfo.material         = stage.defaultMaterial();
        viewInfo.ambient          = stage.defaultAmbient();

        hvt::ModelParams modelInfo;
        modelInfo.worldExtent = stage.computeStageBounds();

        viewport->Update(viewInfo, modelInfo, _enableFrameCancellation, _usePresentationTask);
        viewport->Render();

        return --frameCount > 0;
    };

    // Needs to get the AOV buffers of the last frame pass.
    _backend->run(render, viewport->GetLastFramePass());
}

bool TestContext::validateImages(const std::string& computedImageName, const std::string& imageFile,
    const uint8_t threshold, const uint16_t pixelCountThreshold)
{
    if (!_backend->saveImage(computedImageName))
    {
        return false;
    }
    return _backend->compareImage(computedImageName, imageFile, threshold, pixelCountThreshold);
}

FramePassInstance FramePassInstance::CreateInstance(std::string const& rendererName,
    pxr::UsdStageRefPtr& stage, std::shared_ptr<TestHelpers::HydraRendererContext>& backend,
    std::string const& uid)
{
    FramePassInstance framePass;

    // Creates the render index.

    hvt::RendererDescriptor renderDesc;
    renderDesc.hgiDriver    = &backend->hgiDriver();
    renderDesc.rendererName = rendererName;
    hvt::ViewportEngine::CreateRenderer(framePass.renderIndex, renderDesc);

    // Creates the scene index using the same stage.

    framePass.sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage);
    framePass.renderIndex->RenderIndex()->InsertSceneIndex(
        framePass.sceneIndex, pxr::SdfPath::AbsoluteRootPath());

    // Creates the frame pass instance.

    hvt::FramePassDescriptor passDesc;
    passDesc.renderIndex     = framePass.renderIndex->RenderIndex();
    passDesc.uid             = pxr::SdfPath(uid);
    framePass.sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);

    return framePass;
}

FramePassInstance FramePassInstance::CreateInstance(
    pxr::UsdStageRefPtr& stage, std::shared_ptr<TestHelpers::HydraRendererContext>& backend)
{
    return CreateInstance("HdStormRendererPlugin", stage, backend, "/SceneFramePass");
}

ScopedBaselineContextFolder::ScopedBaselineContextFolder(
    std::filesystem::path const& baselineFolder)
{
    _previousBaselinePath = TestHelpers::getBaselineFolder();
    _SetBaselineFolder(baselineFolder);
}

ScopedBaselineContextFolder::~ScopedBaselineContextFolder()
{
    _SetBaselineFolder(_previousBaselinePath);
}

} // namespace TestHelpers
