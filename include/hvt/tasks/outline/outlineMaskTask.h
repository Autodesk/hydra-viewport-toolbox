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

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hdSt/glslProgram.h>
#include <pxr/imaging/hdSt/renderBuffer.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hgi/texture.h>

#include <cstdint>

#include <string>
#include <vector>

namespace HVT_NS
{

enum class VisualizationMode
{
    VISUALIZE_MASK_3x3,
    VISUALIZE_MASK_5x5,
    VISUALIZE_PRIM_IDS,
    VISUALIZE_DEPTH
};

/// Shader constants controlling outline colors, selection state, and softness.
struct HVT_API OutlineMaskStyleParams
{
    /// Color for selected primitives.
    PXR_NS::GfVec4f selectedColor;
    /// Color for selected primitives that are also hovered.
    PXR_NS::GfVec4f selectedHoverColor;
    /// Color for the active, or lead, selected primitive.
    PXR_NS::GfVec4f selectionLeadColor;
    /// Color for the active selected primitive when it is also hovered.
    PXR_NS::GfVec4f selectionLeadHoverColor;
    /// Color for overlay primitives.
    PXR_NS::GfVec4f overlayColor;
    /// Color for overlay primitives that are also hovered.
    PXR_NS::GfVec4f overlayHoverColor;
    /// Color for hovered primitives that are not selected.
    PXR_NS::GfVec4f unselectedHoverColor;
    /// Fallback color used by the mask shader when no outline category matches.
    PXR_NS::GfVec4f defaultColor;

    /// Number of valid active primitive IDs in the active ID buffer.
    int activeIdsCount;
    /// Non-zero when the hover target is also selected.
    int isHoverSelected;

    /// Number of valid overlay primitive IDs in the overlay ID buffer.
    int overlayIdsCount;
    /// Number of valid hover primitive IDs in the hover ID buffer.
    int hoverIdsCount;
    /// 1 if overlay textures differ from base, 0 to skip overlay lookups
    int hasDistinctOverlay;
    /// 1 if base textures differ from default, 0 to skip default lookups
    int hasDistinctDefault;

    /// Edge softness amount; 0.0 gives hard edges and 1.0 gives full coverage-based softness.
    float softnessStrength;
    /// Exponent for the softness falloff curve; lower values produce thicker lines.
    float softnessFalloff;

    /// Constructor.
    OutlineMaskStyleParams()
        : selectedColor(0.0f)
        , selectedHoverColor(0.0f)
        , selectionLeadColor(0.0f)
        , selectionLeadHoverColor(0.0f)
        , overlayColor(0.0f)
        , overlayHoverColor(0.0f)
        , unselectedHoverColor(0.0f)
        , defaultColor(0.0f)
        , activeIdsCount(0)
        , isHoverSelected(false)
        , overlayIdsCount(0)
        , hoverIdsCount(0)
        , hasDistinctOverlay(0)
        , hasDistinctDefault(0)
        , softnessStrength(1.0f)
        , softnessFalloff(0.4f)
    {
    }

    /// Operator overloads.
    /// @{

    /// Compares the task property values.
    bool operator==(OutlineMaskStyleParams const& other) const
    {
        if (selectedColor != other.selectedColor ||
            selectedHoverColor != other.selectedHoverColor ||
            selectionLeadColor != other.selectionLeadColor ||
            selectionLeadHoverColor != other.selectionLeadHoverColor ||
            overlayColor != other.overlayColor ||
            overlayHoverColor != other.overlayHoverColor ||
            unselectedHoverColor != other.unselectedHoverColor ||
            defaultColor != other.defaultColor ||
            activeIdsCount != other.activeIdsCount ||
            isHoverSelected != other.isHoverSelected ||
            overlayIdsCount != other.overlayIdsCount ||
            hoverIdsCount != other.hoverIdsCount ||
            hasDistinctOverlay != other.hasDistinctOverlay ||
            hasDistinctDefault != other.hasDistinctDefault ||
            softnessStrength != other.softnessStrength ||
            softnessFalloff != other.softnessFalloff) {
            return false;
        }

        return true;
    }

