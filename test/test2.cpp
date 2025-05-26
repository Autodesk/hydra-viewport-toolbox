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

// glew.h has to be included first
#include <GL/glew.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <pxr/imaging/glf/glContext.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#include <gtest/gtest.h>

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

// TODO: This is temporary testing code, for validating the test framework. It will be removed
// later.
TEST(test3, BasicAssertions)
{
    static constexpr unsigned int glMajor = getGLMajorVersion();
    static constexpr unsigned int glMinor = getGLMinorVersion();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, glMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, glMinor);

    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    int width, height;
    glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), nullptr, nullptr, &width, &height);

    auto window = glfwCreateWindow(width, height, "Window Example", NULL, NULL);
    ASSERT_TRUE(window);

    glfwMakeContextCurrent(window);
    ASSERT_EQ(window, glfwGetCurrentContext());

    ASSERT_TRUE(initGlew());

#if defined(ADSK_OPENUSD_PENDING)
    auto hgi = pxr::Hgi::CreatePlatformDefaultHgi();
#elif defined(_WIN32)
    auto hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->OpenGL);
#elif defined(__APPLE__)
    auto hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->Metal);
#else
    #error "The platform is not supported"
#endif

    ASSERT_TRUE(hgi);
    ASSERT_TRUE(hgi->IsBackendSupported());

    glfwTerminate();
}
