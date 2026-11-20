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

#include <RenderingFramework/TestFlags.h>
#include <RenderingFramework/TestHelpers.h>

#include <RenderingFramework/ImageUtils.h>

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
#include <pxr/base/tf/getenv.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/tokens.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{

// Ordered lists of folders searched when resolving assets and baseline images. Resolution walks
// each list in registration order and returns the first entry that actually contains the requested
// file (see resolveIn / ResolveAssetPath / ResolveBaselinePath). AddTestDataRoot appends more
// folders at run time so disjoint data trees coexist without copying.
std::vector<std::filesystem::path> makeAssetFolders();
std::vector<std::filesystem::path> makeBaselineFolders();

std::vector<std::filesystem::path> gAssetFolders    = makeAssetFolders();
std::vector<std::filesystem::path> gBaselineFolders = makeBaselineFolders();

// The output and resource paths carry no build-tree path in the installed static library; each
// falls back to the compile-time default baked by the framework's CMakeLists (HVT's own layout)
// when the environment variable is unset, keeping HVT's own tests zero-config.
const std::string outputRoot =
    pxr::TfGetenv("HVT_TEST_DATA_OUTPUT_PATH", TOSTRING(HVT_TEST_DATA_OUTPUT_PATH));
const std::filesystem::path outFullpath = outputRoot + "/computed";
const std::filesystem::path resFullpath =
    pxr::TfGetenv("HVT_RESOURCE_PATH", TOSTRING(HVT_RESOURCE_PATH));

// Seeds with the compile-time default root (HVT's own layout). If HVT_TEST_DATA_PATH is set
// it is appended as an EXTRA root rather than replacing the default, so per-file resolution falls
// through to the real data even when a stray value is present in the environment.
std::vector<std::filesystem::path> makeDesktopRoots()
{
    std::vector<std::filesystem::path> roots;
    roots.emplace_back(TOSTRING(HVT_TEST_DATA_PATH));
    const std::string env = pxr::TfGetenv("HVT_TEST_DATA_PATH", "");
    if (!env.empty())
        roots.emplace_back(env);
    return roots;
}
std::vector<std::filesystem::path> makeAssetFolders()
{
    std::vector<std::filesystem::path> folders;
    for (auto const& root : makeDesktopRoots())
    {
        folders.push_back(root / "data" / "assets");
    }
    return folders;
}
std::vector<std::filesystem::path> makeBaselineFolders()
{
    std::vector<std::filesystem::path> folders;
    for (auto const& root : makeDesktopRoots())
    {
        folders.push_back(root / "data" / "baselines");
    }
    return folders;
}

// Searches a folder list for the first entry that contains \p relative; falls back to the first
// folder so callers always get a usable path for diagnostics.
std::filesystem::path resolveIn(
    std::vector<std::filesystem::path> const& folders, std::filesystem::path const& relative)
{
    for (auto const& folder : folders)
    {
        std::filesystem::path candidate = folder / relative;
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }
    }
    return folders.empty() ? relative : folders.front() / relative;
}

bool beginsWithUpperCase(const std::string& name)
{
    // Some files maybe nested under folders -- extract the filename from the folder.
    const std::string stem = std::filesystem::path(name).filename().string();
    if (stem.empty())
    {
        return false;
    }

    return stem[0] == static_cast<char>(std::toupper(stem[0]));
}

// Converts the filename component to camel case (first letter lower case, remainder untouched).
// Handles paths with directory prefixes (e.g. "origin_dev/02511/TestName" ->
// "origin_dev/02511/testName").
std::string toCamelCase(const std::string& filename)
{
    std::filesystem::path p(filename);
    std::string stem = p.filename().string();
    if (!stem.empty())
    {
        stem[0] = static_cast<char>(std::tolower(stem[0]));
        p       = p.parent_path() / stem;
    }
    return p.string();
}

// Returns the path of \p stem (a file name without extension) under \p folder when it exists,
// trying the camel case variant as well: many unit tests use their test name as the baseline image
// name, but the casing has not yet been standardized for all test names and baseline images.
// TODO: Remove the camel case variant once the casing of all assets is standardized.
std::optional<std::filesystem::path> findImage(
    std::filesystem::path const& folder, std::string const& stem)
{
    std::error_code ec;
    const std::string path = TestHelpers::HydraRendererContext::getFilename(folder, stem);
    if (std::filesystem::exists(path, ec))
    {
        return std::filesystem::path(path);
    }
    if (beginsWithUpperCase(stem))
    {
        const std::string camelCase =
            TestHelpers::HydraRendererContext::getFilename(folder, toCamelCase(stem));
        if (std::filesystem::exists(camelCase, ec))
        {
            return std::filesystem::path(camelCase);
        }
    }
    return std::nullopt;
}

