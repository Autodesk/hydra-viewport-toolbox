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

#pragma once

#include <RenderingFramework/iOSTestHelpers.h>

// glew.h has to be included first
#if TARGET_OS_IPHONE == 0 && !defined(__ANDROID__)
#include <GL/glew.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wunused-parameter"
#if __clang_major__ > 11
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#endif
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4275)
#pragma warning(disable : 4305)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif

#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/usd/stage.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <hvt/engine/framePass.h>
#include <hvt/engine/taskBackend.h>
#include <hvt/engine/viewport.h>

#include <RenderingFramework/GpuImageCapture.h>

#include <filesystem>
#include <functional>
#include <memory>

// To be used in gtests
#define STRINGIFY(x) #x
#define TOSTRING(x) std::string(STRINGIFY(x))

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

// Some global color definitions.
const pxr::GfVec4f ColorBlackNoAlpha = pxr::GfVec4f(0.0f, 0.0f, 0.0f, 0.0f);
const pxr::GfVec4f ColorDarkGrey     = pxr::GfVec4f(0.025f, 0.025f, 0.025f, 1.0f);
const pxr::GfVec4f ColorYellow       = pxr::GfVec4f(1.0f, 1.0f, 0.0f, 1.0f);
const pxr::GfVec4f ColorWhite        = pxr::GfVec4f(1.0f, 1.0f, 1.0f, 1.0f);
const pxr::GfVec4f ColorRed          = pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f);
const pxr::GfVec4f ColorGreen        = pxr::GfVec4f(0.0f, 1.0f, 0.0f, 1.0f);
const pxr::GfVec4f ColorBlue         = pxr::GfVec4f(0.0f, 0.0f, 1.0f, 1.0f);

std::vector<char> readDataFile(const std::string& filename);

/// Gets the path to the output directory where to find generated rendering images.
std::filesystem::path const& getOutputDataFolder();

/// Gets the path to the data directory where to find various scene files and other assets.
/// \note With more than one registered data root (see \c AddTestDataRoot) this returns the first
/// (primary) root's assets folder. Prefer \c ResolveAssetPath to locate a specific file, as it
/// searches every root.
std::filesystem::path const& getAssetsDataFolder();

/// Gets the path to the data directory where to find baseline images.
/// \note With more than one registered data root this returns the first (primary) root's baseline
/// folder. Prefer \c ResolveBaselinePath to locate a specific file.
std::filesystem::path const& getBaselineFolder();

/// Resolves \p relative against \c <root>/data/assets for each registered data root, in
/// registration order, and returns the first path that exists on disk. When no root contains the
/// file (or none is registered) the path under the first root is returned, so callers still get a
/// meaningful path for their error message.
std::filesystem::path ResolveAssetPath(std::filesystem::path const& relative);

/// Resolves \p relative against \c <root>/data/baselines for each registered data root, in
/// registration order, and returns the first path that exists on disk. Falls back to the first
/// root when the file is absent everywhere (see \c ResolveAssetPath).
std::filesystem::path ResolveBaselinePath(std::filesystem::path const& relative);

/// Gets the path to the HVT public resource directory.
std::filesystem::path const& getPublicResourceFolder();

/// Registers an additional data root the framework searches when resolving assets and baselines.
/// Each root is expected to hold a \c data/assets and a \c data/baselines subdirectory. Roots are
/// searched in registration order and the first one that contains a requested file wins (see
/// \c ResolveAssetPath / \c ResolveBaselinePath), so disjoint data trees can coexist without being
/// copied or merged.
///
/// The framework seeds the list at static-initialization time with its compile-time default (HVT's
/// own layout), keeping HVT's top-level test run zero-config. Downstream projects that embed this
/// framework and combine their own tests with HVT's should call this from their test binary's
/// \c main() -- before \c RUN_ALL_TESTS() -- to add their own data tree. Because resolution is
/// per-file and falls through missing roots, the executable runs standalone with no environment
/// setup, and a stray \c HVT_TEST_DATA_PATH cannot break lookups.
void AddTestDataRoot(std::filesystem::path const& root);

/// Resets the search list to a single \p root. Equivalent to clearing all registered roots and
/// calling \c AddTestDataRoot(root). Retained for callers that only ever need one root.
void SetTestDataRoot(std::filesystem::path const& root);

/// \brief Destroys the Hgi instances shared between test contexts.
/// \note Test binaries must call this after RUN_ALL_TESTS() and before tearing down their
/// windowing system (SDL/GLFW). The shared devices cannot be destroyed by static destructors
/// because those run after main() returns, i.e. after the windowing system is already gone.
/// Not calling it is safe but leaks the devices until process exit.
void releaseSharedHgi();

