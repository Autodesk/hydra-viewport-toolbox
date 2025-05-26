//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//
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
#endif

#include <hvt/engine/viewport.h>

// Forward declaration.
namespace HVT_NS
{
class FramePass;
} // namespace HVT_NS

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

std::vector<char> readDataFile(const std::string& filename);

/// Gets the path to the output directory where to find generated rendering images.
std::filesystem::path const& getOutputDataFolder();

/// Gets the path to the data directory where to find various scene files.
std::filesystem::path const& getInputDataFolder();

/// Gets the path to the data directory where to find baseline images.
std::filesystem::path const& getBaselineFolder();

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

class TestStage : public TestView
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

class TestContext
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
    std::shared_ptr<TestHelpers::HydraRendererContext>& backend() { return _backend; }

    // Render a single frame pass.
    void run(std::function<bool()> render, hvt::FramePass* framePass);
    // Render a viewport i.e., several frame passes.
    void run(TestHelpers::TestStage& stage, hvt::Viewport* viewport, size_t frameCount);

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
