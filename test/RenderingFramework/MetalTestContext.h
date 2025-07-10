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

#include <hvt/testFramework/testFramework.h>

#include <SDL.h>

#include <filesystem>

struct SDL_Window;

/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

/// Defines an Metal context to execute the unit tests.
class MetalRendererContext : public hvt::TestFramework::HydraRendererContext
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
class MetalTestContext : public hvt::TestFramework::TestContext
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