/// Base class for the OpenGL and Metal context renderers.
class HydraRendererContext
{
public:
    virtual ~HydraRendererContext() {}

    int width() const { return _width; }
    int height() const { return _height; }
    bool presentationEnabled() const { return _presentationEnabled; }

    virtual void run(std::function<bool()> render, hvt::FramePass* framePass) = 0;
    virtual void shutdown()                                                   = 0;
    virtual void waitForGPUIdle()                                             = 0;

    static std::string readImage(
        const std::string& fileName, int& width, int& height, int& channels);

    [[nodiscard]] pxr::HdDriver& hgiDriver() { return _hgiDriver; }
    [[nodiscard]] pxr::Hgi* hgi() { return _hgi.get(); }

    /// As there are slight differences between the platforms, the filename is adjusted to
    /// get the right filepath.
    static std::string getFilename(
        const std::filesystem::path& filePath, const std::string& filename);

    /// Compare image against stored "_computed" image and throws if a difference is found within
    /// the thresholds defined.
    virtual bool compareImages(const std::string& fileName, const uint8_t threshold = 1,
        const uint16_t pixelCountThreshold = 0);

    /// Compare image against stored "_computed" image and throws if a difference is found within
    /// the threshold defined.
    virtual bool compareImage(const std::string& computedFilename,
        const std::string& baselineFilename, const uint8_t threshold = 1,
        const uint16_t pixelCountThreshold = 1);

    /// Compare two "_computed" images and throws if a difference is found within the thresholds
    /// defined
    virtual bool compareOutputImages(const std::string& fileName1, const std::string& fileName2,
        const uint8_t threshold = 1, const uint16_t pixelCountThreshold = 0);

    /// Captures the color texture from the given frame pass.
    /// \param framePass The frame pass to capture the color texture from.
    void captureColorTexture(hvt::FramePass* framePass);

    /// Saves the captured color texture to a file.
    /// \note This method must be called after captureColorTexture.
    /// \param fileName The name of the file to save the captured color texture to.
    /// \return True if the image was saved successfully, false otherwise.
    [[nodiscard]] bool saveImage(std::string const& fileName);

protected:
    /// The Hgi backing this context. Shared instances are co-owned with the process-global
    /// cache in TestHelpers.cpp, so this is a shared_ptr rather than pxr::HgiUniquePtr.
    std::shared_ptr<pxr::Hgi> _hgi;
    bool _presentationEnabled = true;

    /// The image capture object.
    GpuImageCapture _imageCapture;

protected:
    HydraRendererContext(int w, int h) : _width(w), _height(h) {}
    void createHGI(pxr::TfToken type = pxr::TfToken(""));
    void destroyHGI();

    /// \brief Compares two images and throws an exception on the first difference greater than the
    /// threshold.
    /// \note It requires full paths.
    /// \param fileName1 The first file to compare.
    /// \param fileName2 The second file to compare.
    /// \param threshold The comparison threshold for pixel value differences.
    /// \param pixelCountThreshold The comparison threshold for pixel count differences.
    virtual bool compareImages(const std::string& fileName1, const std::string& fileName2,
        const uint8_t threshold, const uint16_t pixelCountThreshold);

private:
    int _width  = 1;
    int _height = 1;

    pxr::HdDriver _hgiDriver;
};

class TestView
{
public:
    TestView(std::shared_ptr<HydraRendererContext>& context);
    virtual ~TestView() {}

    bool updateCameraAndLights(pxr::GfRange3d const& world);

    pxr::GlfSimpleMaterial const& defaultMaterial() const { return _defaultMaterial; }
    pxr::GlfSimpleLightVector const& defaultLights() const { return _defaultLights; }
    pxr::GfVec4f const& defaultAmbient() const { return _ambient; }

    pxr::GfMatrix4d const& viewMatrix() const { return _viewMatrix; }
    pxr::GfMatrix4d const& projectionMatrix() const { return _projectionMatrix; }

private:
    std::shared_ptr<HydraRendererContext> _context;
    pxr::GlfSimpleMaterial _defaultMaterial;
    pxr::GlfSimpleLightVector _defaultLights;
    pxr::GfVec4f _ambient = pxr::GfVec4f(0.1f, 0.1f, 0.1f, 0.0f);
    pxr::GfMatrix4d _viewMatrix;
    pxr::GfMatrix4d _projectionMatrix;
};

class TestStage : public TestView
{
public:
    TestStage(std::shared_ptr<HydraRendererContext>& context, bool createSessionLayer = true);
    virtual ~TestStage();

