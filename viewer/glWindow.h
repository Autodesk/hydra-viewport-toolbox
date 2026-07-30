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

#pragma once

// glew.h has to be included before any other OpenGL header.
#include <GL/glew.h>

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4003)
    #pragma warning(disable : 4244)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#include <pxr/base/gf/vec2i.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <string>

struct SDL_Window;
using SDL_GLContext = void*;

namespace HvtViewer
{

/// A visible OpenGL window backed by SDL2.
///
/// \note This mirrors TestHelpers::OpenGLWindow and the context part of
/// TestHelpers::OpenGLRendererContext from the unit test framework. The differences are that this
/// window is visible and resizable, that vertical synchronization paces the frames, and that the
/// render loop is owned by the caller rather than by this class.
class GlWindow
{
public:
    /// Creates the window and its OpenGL context, and makes the context current.
    /// \param title The window title.
    /// \param width The initial window width, in pixels.
    /// \param height The initial window height, in pixels.
    GlWindow(std::string const& title, int width, int height);
    ~GlWindow();

    GlWindow(GlWindow const&)            = delete;
    GlWindow& operator=(GlWindow const&) = delete;

    /// Gets the SDL window, e.g., to identify the window an event belongs to.
    SDL_Window* handle() const { return _window; }

    /// Gets the drawable dimensions, which change when the user resizes the window.
    pxr::GfVec2i size() const;

    /// Makes the OpenGL context current on the calling thread.
    void makeContextCurrent() const;

    /// Prepares the default framebuffer for the next frame.
    void beginFrame();

    /// Restores the state changed by beginFrame().
    void endFrame();

    /// Displays the content of the default framebuffer.
    void swapBuffers() const;

private:
    SDL_Window* _window { nullptr };
    SDL_GLContext _glContext { nullptr };

    /// Only used in core profile.
    GLuint _vao { 0 };
};

} // namespace HvtViewer
