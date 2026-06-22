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
#pragma once

#include <hvt/api.h>

#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hdSt/renderBuffer.h>

namespace HVT_NS::Outline
{

/// OutlinePrimIdsTask parameters.
struct HVT_API OutlinePrimIdsTaskParams
{
    /// Constructor.
    OutlinePrimIdsTaskParams()
        : enabled(false)
        , bufferPrefix("Base")
        , size{ 0, 0 }
        , cullStyle(PXR_NS::HdCullStyleNothing)
        , overrideWindowPolicy(PXR_NS::CameraUtilDontConform)
    {
    }

    /// Returns true when both parameter sets describe the same primId render pass.
    bool operator==(OutlinePrimIdsTaskParams const& other) const
    {
        if (enabled != other.enabled ||
            bufferPrefix != other.bufferPrefix ||
            size != other.size ||
            collection != other.collection ||
            camera != other.camera ||
            cullStyle != other.cullStyle ||
            framing != other.framing ||
            overrideWindowPolicy != other.overrideWindowPolicy ||
            aovBindings.size() != other.aovBindings.size()) {
            return false;
        }

        for (size_t i = 0; i < aovBindings.size(); ++i) {
            if (aovBindings[i].renderBufferId != other.aovBindings[i].renderBufferId) {
                return false;
            }
        }

        return true;
    }

    /// Returns true when the parameter sets differ.
    bool operator!=(OutlinePrimIdsTaskParams const& other) const
    {
        return !(*this == other);
    }

    /// Writes out the task property values to the stream.
    friend std::ostream& operator<<(std::ostream& out, OutlinePrimIdsTaskParams const& params)
    {
        std::string collectionPaths;
        for (PXR_NS::SdfPath const& path : params.collection.GetRootPaths()) {
            collectionPaths += path.GetString() + " \n";
        }

        out << "OutlinePrimIdsTask Params: enabled=" << (params.enabled ? "YES" : "NO")
            << ", bufferPrefix=" << params.bufferPrefix
            << ", size=" << params.size[0] << "x" << params.size[1]
            << ", collection=" << collectionPaths
            << ", camera=" << params.camera.GetString()
            << ", cullStyle=" << params.cullStyle
            << ", framing=" << (params.framing.IsValid() ? "Valid" : "Invalid")
            << ", overrideWindowPolicy="
            << (params.overrideWindowPolicy.has_value()
                    ? TfStringify(params.overrideWindowPolicy.value())
                    : "None")
            << ", aovBindings=[";
        for (const auto& binding : params.aovBindings) {
            out << "{name=" << binding.aovName
                << ", renderBufferId=" << binding.renderBufferId.GetString() << "}, ";
        }
        out << "]";

        return out;
    }

    /// Enables rendering of the primId and depth outline buffers.
    bool enabled;

    /// Prefix used when publishing primId and depth textures into the task context.
    std::string bufferPrefix;

    /// Size, in pixels, of the generated primId and depth AOV buffers.
    PXR_NS::GfVec2i size;

    /// Rprim collection to render into the outline buffers.
    PXR_NS::HdRprimCollection collection;

    /// Camera sprim path used to render the collection.
    PXR_NS::SdfPath camera;

    /// Cull style applied by the offscreen render pass.
    PXR_NS::HdCullStyle cullStyle;

    /// Optional camera framing used instead of a raw viewport when valid.
    PXR_NS::CameraUtilFraming framing;

    /// Optional window policy override applied with the framing data.
    std::optional<PXR_NS::CameraUtilConformWindowPolicy> overrideWindowPolicy;

    /// Optional AOV bindings supplied by callers; render buffer ids participate in equality.
    PXR_NS::HdRenderPassAovBindingVector aovBindings;
};

/// A task to render outline primIds and depth buffers from an input collection.
class HVT_API OutlinePrimIdsTask : public PXR_NS::HdxTask
{
public:
    /// Constructor.
    /// \param delegate A task delegate to store input parameters in.
    /// \param id The unique identifier for this task.
    OutlinePrimIdsTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~OutlinePrimIdsTask() override;

    /// Prepare render pass state and AOV bindings before execution.
    /// \param ctx The task context holding the names and resources of the AOVs in use.
    /// \param renderIndex The render index holding scene and render resources.
    void Prepare(PXR_NS::HdTaskContext* ctx,
                 PXR_NS::HdRenderIndex* renderIndex) override;

    /// Render primId and depth buffers and publish their texture handles to the task context.
    /// \param ctx The task context receiving the generated outline textures.
    void Execute(PXR_NS::HdTaskContext* ctx) override;
    
    /// Returns the associated token for a named primId task instance.
    /// \param prefix Prefix included in the task token.
    static PXR_NS::TfToken const& GetToken(const std::string& prefix);

protected:
    /// Synchronize render pass state from task parameters.
    /// \param delegate The scene delegate that stores task parameters.
    /// \param ctx The task context for the current task graph execution.
    /// \param dirtyBits Dirty state indicating whether parameters changed.
    void _Sync(PXR_NS::HdSceneDelegate* delegate,
               PXR_NS::HdTaskContext* ctx,
               PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    OutlinePrimIdsTask() = delete;
    OutlinePrimIdsTask(OutlinePrimIdsTask const&) = delete;
    OutlinePrimIdsTask& operator=(OutlinePrimIdsTask const&) = delete;

    /// Initialize AOV resources, render pass, and render pass state when needed.
    /// \return True if initialization succeeded, otherwise false.
    bool _InitIfNeeded();
    /// Allocate the primId and depth render buffers and build their AOV bindings.
    void _CreateAovBindings();
    /// Finalize and release render buffers and AOV bindings.
    void _CleanupAovBindings();
    /// Returns true when the current render delegate supports this task.
    bool _Enabled() const;

    /// Read back and validate a primId texture for debug builds.
    /// \param binding The primId AOV binding to validate.
    /// \param resource Texture resource stored in the render buffer.
    void _ValidatePrimIdBuffer(PXR_NS::HdRenderPassAovBinding binding, PXR_NS::VtValue resource);
    /// Return the texture handle associated with an AOV binding index.
    /// \param bindingIndex Index in the internal AOV binding array.
    PXR_NS::HgiTextureHandle _GetTextureHandleForBinding(size_t bindingIndex) const;

    /// Returns the path to the picking shader used for primId rendering.
    PXR_NS::TfToken _GetShaderFilePath();

    PXR_NS::HdRenderIndex* _renderIndex;

    // Render pass to render primId and depth buffers for the outline collection
    PXR_NS::HdRenderPassSharedPtr _renderPass;
    PXR_NS::HdRenderPassStateSharedPtr _renderPassState;

    std::vector<std::unique_ptr<PXR_NS::HdStRenderBuffer>> _aovBuffers;

    PXR_NS::HdRenderPassAovBinding _primIdBinding;
    PXR_NS::HdRenderPassAovBindingVector _aovBindings;

    size_t _primIdBindingIndex{0};
    size_t _depthBindingIndex{1};

    OutlinePrimIdsTaskParams _params;

    bool _isStormRenderer{false};
    bool _vpChanged;
};

} // namespace HVT_NS::Outline
