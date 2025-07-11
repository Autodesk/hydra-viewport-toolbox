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

#include <hvt/api.h>

// glew.h has to be included first
#if TARGET_OS_IPHONE == 0 && !defined(__ANDROID__)
#include <GL/glew.h>
#endif

// clang-format off
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
// clang-format on

#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/usd/stage.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// clang-format on

#include <hvt/engine/framePass.h>
#include <hvt/engine/viewport.h>

#include <filesystem>
#include <functional>
#include <memory>

// To be used in gtests
#define STRINGIFY(x) #x
#define TOSTRING(x) std::string(STRINGIFY(x))

namespace HVT_NS
{

namespace TestFramework
{

// Some global color definitions.
const pxr::GfVec4f ColorBlackNoAlpha = pxr::GfVec4f(0.0f, 0.0f, 0.0f, 0.0f);
const pxr::GfVec4f ColorDarkGrey     = pxr::GfVec4f(0.025f, 0.025f, 0.025f, 1.0f);
const pxr::GfVec4f ColorYellow       = pxr::GfVec4f(1.0f, 1.0f, 0.0f, 1.0f);
const pxr::GfVec4f ColorWhite        = pxr::GfVec4f(1.0f, 1.0f, 1.0f, 1.0f);

HVT_API extern std::vector<char> readDataFile(const std::string& filename);

/// Gets the path to the output directory where to find generated rendering images.
HVT_API extern std::filesystem::path const& getOutputDataFolder();

/// Gets the path to the data directory where to find various scene files and other assets.
HVT_API extern std::filesystem::path const& getAssetsDataFolder();

/// Gets the path to the data directory where to find baseline images.
HVT_API extern std::filesystem::path const& getBaselineFolder();

/// Base class for the OpenGL and Metal context renderers.
class HydraRendererContext
{
public:
    virtual ~HydraRendererContext() {}

    int width() const { return _width; }
    int height() const { return _height; }
    bool presentationEnabled() const { return _presentationEnabled; }

    virtual void run(std::function<bool()> render, hvt::FramePass* framePass) = 0;
    virtual bool saveImage(const std::string& fileName)                       = 0;
    virtual void shutdown()                                                   = 0;

    static std::string readImage(
        const std::string& fileName, int& width, int& height, int& channels);

    [[nodiscard]] pxr::HdDriver& hgiDriver() { return _hgiDriver; }
    [[nodiscard]] pxr::Hgi* hgi() { return _hgi.get(); }

    /// As there are slight differences between the platforms, the filename is adjusted to
    /// get the right filepath.
    static std::string getFilename(
        const std::filesystem::path& filePath, const std::string& filename);

    /// Compare image against stored "_computed" image and throws if a difference is found within
    /// the threshold defined.
    virtual bool compareImages(const std::string& fileName, const uint8_t threshold = 1);

    /// Compare two "_computed" images and throws if a difference is found within the threshold
    /// defined
    virtual bool compareOutputImages(
        const std::string& fileName1, const std::string& fileName2, const uint8_t threshold = 1);

    virtual void setDataPath(const std::filesystem::path& path) { _dataPath = path; }
    virtual const std::filesystem::path& dataPath() const { return _dataPath; }

protected:
    pxr::HgiUniquePtr _hgi;
    bool _presentationEnabled = true;

protected:
    HydraRendererContext(int w, int h) : _width(w), _height(h) {}
    void createHGI(pxr::TfToken type = pxr::TfToken(""));
    void destroyHGI();

    /// \brief Compares two images and throws an exception on the first difference greater than the
    /// threshold.
    /// \note It requires full paths.
    /// \param fileName1 The first file to compare.
    /// \param fileName2 The second file to compare.
    /// \param threshold The comparison threshold.
    virtual bool compareImages(
        const std::string& fileName1, const std::string& fileName2, const uint8_t threshold);

private:
    int _width  = 1;
    int _height = 1;

    std::filesystem::path _dataPath;

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

class HVT_API TestStage : public TestView
{
public:
    TestStage(std::shared_ptr<HydraRendererContext> context);
    virtual ~TestStage() {}

    bool open(const std::string& path);

    /// Gets the stage.
    const pxr::UsdStageRefPtr& stage() const { return _stage; }
    /// Gets the stage.
    pxr::UsdStageRefPtr& stage() { return _stage; }

