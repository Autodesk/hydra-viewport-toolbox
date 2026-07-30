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

#include "glWindow.h"

#include <pxr/imaging/glf/glContext.h>

#include <SDL2/SDL.h>

#include <mutex>
#include <stdexcept>

namespace
{

bool initGlew()
{
    static bool result = false;
    static std::once_flag once;
    std::call_once(once,
        []()
        {
            pxr::GlfSharedGLContextScopeHolder sharedGLContext;
            glewExperimental = GL_TRUE;
            result           = glewInit() == GLEW_OK;
        });

    return result;
}

// Apple still supports OpenGL, but not the OpenGL core profile, so the version reverts to 2.1.
constexpr int getGLMajorVersion()
{
#if defined(__APPLE__)
    return 2;
#else
    return 4;
#endif
}

constexpr int getGLMinorVersion()
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

} // anonymous namespace

namespace HvtViewer
{

GlWindow::GlWindow(std::string const& title, int width, int height)
{
    static constexpr int glMajor = getGLMajorVersion();
    static constexpr int glMinor = getGLMinorVersion();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glMinor);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

#ifdef _DEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // Disable high-DPI scaling so the framebuffer matches the window size.
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");

    static constexpr Uint32 windowFlags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

    _window = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, windowFlags);
    if (!_window)
    {
        throw std::runtime_error("Creation of an OpenGL " + std::to_string(glMajor) + "." +
            std::to_string(glMinor) + " SDL window failed: " + SDL_GetError());
    }

    _glContext = SDL_GL_CreateContext(_window);
    if (!_glContext)
    {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(_window);
        _window = nullptr;
        throw std::runtime_error("Creation of an OpenGL " + std::to_string(glMajor) + "." +
            std::to_string(glMinor) + " context failed: " + error);
    }

    makeContextCurrent();

    // Let vertical synchronization pace the render loop. Not all drivers support it, in which case
    // the loop simply runs as fast as the GPU allows.
    SDL_GL_SetSwapInterval(1);

    if (!initGlew())
    {
        throw std::runtime_error("Initialization of GLEW failed.");
    }

    if (isCoreProfile())
    {
        glGenVertexArrays(1, &_vao);
    }
}

GlWindow::~GlWindow()
{
    if (_glContext)
    {
        makeContextCurrent();

        if (isCoreProfile() && _vao != 0)
        {
            glDeleteVertexArrays(1, &_vao);
            _vao = 0;
        }

        SDL_GL_DeleteContext(_glContext);
        _glContext = nullptr;
    }

    if (_window)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }
}

pxr::GfVec2i GlWindow::size() const
{
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(_window, &width, &height);

    return pxr::GfVec2i(width, height);
}

void GlWindow::makeContextCurrent() const
{
    SDL_GL_MakeCurrent(_window, _glContext);
}

void GlWindow::beginFrame()
{
    makeContextCurrent();

    const pxr::GfVec2i dimensions = size();
    glViewport(0, 0, dimensions[0], dimensions[1]);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (isCoreProfile())
    {
        // We must bind a VAO (Vertex Array Object) because core profile contexts do not have a
        // default vertex array object. VAO objects are container objects which are not shared
        // between contexts, so we create and bind a VAO here so that core rendering code does not
        // have to explicitly manage per-GL context state.
        glBindVertexArray(_vao);
    }
}

void GlWindow::endFrame()
{
    if (isCoreProfile())
    {
        glBindVertexArray(0);
    }
}

void GlWindow::swapBuffers() const
{
    SDL_GL_SwapWindow(_window);
}

} // namespace HvtViewer