    /// Compares the task property values.
    bool operator!=(OutlineMaskStyleParams const& other) const
    {
        return !(*this == other);
    }

    /// Writes the task style values to the stream.
    friend std::ostream& operator<<(std::ostream& out, OutlineMaskStyleParams const& params)
    {
        out << "OutlineMaskStyleParams: "
            << "\n selectedColor=" << params.selectedColor
            << "\n selectedHoverColor=" << params.selectedHoverColor
            << "\n selectionLeadColor=" << params.selectionLeadColor
            << "\n selectionLeadHoverColor=" << params.selectionLeadHoverColor
            << "\n overlayColor=" << params.overlayColor
            << "\n overlayHoverColor=" << params.overlayHoverColor
            << "\n unselectedHoverColor=" << params.unselectedHoverColor
            << "\n defaultColor=" << params.defaultColor
            << "\n activeIdsCount=" << params.activeIdsCount
            << "\n isHoverSelected=" << params.isHoverSelected
            << "\n overlayIdsCount=" << params.overlayIdsCount
            << "\n hoverIdsCount=" << params.hoverIdsCount
            << "\n hasDistinctOverlay=" << params.hasDistinctOverlay
            << "\n hasDistinctDefault=" << params.hasDistinctDefault
            << "\n softnessStrength=" << params.softnessStrength
            << "\n softnessFalloff=" << params.softnessFalloff;

        return out;
    }

    /// @}
};

/// Parameters used by OutlineMaskTask.
struct HVT_API OutlineMaskTaskParams
{
    /// Constructor.
    OutlineMaskTaskParams()
        : enabled(false)
        , size{ 0, 0 }
        , multisampling(false)
        , maskVisualizationMode(VisualizationMode::VISUALIZE_MASK_3x3)
        , style{}
    {
    }

    /// Returns true when both parameter sets describe the same mask generation pass.
    bool operator==(OutlineMaskTaskParams const& other) const
    {
        if (enabled != other.enabled ||
            size != other.size ||
            multisampling != other.multisampling ||
            defaultPrimIdsTexture != other.defaultPrimIdsTexture ||
            defaultDepthTexture != other.defaultDepthTexture ||
            basePrimIdsTexture != other.basePrimIdsTexture ||
            baseDepthTexture != other.baseDepthTexture ||
            overlayPrimIdsTexture != other.overlayPrimIdsTexture ||
            overlayDepthTexture != other.overlayDepthTexture ||
            maskVisualizationMode != other.maskVisualizationMode ||
            hoverPaths != other.hoverPaths ||
            activePath != other.activePath ||
            overlayPaths != other.overlayPaths ||
            style != other.style ||
            overlayIdValues != other.overlayIdValues ||
            hoverIdValues != other.hoverIdValues ||
            activeIdValues != other.activeIdValues) {
            return false;
        }

        return true;
    }

    /// Returns true when the parameter sets differ.
    bool operator!=(OutlineMaskTaskParams const& other) const
    {
        return !(*this == other);
    }

