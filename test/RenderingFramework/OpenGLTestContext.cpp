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

#include <RenderingFramework/OpenGLTestContext.h>

#include <hvt/engine/hgiInstance.h>

#include <pxr/imaging/glf/glContext.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <hvt/engine/framePass.h>

#include <filesystem>
#include <mutex>

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

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

#ifdef GLFW_SCALE_FRAMEBUFFER
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
#else
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    if (isCoreProfile())
    {
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

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
    _imageCapture.flipVertically(true);
    init();
    createHGI();
}

OpenGLRendererContext::~OpenGLRendererContext()
{
    destroyHGI();
    shutdown();
}

void OpenGLRendererContext::waitForGPUIdle()
{
    HD_TRACE_FUNCTION();

    // Wait for all GPU commands to complete.
    glFinish();
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
    HD_TRACE_FUNCTION();

    _glWindow.makeContextCurrent();

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
    HD_TRACE_FUNCTION();

    if (isCoreProfile())
    {
        glBindVertexArray(0);
    }

    _glWindow.swapBuffers();

    glFinish();
}

void OpenGLRendererContext::run(
    std::function<bool()> render, hvt::FramePass* framePass)
{
    HD_TRACE_FUNCTION();

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

    captureColorTexture(framePass);
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
    _sceneFilepath = (TestHelpers::getAssetsDataFolder() / "usd/test_fixed.usda").string();

    // Create the renderer context required for Hydra.
    _backend = std::make_shared<TestHelpers::OpenGLRendererContext>(_width, _height);
    if (!_backend)
    {
        throw std::runtime_error("Failed to initialize the unit test backend!");
    }
}

} // namespace TestHelpers
