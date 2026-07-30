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

//
// A minimal interactive viewer: it displays a USD model using a single frame pass, and lets the
// user navigate around it with the mouse. It is the interactive counterpart of the
// TestFramePass.framepass_mainOnly unit test.
//

// glWindow.h includes glew.h, which has to be included before any other OpenGL header.
#include "glWindow.h"

#include "orbitCamera.h"

#include <hvt/engine/framePass.h>
#include <hvt/engine/viewportEngine.h>
#include <hvt/tasks/resources.h>

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4003)
    #pragma warning(disable : 4244)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hdx/colorCorrectionTask.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/tokens.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <SDL2/SDL.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#if !defined(HVT_RESOURCE_PATH) || !defined(HVT_VIEWER_DEFAULT_SCENE)
    #error "HVT_RESOURCE_PATH and HVT_VIEWER_DEFAULT_SCENE must be defined by the build system."
#endif

#define HVT_VIEWER_STRINGIFY(x) #x
#define HVT_VIEWER_TOSTRING(x) HVT_VIEWER_STRINGIFY(x)

namespace
{

/// The window dimensions at startup.
constexpr int kWindowWidth  = 1280;
constexpr int kWindowHeight = 720;

/// Gets the rendering backend to use, like the unit test framework does.
/// \note macOS is limited to OpenGL 4.1 where Storm needs 4.5, so Metal does the rendering there
/// and the present task then composites its result into the OpenGL context of the window.
pxr::TfToken GetRenderingBackend()
{
#if defined(__APPLE__)
    return pxr::HgiTokens->Metal;
#else
    return pxr::HgiTokens->OpenGL;
#endif
}

/// Computes the world extent of the model, i.e., what the camera has to frame.
pxr::GfRange3d ComputeStageBounds(pxr::UsdStageRefPtr const& stage)
{
    const pxr::TfTokenVector purposes { pxr::UsdGeomTokens->default_, pxr::UsdGeomTokens->proxy };

    pxr::UsdGeomBBoxCache bboxCache(pxr::UsdTimeCode::Default(), purposes, false);
    const pxr::GfBBox3d bbox = bboxCache.ComputeWorldBound(stage->GetPseudoRoot());

    return bbox.ComputeAlignedRange();
}

/// Consumes all the pending events, updating the camera along the way.
/// \param camera The camera to navigate.
/// \param bounds The world extent to frame when the user asks for it.
/// \param viewportSize The size of the last rendered frame, used to scale the mouse motions.
/// \return False when the user asks to quit.
bool ProcessEvents(
    HvtViewer::OrbitCamera& camera, pxr::GfRange3d const& bounds, pxr::GfVec2i const& viewportSize)
{
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0)
    {
        switch (event.type)
        {
        case SDL_QUIT:
            return false;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE)
            {
                return false;
            }
            break;

        case SDL_MOUSEMOTION:
        {
            const double dx = static_cast<double>(event.motion.xrel);
            const double dy = static_cast<double>(event.motion.yrel);

            const bool isShiftDown  = (SDL_GetModState() & KMOD_SHIFT) != 0;
            const bool isLeftDown   = (event.motion.state & SDL_BUTTON_LMASK) != 0;
            const bool isMiddleDown = (event.motion.state & SDL_BUTTON_MMASK) != 0;

            if (isMiddleDown || (isLeftDown && isShiftDown))
            {
                camera.pan(dx, dy, viewportSize[1]);
            }
            else if (isLeftDown)
            {
                camera.orbit(dx, dy);
            }
            break;
        }

        case SDL_MOUSEWHEEL:
            camera.dolly(static_cast<double>(event.wheel.y));
            break;

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                return false;
            case SDLK_f:
                camera.fit(bounds);
                break;
            default:
                break;
            }
            break;

        default:
            break;
        }
    }

    return true;
}

void PrintUsage(std::string const& sceneFilepath, pxr::TfToken const& backend)
{
    std::cout << "HVT viewer: " << sceneFilepath << " [" << backend << "]\n"
              << "  left drag                 orbit\n"
              << "  middle drag / shift+left  pan\n"
              << "  mouse wheel               zoom\n"
              << "  F                         frame the model\n"
              << "  Esc                       quit\n";
}

} // anonymous namespace