// Locates a baseline image by logical name across every registered baseline folder. In each
// folder, the most precise match wins:
//
//   <name>_<OS>_<API>.png   this OS and this API
//   <name>_<API>_<OS>.png   this API on this OS
//   <name>_<OS>.png         every API on this OS
//   <name>_<API>.png        this API on every OS
//   <name>.png              everything
//
// One image is normally right everywhere, so the unsuffixed name is the expected case and a
// suffixed one is an override for a test that genuinely renders differently. That way a new API
// or OS inherits the shared baseline rather than silently getting none. OS outranks API because it
// is an imperfect proxy for the hardware (the CI runs macOS on Apple silicon and the others on
// Nvidia).
//
// Returns the first existing match, else the unsuffixed name under the first folder for a
// sensible error.
std::string resolveBaselineFilename(std::string const& fileName)
{
    const std::string api = TestHelpers::GetBackendSuffix(TestHelpers::GetRenderingBackend());
    const std::string os  = TestHelpers::GetPlatformSuffix();

    for (auto const& folder : gBaselineFolders)
    {
        for (std::string const& stem :
            { fileName + os + api, fileName + api + os, fileName + os, fileName + api, fileName })
        {
            if (auto found = findImage(folder, stem))
            {
                return found->string();
            }
        }
    }
    return TestHelpers::HydraRendererContext::getFilename(
        gBaselineFolders.empty() ? std::filesystem::path {} : gBaselineFolders.front(), fileName);
}

// The backend selected by InitRenderingBackend(); empty until resolved.
pxr::TfToken& selectedBackend()
{
    static pxr::TfToken backend;
    return backend;
}

// Maps a case-insensitive backend name to its Hgi token.
pxr::TfToken parseBackendName(std::string const& name)
{
    auto lower = [](std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return str;
    };

    const std::string requested = lower(name);
    for (pxr::TfToken const& token :
        { pxr::HgiTokens->OpenGL, pxr::HgiTokens->Metal, pxr::HgiTokens->Vulkan })
    {
        if (lower(token.GetString()) == requested)
        {
            return token;
        }
    }
    throw std::invalid_argument(
        "Unknown Hgi backend '" + name + "': expected one of OpenGL, Metal or Vulkan.");
}

// Creating a Hgi is expensive: HgiVulkan spins up a VkInstance and VkDevice
// (with validation layers in Debug builds) and HgiMetal creates an MTLDevice and
// command queue. Every fresh device also throws away the render delegate's
// compiled shader and pipeline caches. Since hundreds of parameterized tests
// each build and tear down their own context, that cost is paid over and over
// for identical work.
//
// A single Hgi instance per backend is therefore created lazily and reused
// across all test contexts in the process, so the device and its warm
// shader/pipeline caches survive between tests. Only backends whose device is
// independent of the per-test window are shared: Vulkan and Metal both create
// the device up front and attach a throw-away per-test surface/drawable, so the
// shared device safely outlives each window. OpenGL is excluded because HgiGL's
// cached GPU objects live inside the GL context.
//
// Note that this cache is only consulted for the shareable backends (Vulkan and
// Metal) -- not for OpenGL. Creating the Hgi itself is cheap (~0.1 ms when
// measured on macOS) next to creating the window and GL context (~12 ms), which
// OpenGLWindow addresses separately by borrowing a process-wide context.
//
// Sharing is enabled by default; set HVT_TEST_SHARE_HGI=0 to opt out (e.g. when
// debugging a test that is sensitive to residual device state).

using SharedHgiCache =
    std::unordered_map<pxr::TfToken, std::shared_ptr<pxr::Hgi>, pxr::TfToken::HashFunctor>;

SharedHgiCache& sharedHgiCache()
{
    static SharedHgiCache cache;
    return cache;
}

bool shareHgiEnabled()
{
    static const bool enabled = pxr::TfGetenvBool("HVT_TEST_SHARE_HGI", true);
    return enabled;
}

// Only backends whose GPU device is independent of the per-test window can be
// safely shared process-wide (see the note above). OpenGL is excluded.
bool isShareableBackend(const pxr::TfToken& type)
{
    return type == pxr::HgiTokens->Vulkan || type == pxr::HgiTokens->Metal;
}

} // anonymous namespace