    /// Gets the boundaries of the stage.
    pxr::GfRange3d computeStageBounds() const;

private:
    pxr::UsdStageRefPtr _stage;
};

class HVT_API TestContext
{
public:
    TestContext() = default;
    TestContext(int w, int h) : _width(w), _height(h) {}
    virtual ~TestContext()
    {
        if (_backend)
        {
            _backend->shutdown();
            _backend = nullptr;
        }
    }

    int width() const { return _width; }
    int height() const { return _height; }
    bool presentationEnabled() const { return _usePresentationTask; }
    const std::filesystem::path& dataPath() const { return _backend->dataPath(); }
    std::shared_ptr<HydraRendererContext>& backend() { return _backend; }

    /// Render a single frame pass.
    void run(std::function<bool()> render, hvt::FramePass* framePass);
    /// Render a viewport i.e., several frame passes.
    void run(TestStage& stage, hvt::Viewport* viewport, size_t frameCount);

public:
    /// The GPU backend used by the unit test.
    std::shared_ptr<HydraRendererContext> _backend;
    /// The USD scene file to load.
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

/// \brief A convenience wrapper for creating and managing frame pass instances in unit tests.
///
/// FramePassInstance encapsulates the three core components needed for rendering operations
/// in the HVT framework: a render index, scene index, and frame pass. This struct simplifies
/// the setup and management of these interdependent components for testing scenarios.
///
struct HVT_API FramePassInstance
{
    /// The render index proxy that manages rendering operations and coordinates between
    /// scene data and the rendering backend. This is the central hub for all rendering
    /// activities and holds references to render delegates, tasks, and buffers.
    hvt::RenderIndexProxyPtr renderIndex;

    /// The scene index containing the 3D scene data (geometry, materials, lights, cameras).
    /// This represents the scene in a form optimized for rendering operations and provides
    /// efficient access to scene primitives and their properties.
    pxr::HdSceneIndexBaseRefPtr sceneIndex;

    /// The frame pass that orchestrates the rendering pipeline. This component manages
    /// the sequence of rendering operations, including setup of render targets, execution
    /// of rendering tasks, and cleanup. It serves as the main interface for rendering
    /// operations in unit tests.
    hvt::FramePassPtr sceneFramePass;

    /// \brief Creates a frame pass instance with a specific render delegate.
    ///
    /// This factory method creates a complete frame pass instance by:
    /// 1. Creating a render index with the specified render delegate
    /// 2. Creating a scene index from the provided USD stage
    /// 3. Linking the scene index to the render index
    /// 4. Creating a frame pass instance that orchestrates the rendering pipeline
    ///
    /// \param rendererName The plugin name of the render delegate to use (e.g.,
    /// "HdStormRendererPlugin").
    /// \param stage The USD stage containing the 3D scene data to render.
    /// \param backend The HGI backend context that provides the graphics API abstraction.
    /// \param uid The unique identifier for the frame pass instance. This must be unique
    ///            when multiple frame pass instances share the same render index to avoid
    ///            task conflicts.
    ///
    /// \return A fully configured frame pass instance ready for rendering operations.
    static FramePassInstance CreateInstance(std::string const& rendererName,
        pxr::UsdStageRefPtr& stage, std::shared_ptr<HydraRendererContext>& backend,
        std::string const& uid = "/SceneFramePass");

    /// \brief Creates a frame pass instance using the default Storm renderer.
    ///
    /// This is a convenience method that creates a frame pass instance using the
    /// HdStormRendererPlugin (OpenGL-based renderer) with a default unique identifier.
    /// Equivalent to calling CreateInstance("HdStormRendererPlugin", stage, backend,
    /// "/SceneFramePass").
    ///
    /// \param stage The USD stage containing the 3D scene data to render.
    /// \param backend The HGI backend context that provides the graphics API abstraction.
    ///
    /// \return A fully configured frame pass instance using the Storm renderer.
    ///
    /// \see CreateInstance(std::string const&, pxr::UsdStageRefPtr&,
    /// std::shared_ptr<HydraRendererContext>&, std::string const&)
    static FramePassInstance CreateInstance(
        pxr::UsdStageRefPtr& stage, std::shared_ptr<HydraRendererContext>& backend);
};

} // namespace TestFramework

} // namespace HVT_NS