    /// Writes out the task property values to the stream.
    HVT_API friend std::ostream& operator<<(std::ostream& out, OutlineMaskTaskParams const& params)
    {
        std::string hoverPaths;
        for (PXR_NS::SdfPath const& path : params.hoverPaths)
        {
            hoverPaths += path.GetString() + ", ";
        }
        std::string overlayPaths;
        for (PXR_NS::SdfPath const& path : params.overlayPaths)
        {
            overlayPaths += path.GetString() + ", ";
        }
        std::string overlayIdValues;
        for (int id : params.overlayIdValues)
        {
            overlayIdValues += std::to_string(id) + ", ";
        }
        std::string hoverIdValues;
        for (int id : params.hoverIdValues)
        {
            hoverIdValues += std::to_string(id) + ", ";
        }
        std::string activeIdValues;
        for (int id : params.activeIdValues)
        {
            activeIdValues += std::to_string(id) + ", ";
        }

        out << "OutlineMaskTaskParams: "
            << "\n enabled=" << params.enabled
            << "\n size=" << params.size[0] << "\n x" << params.size[1]
            << "\n multisampling=" << (params.multisampling ? "YES" : "NO")
            << "\n maskVisualizationMode=" << static_cast<int>(params.maskVisualizationMode)
            << "\n defaultPrimIdsTexture=" << params.defaultPrimIdsTexture
            << "\n defaultDepthTexture=" << params.defaultDepthTexture
            << "\n basePrimIdsTexture=" << params.basePrimIdsTexture
            << "\n baseDepthTexture=" << params.baseDepthTexture
            << "\n overlayPrimIdsTexture=" << params.overlayPrimIdsTexture
            << "\n overlayDepthTexture=" << params.overlayDepthTexture
            << "\n hoverPaths=" << hoverPaths
            << "\n activePath=" << params.activePath.GetString()
            << "\n overlayPaths=" << overlayPaths
            << "\n style=" << params.style
            << "\n overlayIdValues=" << overlayIdValues
            << "\n hoverIdValues=" << hoverIdValues
            << "\n activeIdValues=" << activeIdValues;

        return out;
    }


    /// Enables computation of the outline mask texture.
    bool enabled;
    /// Size, in pixels, of the output mask and input textures.
    PXR_NS::GfVec2i size;
    /// Whether the associated render setup uses multisampling.
    bool multisampling;
    /// Selects normal mask output or a debug visualization shader path.
    VisualizationMode maskVisualizationMode;

    /// Task context texture name containing default-pass primIds.
    std::string defaultPrimIdsTexture;
    /// Task context texture name containing default-pass depth.
    std::string defaultDepthTexture;
    /// Task context texture name containing base-pass primIds.
    std::string basePrimIdsTexture;
    /// Task context texture name containing base-pass depth.
    std::string baseDepthTexture;
    /// Task context texture name containing overlay-pass primIds.
    std::string overlayPrimIdsTexture;
    /// Task context texture name containing overlay-pass depth.
    std::string overlayDepthTexture;

    /// Scene paths whose primIds should be treated as hovered.
    PXR_NS::SdfPathVector hoverPaths;
    /// Scene path whose primIds should be treated as active, or lead selected.
    PXR_NS::SdfPath activePath;
    /// Scene paths whose primIds should be treated as overlay primitives.
    PXR_NS::SdfPathVector overlayPaths;

    /// Style and shader constants used when generating the mask.
    OutlineMaskStyleParams style;

    /// Resolved overlay primitive IDs uploaded to the compute shader.
    std::vector<int> overlayIdValues;
    /// Resolved hover primitive IDs uploaded to the compute shader.
    std::vector<int> hoverIdValues;
    /// Resolved active primitive IDs uploaded to the compute shader.
    std::vector<int> activeIdValues;
};

/// A task to convert outline primId and depth buffers to a color 
/// mask for overlay display.
class HVT_API OutlineMaskTask : public PXR_NS::HdxTask
{
public:
    /// Constructor.
    /// \param delegate The scene delegate to use for this task.
    /// \param id The unique identifier for this task.
    OutlineMaskTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);
    ~OutlineMaskTask() override;

    /// Prepare task resources before execution.
    /// \param ctx The task context holding the names and resources of the AOVs in use.
    /// \param renderIndex The render index holding scene and render resources.
    void Prepare(PXR_NS::HdTaskContext* ctx,
                 PXR_NS::HdRenderIndex* renderIndex) override;
    
    /// Generate the outline mask texture and publish it to the task context.
    /// \param ctx The task context containing input primId/depth textures and receiving the mask.
    void Execute(PXR_NS::HdTaskContext* ctx) override;

    /// Sets the visualization mode for the outline mask.
    /// \param mode The visualization shader path to use on the next sync/execute.
    void SetVisualizationMode(VisualizationMode mode);