namespace TestHelpers
{

std::string HydraRendererContext::readImage(
    const std::string& fileName, int& width, int& height, int& channels)
{
    const std::string filePath = ResolveAssetPath(fileName).string();
    return RenderingUtils::readImage(filePath, width, height, channels);
}

void HydraRendererContext::captureColorTexture(hvt::FramePass* framePass)
{
    _imageCapture.capture(framePass, _hgi.get());
}

bool HydraRendererContext::saveImage(std::string const& fileName)
{
    static const std::filesystem::path filePath = getOutputDataFolder();
    const std::filesystem::path screenShotPath  = getFilename(filePath, fileName + "_computed");
    return _imageCapture.writePng(screenShotPath);
}

std::string HydraRendererContext::getFilename(
    const std::filesystem::path& filePath, const std::string& filename)
{
    return filePath.string() + "/" + filename + ".png";
}

bool HydraRendererContext::compareImages(const std::string& fileName, const uint8_t threshold,
    const uint16_t pixelCountThreshold, bool perceptual)
{
    const std::string inFile  = resolveBaselineFilename(fileName);
    const std::string outFile = getFilename(outFullpath, fileName + "_computed");

    return compareImages(inFile, outFile, threshold, pixelCountThreshold, perceptual);
}

bool HydraRendererContext::compareImage(const std::string& computedFilename,
    const std::string& baselineFilename, const uint8_t threshold,
    const uint16_t pixelCountThreshold, bool perceptual)
{
    const std::string baseline = resolveBaselineFilename(baselineFilename);
    const std::string computed = getFilename(outFullpath, computedFilename + "_computed");
    return compareImages(computed, baseline, threshold, pixelCountThreshold, perceptual);
}

bool HydraRendererContext::compareOutputImages(const std::string& fileName1,
    const std::string& fileName2, const uint8_t threshold, const uint16_t pixelCountThreshold,
    bool perceptual)
{
    const std::string file1 = getFilename(outFullpath, fileName1 + "_computed");
    const std::string file2 = getFilename(outFullpath, fileName2 + "_computed");

    return compareImages(file1, file2, threshold, pixelCountThreshold, perceptual);
}

bool HydraRendererContext::compareImages(const std::string& inFile, const std::string& outFile,
    const uint8_t threshold, const uint16_t pixelCountThreshold, bool perceptual)
{
    return RenderingUtils::compareImages(
        inFile, outFile, threshold, pixelCountThreshold, perceptual);
}

void HydraRendererContext::createHGI(pxr::TfToken const& type)
{
    // Only backends whose device is independent of the per-test window benefit
    // from (and are safe for) process-wide sharing. The cache is keyed by backend
    // type so a process that creates multiple backends never borrows the wrong one.
    const bool canShare = shareHgiEnabled() && isShareableBackend(type);
    auto& cache         = sharedHgiCache();
    const auto cached   = canShare ? cache.find(type) : cache.end();

    if (cached != cache.end())
    {
        // Co-own the already-created shared instance.
        _hgi = cached->second;
    }
    else
    {
        // An empty token creates the OpenUSD platform default.
        _hgi = pxr::Hgi::CreateNamedHgi(type);
    }

    if (!_hgi)
    {
        throw std::runtime_error("Failed to create the '" + type.GetString() +
            "' Hgi: the OpenUSD build may not include this backend.");
    }

    if (!_hgi->IsBackendSupported())
    {
        _hgi.reset();
        throw std::runtime_error("HGI initialization succeeded but backend is not supported!");
    }

    // Promote a freshly created shareable instance into the process-global cache.
    if (canShare && cached == cache.end())
    {
        cache.emplace(type, _hgi);
    }

    // The driver is a per-context view onto the (possibly shared) Hgi; createHGI runs once per
    // context construction, so it is always initialized here.
    _hgiDriver.name   = pxr::HgiTokens->renderDriver;
    _hgiDriver.driver = pxr::VtValue(_hgi.get());
}

void HydraRendererContext::destroyHGI()
{
    // A shared instance is co-owned by the process-global cache, so dropping this
    // context's reference keeps it alive (and warm) for the next test. An unshared
    // instance is destroyed here, as this reference is the only one.
    _hgi.reset();
    _hgiDriver = {};
}

void releaseSharedHgi()
{
    sharedHgiCache().clear();
}

pxr::TfToken GetPlatformDefaultBackend()
{
    // Mirrors pxr::Hgi::CreatePlatformDefaultHgi(), which only exposes the created instance.
    if (pxr::TfGetenvBool("HGI_ENABLE_VULKAN", false))
    {
        return pxr::HgiTokens->Vulkan;
    }
#if defined(__APPLE__)
    return pxr::HgiTokens->Metal;
#else
    return pxr::HgiTokens->OpenGL;
#endif
}

void InitRenderingBackend(int* argc, char** argv)
{
    static constexpr std::string_view kFlag = "--hgi=";

    std::string requested;
    if (argc && argv)
    {
        int kept = 1;
        for (int i = 1; i < *argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if (arg.substr(0, kFlag.size()) == kFlag)
            {
                requested = std::string(arg.substr(kFlag.size()));
                continue;
            }
            argv[kept++] = argv[i];
        }
        *argc = kept;
    }

    if (requested.empty())
    {
        requested = pxr::TfGetenv("HVT_TEST_HGI", "");
    }

    selectedBackend() =
        requested.empty() ? GetPlatformDefaultBackend() : parseBackendName(requested);
}

bool RunSkippedTests()
{
    static const bool enabled = pxr::TfGetenvBool("HVT_TEST_RUN_SKIPPED", false);
    return enabled;
}

pxr::TfToken const& GetRenderingBackend()
{
    if (selectedBackend().IsEmpty())
    {
        InitRenderingBackend(nullptr, nullptr);
    }
    return selectedBackend();
}

std::string GetPlatformSuffix()
{
#if defined(_WIN32)
    return "_win";
#elif defined(__APPLE__)
    return "_osx";
#else
    return "_linux";
#endif
}

std::string GetBackendSuffix(pxr::TfToken const& backend)
{
    return "_" + backend.GetString();
}

TestView::TestView(std::shared_ptr<HydraRendererContext>& context) : _context(context)
{
    // Tell the Viewport Toolbox where to find its resources.
    hvt::SetResourceDirectory(getPublicResourceFolder());

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
    const std::string filePath = ResolveAssetPath(filename).string();

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
    // The primary (first) root's assets folder. Prefer ResolveAssetPath to locate a specific file.
    return gAssetFolders.front();
}

std::filesystem::path const& getBaselineFolder()
{
    return gBaselineFolders.front();
}

std::filesystem::path const& getPublicResourceFolder()
{
    return resFullpath;
}

std::filesystem::path ResolveAssetPath(std::filesystem::path const& relative)
{
    return resolveIn(gAssetFolders, relative);
}

std::filesystem::path ResolveBaselinePath(std::filesystem::path const& relative)
{
    return std::filesystem::path(resolveBaselineFilename(relative.string()));
}

void _SetBaselineFolder(std::filesystem::path const& inputPath)
{
    // Override the primary baseline folder in place, leaving any additional roots for fall-through.
    if (gBaselineFolders.empty())
        gBaselineFolders.push_back(inputPath);
    else
        gBaselineFolders.front() = inputPath;
}

void AddTestDataRoot(std::filesystem::path const& root)
{
    // Each root contributes its data/assets and data/baselines folders to the search lists, kept in
    // registration order so the first root that owns a requested file wins (see resolveIn).
    gAssetFolders.push_back(root / "data" / "assets");
    gBaselineFolders.push_back(root / "data" / "baselines");
}

void SetTestDataRoot(std::filesystem::path const& root)
{
    // Reset to a single root, then register it -- retained for callers that only need one root.
    gAssetFolders.clear();
    gBaselineFolders.clear();
    AddTestDataRoot(root);
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
    const uint8_t threshold, const uint16_t pixelCountThreshold, bool perceptual)
{
    if (!_backend->saveImage(computedImageName))
    {
        return false;
    }
    return _backend->compareImage(
        computedImageName, imageFile, threshold, pixelCountThreshold, perceptual);
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

ScopedSceneDelegateMode::ScopedSceneDelegateMode(bool useLegacySceneDelegate) :
    _previousUseLegacySceneDelegate(hvt::UseLegacySceneDelegate())
{
    hvt::SetUseLegacySceneDelegate(useLegacySceneDelegate);
}

ScopedSceneDelegateMode::~ScopedSceneDelegateMode()
{
    hvt::SetUseLegacySceneDelegate(_previousUseLegacySceneDelegate);
}

} // namespace TestHelpers
