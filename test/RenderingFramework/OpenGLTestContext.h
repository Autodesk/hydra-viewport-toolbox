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

#include <RenderingFramework/TestHelpers.h>

#include <filesystem>

struct SDL_Window;
using SDL_GLContext = void*;

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

/// Creates an OpenGL window backed by SDL2, which can be made as the current OpenGL context
class OpenGLWindow
{
public:
    OpenGLWindow(int w, int h);
    OpenGLWindow(const OpenGLWindow&)            = delete;
    OpenGLWindow& operator=(const OpenGLWindow&) = delete;
    ~OpenGLWindow();

    void destroy();
    void swapBuffers() const;
    void makeContextCurrent() const;
    bool windowShouldClose() const;
    void setWindowShouldClose();

private:
    SDL_Window* _window       = nullptr;
    SDL_GLContext _glContext   = nullptr;
    bool _shouldClose         = false;
};

/// Defines an OpenGL context to execute the unit tests.
class OpenGLRendererContext : public HydraRendererContext
{
public:
    OpenGLRendererContext(int w, int h);
    OpenGLRendererContext(const OpenGLRendererContext&)        = delete;
    OpenGLRendererContext& operator=(const OpenGLRendererContext&) = delete;
    ~OpenGLRendererContext();

    void init();
    void shutdown() override;

    /// Render the frame pass.
    void run(std::function<bool()> render, hvt::FramePass* framePass) override;
    void waitForGPUIdle() override;

protected:
    void beginGL();
    void endGL();

private:
    OpenGLWindow _glWindow;
    GLuint _vao; // Only used in Core Profile.
};

/// \brief Helper to build a unit test.
/// \note Some unit tests from this unit test suite needs a fixture but others do not. So, a
/// google test fixture cannot be used. The following class is then used in place of the fixture
/// only when a unit test needs it.
class OpenGLTestContext : public TestContext
{
public:
    OpenGLTestContext();
    OpenGLTestContext(int w, int h);
    ~OpenGLTestContext() {};

private:
    /// Initialize the backend.
    void init() override;
};

} // namespace TestHelpers
