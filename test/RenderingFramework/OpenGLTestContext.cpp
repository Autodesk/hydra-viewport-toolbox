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

#include <SDL2/SDL.h>

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

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glMinor);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int contextFlags = 0;
    if (isCoreProfile())
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        contextFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    }

#ifdef _DEBUG
    contextFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
#endif
    if (contextFlags != 0)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags);
    }

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    // Disable high-DPI scaling so framebuffer matches window size.
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");

    _window = SDL_CreateWindow(
        "Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, windowFlags);
    if (!_window)
    {
        throw std::runtime_error("Creation of an OpenGL " + std::to_string(glMajor) + "." +
            std::to_string(glMinor) + " SDL window failed: " + SDL_GetError());
    }

    _glContext = SDL_GL_CreateContext(_window);
    if (!_glContext)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
        throw std::runtime_error("Creation of an OpenGL " + std::to_string(glMajor) + "." +
            std::to_string(glMinor) + " context failed: " + SDL_GetError());
    }
}

OpenGLWindow::~OpenGLWindow()
{
    destroy();
}

void OpenGLWindow::destroy()
{
    if (_glContext)
    {
        SDL_GL_DeleteContext(_glContext);
        _glContext = nullptr;
    }
    if (_window)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }
}

void OpenGLWindow::swapBuffers() const
{
    SDL_GL_SwapWindow(_window);
}

void OpenGLWindow::makeContextCurrent() const
{
    SDL_GL_MakeCurrent(_window, _glContext);
}

bool OpenGLWindow::windowShouldClose() const
{
    return _shouldClose;
}

void OpenGLWindow::setWindowShouldClose()
{
    _shouldClose = true;
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

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                break;
        }
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
