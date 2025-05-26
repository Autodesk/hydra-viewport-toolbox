//
// Copyright 2024 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <RenderingFramework/OpenGLTestContext.h>

#include <hvt/engine/hgiInstance.h>

#include <pxr/imaging/glf/glContext.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define STBI_MSC_SECURE_CRT
#endif

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <RenderingUtils/stb/stb_image.h>
#include <RenderingUtils/stb/stb_image_write.h>

#if __clang__
#pragma clang diagnostic pop
#endif

#include <filesystem>
#include <mutex>

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

// Recursive helper function to create a directory tree.
void createDirectoryTree(const std::filesystem::path& directoryTree)
{
    // Nothing to do, end recursion.
    if (std::filesystem::exists(directoryTree))
    {
        return;
    }

    // Make sure the parent directory exists.
    // Note: std::filesystem::create_directory can only create the leaf of the directory tree,
    //       hence the recursion.
    createDirectoryTree(directoryTree.parent_path());

    // Create the final directory.
    if (!std::filesystem::create_directory(directoryTree))
    {
        throw std::runtime_error(
            std::string("Failed to create the directory: ") + directoryTree.string());
    }
}

bool initGlew()
{
    static bool result = false;
    static std::once_flag once;
    std::call_once(once, []() {
        pxr::GlfSharedGLContextScopeHolder sharedGLContext;
        glewExperimental = GL_TRUE;
        result           = glewInit() == GLEW_OK;
    });

    return result;
}

constexpr unsigned int getGLMajorVersion()
{
#if defined(__APPLE__)
    return 2;
#else
    return 4;
#endif
}

constexpr unsigned int getGLMinorVersion()
{
#if defined(__APPLE__)
    return 1;
#else
    return 5;
#endif
}

constexpr bool isCoreProfile()
{
    return (getGLMajorVersion() > 2);
}

OpenGLWindow::OpenGLWindow(int w, int h)
{
    static constexpr unsigned int glMajor = getGLMajorVersion();
    static constexpr unsigned int glMinor = getGLMinorVersion();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, glMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, glMinor);

    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

    if (isCoreProfile())
    {
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

#ifdef _DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    _pGLFWWindow = glfwCreateWindow(w, h, "Test", nullptr, nullptr);
    if (!_pGLFWWindow)
    {
        throw std::runtime_error("Creation of an OpenGL " + std::to_string(glMajor) + "." +
            std::to_string(glMinor) + " context failed!");
    }
}

OpenGLWindow::~OpenGLWindow()
{
    destroy();
}

void OpenGLWindow::destroy()
{
    if (_pGLFWWindow)
    {
        glfwDestroyWindow(_pGLFWWindow);
        _pGLFWWindow = nullptr;
    }
}

void OpenGLWindow::swapBuffers() const
{
    glfwSwapBuffers(_pGLFWWindow);
}

void OpenGLWindow::makeContextCurrent() const
{
    glfwMakeContextCurrent(_pGLFWWindow);
}

bool OpenGLWindow::windowShouldClose() const
{
    return glfwWindowShouldClose(_pGLFWWindow) != 0;
}

void OpenGLWindow::setWindowShouldClose()
{
    glfwSetWindowShouldClose(_pGLFWWindow, GLFW_TRUE);
}

OpenGLRendererContext::OpenGLRendererContext(int w, int h) : HydraRendererContext(w, h), _glWindow(w, h)
{
    init();
    createHGI();
}

OpenGLRendererContext::~OpenGLRendererContext()
{
    destroyHGI();
    shutdown();
}

void OpenGLRendererContext::init()
{
    _glWindow.makeContextCurrent();

    initGlew();

    if (isCoreProfile())
    {
        glGenVertexArrays(1, &_vao);
    }
}

void OpenGLRendererContext::shutdown()
{
    if (isCoreProfile())
    {
        glDeleteVertexArrays(1, &_vao);
    }

    _glWindow.destroy();
}

void OpenGLRendererContext::beginGL()
{
    _glWindow.makeContextCurrent();
    glfwSwapInterval(1);

    glViewport(0, 0, width(), height());

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (isCoreProfile())
    {
        // We must bind a VAO (Vertex Array Object) because core profile
        // contexts do not have a default vertex array object. VAO objects are
        // container objects which are not shared between contexts, so we create
        // and bind a VAO here so that core rendering code does not have to
        // explicitly manage per-GL context state.
        glBindVertexArray(_vao);
    }
}

void OpenGLRendererContext::endGL()
{
    if (isCoreProfile())
    {
        glBindVertexArray(0);
    }

    _glWindow.swapBuffers();
}

void OpenGLRendererContext::run(
    std::function<bool()> render, hvt::FramePass* /* framePass */)
{
    while (!_glWindow.windowShouldClose())
    {
        bool moreFrames = true;
        while (moreFrames)
        {
            // To guarantee a correct cleanup even in case of errors.
            struct Guard
            {
                explicit Guard(OpenGLRendererContext* context) : _context(context)
                {
                    _context->beginGL();
                }
                ~Guard() { _context->endGL(); }
                OpenGLRendererContext* _context { nullptr };
            } guard(this);

            try
            {
                moreFrames = render();
            }
            catch (const std::exception& ex)
            {
                throw std::runtime_error(
                    std::string("Failed to render the frame pass: ") + ex.what() + ".");
            }
            catch (...)
            {
                throw std::runtime_error(
                    std::string("Failed to render the frame pass: Unexpected error."));
            }
        }

        glfwPollEvents();
        _glWindow.setWindowShouldClose();
    }
}

bool OpenGLRendererContext::saveImage(const std::string& fileName)
{
    static const std::filesystem::path filePath = TestHelpers::getOutputDataFolder();

    const std::filesystem::path screenShotPath = getFilename(filePath, fileName + "_computed");

    // Make sure the parent directory exists.

    createDirectoryTree(screenShotPath.parent_path());

    // Grab buffer.

    std::vector<unsigned char> image(width() * height() * 4, 0);
    glReadPixels(0, 0, width(), height(), GL_RGBA, GL_UNSIGNED_BYTE, &image[0]);

    // Write image.

    std::filesystem::remove(screenShotPath);

    struct Guard
    {
        Guard() { stbi_flip_vertically_on_write(1); }
        ~Guard() { stbi_flip_vertically_on_write(0); }
    } guard;

    return stbi_write_png(screenShotPath.string().c_str(), width(), height(), 4, image.data(), 0);
}

OpenGLTestContext::OpenGLTestContext()
{
    init();
}

OpenGLTestContext::OpenGLTestContext(int w, int h) : TestContext(w, h)
{
    init();
}

void OpenGLTestContext::init()
{
    namespace fs = std::filesystem;

    _sceneFilepath = TOSTRING(TEST_DATA_RESOURCE_PATH) + "/data/usd/test_fixed.usda";

    // Create the renderer context required for Hydra.
    _backend = std::make_shared<TestHelpers::OpenGLRendererContext>(_width, _height);
    if (!_backend)
    {
        throw std::runtime_error("Failed to initialize the unit test backend!");
    }

    fs::path dataPath =
        std::filesystem::path(TOSTRING(TEST_DATA_RESOURCE_PATH), fs::path::native_format);
    dataPath.append("Data");
    _backend->setDataPath(dataPath);
}

} // namespace TestHelpers
