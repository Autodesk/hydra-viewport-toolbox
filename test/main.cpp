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

#include <gtest/gtest.h>

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

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    auto glfw_error_callback = [](int error, const char* description)
    {
        throw std::runtime_error(std::string("GLFW error: code ") + std::to_string(error) + ": " +
            (description ? description : ""));
    };
    glfwSetErrorCallback(glfw_error_callback);

    if (glfwInit() == GLFW_FALSE)
    {
        const char* description;
        glfwGetError(&description);
        std::cerr << "GLFW initialization failed:" << description << std::endl;
        return 0;
    }

    int ret = 1;
    try
    {
        ret = RUN_ALL_TESTS();

        std::cout << "Done tests on ViewportToolbox" << std::endl;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Failure for tests on ViewportToolbox: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Failure for tests on ViewportToolbox" << std::endl;
    }

    glfwTerminate();

    return ret;
}