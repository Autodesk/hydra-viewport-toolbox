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

#include <SDL.h>

#include <filesystem>

struct SDL_Window;

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

/// Defines an Metal context to execute the unit tests.
class MetalRendererContext : public HydraRendererContext
{
public:
    MetalRendererContext(int w, int h);
    MetalRendererContext(const MetalRendererContext&)        = delete;
    MetalRendererContext& operator=(const MetalRendererContext&) = delete;
    ~MetalRendererContext();

    void init();
    void shutdown() override;
    bool saveImage(const std::string& fileName) override;
    void run(
        std::function<bool()> render, hvt::FramePass* framePass) override;
    void waitForGPUIdle() override;

protected:
    void beginMetal();
    void endMetal();
    void displayFramePass(hvt::FramePass* framePass);

private:
    SDL_Window* _window     = nullptr;
    SDL_Renderer* _renderer = nullptr;
};

/// \brief Helper to build a unit test.
/// \note Some unit tests from this unit test suite needs a fixture but others do not. So, a
/// google test fixture cannot be used. The following class is then used in place of the fixture
/// only when a unit test needs it.
/// Metal Test Context
class MetalTestContext : public TestContext
{
public:
    MetalTestContext();
    MetalTestContext(int w, int h);
    ~MetalTestContext() {};

private:
    /// Initialize the backend.
    void init() override;
};

} // namespace TestHelpers
