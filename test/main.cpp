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

#include <gtest/gtest.h>

#include <RenderingFramework/OpenGLTestContext.h>
#include <RenderingFramework/TestHelpers.h>
#include <RenderingFramework/UsdHelpers.h>
#include <RenderingFramework/TestFlags.h>

#include <SDL2/SDL.h>

namespace
{

/// Restores the shared OpenGL context before each test, because the per-test contexts are
/// destroyed when a test ends.
class SharedGLContextListener : public ::testing::EmptyTestEventListener
{
public:
    void OnTestStart(const ::testing::TestInfo&) override
    {
        TestHelpers::OpenGLWindow::makeSharedCurrent();
    }
};

} // anonymous namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    pxr::TfDiagnosticMgr::GetInstance().AddDelegate(new DiagnosticDelegate(""));

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return EXIT_FAILURE;
    }

    // Keep a shared OpenGL context current for the whole process so that OpenUSD's Storm
    // support probe (which reads HgiGL capabilities) succeeds even during Vulkan-only tests.
    if (TestHelpers::OpenGLWindow::createShared())
    {
        ::testing::UnitTest::GetInstance()->listeners().Append(new SharedGLContextListener);
    }
    else
    {
        std::cerr << "No shared OpenGL context: the Vulkan tests are expected to fail."
                  << std::endl;
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

    // Must run before SDL_Quit(): the shared devices were created against SDL surfaces,
    // and static destructors would only run after main() returns.
    TestHelpers::releaseSharedHgi();

    TestHelpers::OpenGLWindow::destroyShared();
    SDL_Quit();

    return ret;
}
