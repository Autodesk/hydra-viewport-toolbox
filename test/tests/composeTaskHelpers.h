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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#include <RenderingFramework/TestContextCreator.h>

#include <hvt/engine/renderBufferSettingsProvider.h>

namespace TestHelpers
{

/// Add the compose task to the second frame pass.
void AddComposeTask(
    TestHelpers::FramePassInstance const& framePass1, TestHelpers::FramePassInstance& framePass2);

/// Renders the first frame pass i.e., do not display it and let the next frame pass doing it.
void RenderFirstFramePass(TestHelpers::FramePassInstance& framePass, int width, int height,
    TestHelpers::TestStage const& stage);

/// Renders the second frame pass which also display the result.
void RenderSecondFramePass(TestHelpers::FramePassInstance& framePass, int width, int height,
    bool enablePresentTask, TestHelpers::TestStage const& stage,
    hvt::RenderBufferBindings const& inputAOVs, bool clearColorBackground,
    pxr::GfVec4f const& colorBackground, bool clearDepthBackground);

/// Options for RenderSecondFramePass to improve readability.
struct RenderOptions
{
    bool                     enablePresentation    = true;
    hvt::RenderBufferBindings inputAOVs            = {};
    bool                     clearColorBackground  = false;
    pxr::GfVec4f             backgroundColor       = TestHelpers::ColorDarkGrey;
    bool                     clearDepthBackground  = false;
};

/// Renders the second frame pass using RenderOptions for better readability.
void RenderSecondFramePass(TestHelpers::FramePassInstance& framePass, int width, int height,
    TestHelpers::TestStage const& stage, RenderOptions const& options = {});

} // namespace TestHelpers
