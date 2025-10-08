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

#include <RenderingFramework/TestHelpers.h>

#include <filesystem>

struct GLFWwindow;

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

/// Creates an OpenGL window, which can be made as the current OpenGL context
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
    GLFWwindow* _pGLFWWindow = nullptr;
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
    bool saveImage(const std::string& fileName) override;

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