int main(int argc, char** argv)
{
    const std::string sceneFilepath =
        argc > 1 ? argv[1] : HVT_VIEWER_TOSTRING(HVT_VIEWER_DEFAULT_SCENE);

    // Required because the build defines SDL_MAIN_HANDLED, i.e., SDL does not own the entry point.
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;

    try
    {
        // Creates the window and its OpenGL context, i.e., what the frame is presented into.

        HvtViewer::GlWindow window("HVT Viewer", kWindowWidth, kWindowHeight);

        const pxr::TfToken backend = GetRenderingBackend();

        pxr::HgiUniquePtr hgi = pxr::Hgi::CreateNamedHgi(backend);
        if (!hgi || !hgi->IsBackendSupported())
        {
            throw std::runtime_error("The " + backend.GetString() + " backend is not supported.");
        }

        pxr::HdDriver hgiDriver;
        hgiDriver.name   = pxr::HgiTokens->renderDriver;
        hgiDriver.driver = pxr::VtValue(hgi.get());

        // Tells the Viewport Toolbox where to find its resources, e.g., its shaders.

        hvt::SetResourceDirectory(HVT_VIEWER_TOSTRING(HVT_RESOURCE_PATH));

        // Opens the model to display.

        pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(sceneFilepath);
        if (!stage)
        {
            throw std::runtime_error("Failed to open the USD stage: " + sceneFilepath);
        }

        // Creates the render index by providing the hgi driver and the requested renderer name.

        hvt::RenderIndexProxyPtr renderIndex;
        hvt::RendererDescriptor renderDesc;
        renderDesc.hgiDriver    = &hgiDriver;
        renderDesc.rendererName = "HdStormRendererPlugin";
        hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);

        // Creates the scene index containing the model.

        pxr::HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage);
        renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

        // Creates the frame pass instance, i.e., the list of tasks rendering the model.

        hvt::FramePassDescriptor passDesc;
        passDesc.renderIndex        = renderIndex->RenderIndex();
        passDesc.uid                = pxr::SdfPath("/SceneFramePass");
        hvt::FramePassPtr framePass = hvt::ViewportEngine::CreateFramePass(passDesc);

        // Frames the model.

        const pxr::GfRange3d bounds = ComputeStageBounds(stage);
        HvtViewer::OrbitCamera camera;
        camera.fit(bounds);

        // Defines a basic material, like the unit tests do.

        pxr::GlfSimpleMaterial material;
        material.SetAmbient(pxr::GfVec4f(0.2f, 0.2f, 0.2f, 1.0f));
        material.SetSpecular(pxr::GfVec4f(0.1f, 0.1f, 0.1f, 1.0f));
        material.SetShininess(32.0f);

        PrintUsage(sceneFilepath, backend);

        // Runs the render loop, paced by the vertical synchronization of the window.

        while (ProcessEvents(camera, bounds, window.size()))
        {
            window.beginFrame();

            // The render buffer follows the window, so that the presented image is never scaled.
            const pxr::GfVec2i windowSize = window.size();

            hvt::FramePassParams& params = framePass->params();

            params.renderBufferSize = windowSize;
            params.viewInfo.framing =
                hvt::ViewParams::GetDefaultFraming(windowSize[0], windowSize[1]);

            params.viewInfo.viewMatrix = camera.viewMatrix();
            params.viewInfo.projectionMatrix =
                camera.projectionMatrix(windowSize[0], windowSize[1]);
            params.viewInfo.lights   = camera.headlight();
            params.viewInfo.material = material;
            params.viewInfo.ambient  = pxr::GfVec4f(0.1f, 0.1f, 0.1f, 0.0f);

            params.colorspace      = pxr::HdxColorCorrectionTokens->sRGB;
            params.backgroundColor = pxr::GfVec4f(0.025f, 0.025f, 0.025f, 1.0f);

            // The present task blits the color AOV into the framebuffer of the window.
            params.enablePresentation = true;

            framePass->Render();

            window.endFrame();
            window.swapBuffers();
        }

        // Lets the in-flight GPU work complete before the frame pass and the Hgi instance,
        // i.e., the owners of the GPU resources, are destroyed.
        glFinish();

        result = EXIT_SUCCESS;
    }
    catch (std::exception const& ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unexpected error." << std::endl;
    }

    SDL_Quit();

    return result;
}
