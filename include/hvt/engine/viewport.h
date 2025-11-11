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

#include <hvt/api.h>

#include <hvt/engine/framePass.h>

namespace HVT_NS
{

/// This class represents a viewport in an application.
///
/// This class supports displaying in a portion of the application viewport. It means that the
/// screen size (i.e., where to display in the application viewport) could be different from the
/// render buffer size.
///
class HVT_API Viewport
{
public:
    /// Constructor.
    /// \param screenSize The location & size where to display in the application.
    /// \param renderBufferSize The render buffer dimensions.
    Viewport(PXR_NS::GfVec4i const& screenSize, PXR_NS::GfVec2i const& renderBufferSize);

    /// Destructor.
    virtual ~Viewport() = default;

    /// Resets the viewport.
    virtual void Reset() = 0;

    /// Is the viewport initialized ?
    virtual bool IsInitialized() const = 0;

    /// Updates the render pipeline instance.
    /// \param viewInfo The view information such as view and projection matrices.
    /// \param modelInfo The model information.
    /// \param enableFrameCancellation To enable the frame cancellation.
    /// \param usePresentationTask To enable the use of the PresentTask.
    virtual void Update(ViewParams const& viewInfo, ModelParams const& modelInfo,
        bool enableFrameCancellation, bool usePresentationTask) = 0;

    /// Render the contents of the viewport.
    virtual void Render() = 0;

    /// Creates the render pipeline (using a shared model render pass or not).
    /// \param renderIndex The render index to use where a null one means to create it.
    /// \param is3DCamera To define if the camera is in 3D mode.
    virtual void Create(RenderIndexProxyPtr& renderIndex, bool is3DCamera) = 0;

    /// Gets a specific frame pass by identifier.
    /// \param id The identifier of the frame pass to get.
    /// \return The frame pass instance or an empty one in case of failure.
    virtual FramePass* GetFramePass(std::string const& id) = 0;

    /// Gets the last frame pass.
    /// \return The frame pass instance or an empty one in case of failure.
    virtual FramePass* GetLastFramePass() = 0;

    /// Resizes the viewport.
    /// \param screenSize The new location & size of the viewport i.e., { originX, originY, width,
    /// height }.
    /// \param renderBufferSize The new render buffer size.
    /// \return True if the resize succeeded or did nothing; otherwise, false.
    bool Resize(PXR_NS::GfVec4i const& screenSize, PXR_NS::GfVec2i const& renderBufferSize);

    /// Returns the screen position and size.
    inline const PXR_NS::GfVec4i& GetScreenSize() const { return _screenSize; }

    /// Returns the render buffer size.
    inline const PXR_NS::GfVec2i& GetRenderBufferSize() const { return _renderBufferSize; }

private:
    /// Defines the viewport location and size.
    PXR_NS::GfVec4i _screenSize;

    /// Defines the render buffer size.
    PXR_NS::GfVec2i _renderBufferSize;
};

} // namespace HVT_NS