    /// Returns the associated token.
    static PXR_NS::TfToken const& GetToken();

protected:
    /// Synchronize task parameters and resolve scene paths to primIds.
    /// \param delegate The scene delegate that stores task parameters.
    /// \param ctx The task context for the current task graph execution.
    /// \param dirtyBits Dirty state indicating whether parameters changed.
    void _Sync(PXR_NS::HdSceneDelegate* delegate,
               PXR_NS::HdTaskContext* ctx,
               PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    OutlineMaskTask() = delete;
    OutlineMaskTask(OutlineMaskTask const&) = delete;
    OutlineMaskTask& operator=(OutlineMaskTask const&) = delete;

    /// Initialize output texture resources when the viewport changes or resources are missing.
    void _InitIfNeeded();
    /// Allocate the output mask texture.
    void _CreateAovBindings();
    /// Release output texture and dynamic ID buffers.
    void _CleanupAovBindings();
    /// Returns true when the current render delegate supports this task.
    bool _Enabled() const;

    /// Fetch a texture handle from the task context by token.
    /// \param ctx The task context to query.
    /// \param textureToken Token naming the texture resource.
    PXR_NS::HgiTextureHandle _GetInputTexture(
        PXR_NS::HdTaskContext* ctx, PXR_NS::TfToken const& textureToken);

    /// Compile or return the cached compute shader program for the current visualization mode.
    PXR_NS::HdStGLSLProgramSharedPtr _GetComputeProgram();

    /// Create or resize GPU buffers containing overlay, hover, and active primId values.
    /// \param hgi The Hgi device used to allocate buffers.
    /// \return True if buffer resources are available, otherwise false.
    bool _CreateBufferResources(PXR_NS::Hgi* hgi);

    /// Create Hgi resource bindings for all input textures, output texture, and ID buffers.
    /// \return Shared resource bindings handle, or null if creation failed.
    PXR_NS::HgiResourceBindingsSharedPtr _CreateResourceBindings(
        PXR_NS::Hgi* hgi,
        PXR_NS::HgiTextureHandle const& defaultPrimIdTexture,
        PXR_NS::HgiTextureHandle const& defaultDepthTexture,
        PXR_NS::HgiTextureHandle const& basePrimIdTexture,
        PXR_NS::HgiTextureHandle const& baseDepthTexture,
        PXR_NS::HgiTextureHandle const& overlayPrimIdTexture,
        PXR_NS::HgiTextureHandle const& overlayDepthTexture,
        PXR_NS::HgiTextureHandle const& outputTexture);

    /// Create the compute pipeline for the current shader program and constant layout.
    /// \param hgi The Hgi device used to allocate the pipeline.
    /// \param constantValuesSize Size of the shader constant block in bytes.
    /// \param program Shader program used by the compute pipeline.
    PXR_NS::HgiComputePipelineSharedPtr _CreatePipeline(
        PXR_NS::Hgi* hgi,
        uint32_t constantValuesSize,
        PXR_NS::HgiShaderProgramHandle const& program);

    /// Returns the path to the outline mask shader source.
    PXR_NS::TfToken _GetShaderFilePath();

    static constexpr int LOCAL_SIZE = 8;

    PXR_NS::HdRenderIndex* _renderIndex;

    PXR_NS::HgiTextureHandle _outputTexture;

    PXR_NS::HgiBufferHandle _overlayIdValuesBuffer;
    PXR_NS::HgiBufferHandle _hoverIdValuesBuffer;
    PXR_NS::HgiBufferHandle _activeIdValuesBuffer;

    PXR_NS::HdStGLSLProgramSharedPtr _computeProgram;
    uint64_t _computeProgramHash;

    PXR_NS::HgiResourceBindingsSharedPtr _resourceBindings;
    uint64_t _resourceBindingsHash;

    PXR_NS::HgiComputePipelineSharedPtr _pipeline;
    uint64_t _pipelineHash;

    PXR_NS::HgiSamplerHandle _sampler;
    bool _samplerInitialized;

    OutlineMaskTaskParams _params;
    bool _isStormRenderer;
    bool _vpChanged;
    PXR_NS::GfVec3i _workGroupCount;
};

} // namespace HVT_NS