    bool open(const std::string& path);

    /// Gets the stage.
    const pxr::UsdStageRefPtr& stage() const { return _stage; }
    /// Gets the stage.
    pxr::UsdStageRefPtr& stage() { return _stage; }

    /// Gets the boundaries of the stage.
    pxr::GfRange3d computeStageBounds() const;

private:
    /// Adds or not a temporary session layer to allow stage cleanup of all edits
    /// between unit tests.
    bool _createSessionLayer = true;

    pxr::UsdStageRefPtr _stage;

    /// The added session layer.
    pxr::SdfLayerRefPtr _sessionLayer;
};

class TestContext
{
public:
    TestContext() = default;
    TestContext(int w, int h) : _width(w), _height(h) {}
    virtual ~TestContext()
    {
        if (_backend)
        {
#if PXR_VERSION > 2511
            _backend->waitForGPUIdle();
#endif
            _backend->shutdown();
            _backend = nullptr;
        }
    }

    int width() const { return _width; }
    int height() const { return _height; }
    bool presentationEnabled() const { return _usePresentationTask; }
    std::shared_ptr<TestHelpers::HydraRendererContext>& backend() { return _backend; }

    // Render a single frame pass.
    void run(std::function<bool()> render, hvt::FramePass* framePass);
    // Render a viewport i.e., several frame passes.
    void run(TestHelpers::TestStage& stage, hvt::Viewport* viewport, size_t frameCount);

    bool validateImages(const std::string& computedImageName, const std::string& imageFile,
        const uint8_t threshold = 1, const uint16_t pixelCountThreshold = 1);

public:
    // The GPU backend used by the unit test.
    std::shared_ptr<TestHelpers::HydraRendererContext> _backend;
    // The USD scene file to load.
    std::string _sceneFilepath;

protected:
    /// Initialize the backend.
    virtual void init() = 0;

    int _width { 300 };
    int _height { 200 };

    bool _is3DCamera { true };
    bool _enableFrameCancellation { false };
    bool _usePresentationTask { true };
};

/// An instance of this class will set the baseline folder to the given path and restore the
/// previous one when it goes out of scope.
class ScopedBaselineContextFolder
{
public:
    /// Creates a scoped baseline context folder.
    /// \param baselineFolder The new baseline folder to set, for the duration of the scope.
    ScopedBaselineContextFolder(std::filesystem::path const& baselineFolder);
    ~ScopedBaselineContextFolder();

private:
    std::filesystem::path _previousBaselinePath;
};

/// An instance of this class selects the rendering backend (scene-index or scene-delegate) via
/// hvt::SetUseLegacySceneDelegate() and restores the previous selection when it goes out of scope.
/// Useful to run a test body against the scene-delegate (SD) backend without leaking the
/// process-global backend selection into subsequent tests.
class ScopedSceneDelegateMode
{
public:
    /// Selects the backend for the duration of the scope.
    /// \param useLegacySceneDelegate true for the legacy scene-delegate (SD) backend, false for
    ///        the scene-index (SI) backend.
    explicit ScopedSceneDelegateMode(bool useLegacySceneDelegate);
    ~ScopedSceneDelegateMode();

private:
    bool _previousUseLegacySceneDelegate;
};

/// Holds default variables when creating a frame pass for a unit test.
struct FramePassInstance
{
    hvt::RenderIndexProxyPtr renderIndex;
    pxr::HdSceneIndexBaseRefPtr sceneIndex;
    hvt::FramePassPtr sceneFramePass;

    /// Creates an frame pass instance for a dedicated render delegate.
    /// \param rendererName The plugin name of the render delegate to use.
    /// \param stage The model to use by the render index.
    /// \param backend The backend used to render the scene.
    /// \param uid The optional unique identifier for the frame pass instance.
    /// \note The uid is only needed when two frame pass instances are using the same render index
    /// instance.
    /// \return A frame pass instance useful in a unit test context.
    static FramePassInstance CreateInstance(std::string const& rendererName,
        pxr::UsdStageRefPtr& stage, std::shared_ptr<TestHelpers::HydraRendererContext>& backend,
        std::string const& uid = "/SceneFramePass");

    /// Creates an frame pass instace.
    /// \param stage The model to use by the render index.
    /// \param backend The backend used to render the scene.
    /// \return A frame pass instance useful in a unit test context.
    static FramePassInstance CreateInstance(
        pxr::UsdStageRefPtr& stage, std::shared_ptr<TestHelpers::HydraRendererContext>& backend);
};

} // namespace TestHelpers
