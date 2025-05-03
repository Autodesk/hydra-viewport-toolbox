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

#include <gtest/gtest.h>

#include <RenderingFramework/TestHelpers.h>
#include <RenderingFramework/UsdHelpers.h>
#include <RenderingFramework/TestFlags.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // Captures the glfw errors.

    auto glfw_error_callback = [](int error, const char* description)
    {
        throw std::runtime_error(std::string("GLFW error: code ") + std::to_string(error) + ": " +
            (description ? description : ""));
    };
    glfwSetErrorCallback(glfw_error_callback);

    // Captures the OpenUSD errors to only keep pertinent ones.

    pxr::TfDiagnosticMgr::GetInstance().AddDelegate(new DiagnosticDelegate(""));

    // Initializes the glfw library.

    if (glfwInit() == GLFW_FALSE)
    {
        const char* description;
        glfwGetError(&description);
        std::cerr << "GLFW initialization failed:" << description << std::endl;
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try
    {
        ret = RUN_ALL_TESTS();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unexpected failure: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unexpected failure" << std::endl;
    }

    glfwTerminate();

    return ret;
}
