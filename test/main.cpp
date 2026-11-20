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

#include <iostream>

#include <RenderingFramework/OpenGLTestContext.h>
#include <RenderingFramework/TestHelpers.h>
#include <RenderingFramework/UsdHelpers.h>
#include <RenderingFramework/TestFlags.h>

#include <SDL2/SDL.h>

#include <pxr/base/arch/env.h>
#include <pxr/imaging/hgi/tokens.h>

#include <stdexcept>
#include <vector>

#if defined(_WIN32)
    #include <process.h>
#else
    #include <unistd.h>
#endif

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

/// Runs this executable again with \p args (null terminated) and the current environment.
/// Returns the exit code of the new process, if it does not replace this one.
int restartProcess(std::vector<char*> const& args)
{
#if defined(_WIN32)
    // _execv() would detach from the caller (e.g., ctest), so wait for the child instead.
    const intptr_t exitCode = _spawnv(_P_WAIT, args[0], args.data());
#else
    execvp(args[0], args.data());
    const int exitCode = -1; // execvp() only returns on failure.
#endif
    if (exitCode == -1)
    {
        std::cerr << "Failed to restart " << args[0] << " with HGI_ENABLE_VULKAN=1; set it before "
                  << "running the tests." << std::endl;
        return EXIT_FAILURE;
    }
    return static_cast<int>(exitCode);
}

} // anonymous namespace

int main(int argc, char** argv)
{
    // Select the backend first: --hgi=<name> is not a Google Test flag. Keep the original arguments
    // in case the process must be restarted (see below).
    const std::vector<char*> originalArgs(argv, argv + argc + 1);
    try
    {
        TestHelpers::InitRenderingBackend(&argc, argv);
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    // HdRendererPlugin::CreateDelegate() probes the platform default Hgi, which is only HgiVulkan
    // when HGI_ENABLE_VULKAN is set (see RenderIndexProxy). OpenUSD reads its environment settings
    // during static initialization, i.e., before main(), so setting it here is too late: restart
    // the process with it instead.
    if (TestHelpers::GetRenderingBackend() == pxr::HgiTokens->Vulkan &&
        pxr::ArchGetEnv("HGI_ENABLE_VULKAN").empty())
    {
        pxr::ArchSetEnv("HGI_ENABLE_VULKAN", "1", /*overwrite*/ true);
        pxr::ArchSetEnv("HGIVULKAN_DEBUG", "1", /*overwrite*/ true);
        return restartProcess(originalArgs);
    }

    ::testing::InitGoogleTest(&argc, argv);

    // On stderr: test discovery (gtest_discover_tests) parses the stdout of --gtest_list_tests.
    std::cerr << "Rendering backend: " << TestHelpers::GetRenderingBackend().GetString()
              << std::endl;

    pxr::TfDiagnosticMgr::GetInstance().AddDelegate(new DiagnosticDelegate(""));

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return EXIT_FAILURE;
    }

    // Keep a shared OpenGL context current for the whole process so that OpenUSD's Storm
    // support probe (which reads HgiGL capabilities) succeeds even during Vulkan-only tests.
    // OpenGLWindow also borrows this window and context for every test context, which avoids
    // an SDL_CreateWindow + SDL_GL_CreateContext pair per test.
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
