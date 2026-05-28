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

#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hdSt/renderBuffer.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hgi/texture.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEBUG_CODES(
    HVT_OUTLINE_MASK_TASK,
    HVT_OUTLINE_MASK_CACHE,
    HVT_OUTLINE_MASK_PARAMS,
    HVT_OUTLINE_MASK_RESOURCES,
    HVT_OUTLINE_MASK_SHADERCODE
);

PXR_NAMESPACE_CLOSE_SCOPE

namespace HVT_NS
{

PXR_NAMESPACE_USING_DIRECTIVE;

enum class HVT_API VisualizationMode
{
    VISUALIZE_MASK_3x3,
    VISUALIZE_MASK_5x5,
    VISUALIZE_PRIM_IDS,
    VISUALIZE_DEPTH
};

struct HVT_API OutlineMaskStyleParams
{
    PXR_NS::GfVec4f selectedColor;
    PXR_NS::GfVec4f selectedHoverColor;
    PXR_NS::GfVec4f selectionLeadColor;
    PXR_NS::GfVec4f selectionLeadHoverColor;
    PXR_NS::GfVec4f overlayColor;
    PXR_NS::GfVec4f overlayHoverColor;
    PXR_NS::GfVec4f unselectedHoverColor;
    PXR_NS::GfVec4f defaultColor;

    int activeIdsCount;
    int isHoverSelected;

    int overlayIdsCount;
    int hoverIdsCount;
    int hasDistinctOverlay; // 1 if overlay textures differ from base, 0 to skip overlay lookups
    int hasDistinctDefault; // 1 if base textures differ from default, 0 to skip default lookups

    float softnessStrength; // 0.0 = hard edges (no softness), 1.0 = full coverage-based soft edges
    float softnessFalloff;  // exponent for softness falloff curve (lower = thicker lines)

    OutlineMaskStyleParams()
        : activeIdsCount(0)
        , isHoverSelected(false)
        , overlayIdsCount(0)
        , hoverIdsCount(0)
        , hasDistinctOverlay(0)
        , hasDistinctDefault(0)
        , softnessStrength(1.0f)
        , softnessFalloff(0.4f)
    {
    }

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

    bool operator!=(OutlineMaskStyleParams const& other) const
    {
        return !(*this == other);
    }
};

struct HVT_API OutlineMaskTaskParams
{
    OutlineMaskTaskParams()
        : enabled(false)
        , size{ 0, 0 }
        , multisampling(false)
        , maskVisualizationMode(VisualizationMode::VISUALIZE_MASK_3x3)
        , style{}
    {
    }

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

    bool operator!=(OutlineMaskTaskParams const& other) const
    {
        return !(*this == other);
    }

    bool enabled;
    PXR_NS::GfVec2i size;
    bool multisampling;
    VisualizationMode maskVisualizationMode;

    std::string defaultPrimIdsTexture;
    std::string defaultDepthTexture;
    std::string basePrimIdsTexture;
    std::string baseDepthTexture;
    std::string overlayPrimIdsTexture;
    std::string overlayDepthTexture;

    PXR_NS::SdfPathVector hoverPaths;
    PXR_NS::SdfPath activePath;
    PXR_NS::SdfPathVector overlayPaths;

    OutlineMaskStyleParams style;

    std::vector<int> overlayIdValues;
    std::vector<int> hoverIdValues;
    std::vector<int> activeIdValues;
};

/// A task to convert outline primId and depth buffers to a color mask for overlay display.
class HVT_API OutlineMaskTask : public PXR_NS::HdxTask
{
public:
    OutlineMaskTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& id);

    ~OutlineMaskTask() override;

    void Prepare(PXR_NS::HdTaskContext* ctx,
                 PXR_NS::HdRenderIndex* renderIndex) override;

    void Execute(PXR_NS::HdTaskContext* ctx) override;

    void SetVisualizationMode(VisualizationMode mode);

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate,
               PXR_NS::HdTaskContext* ctx,
               PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    void _InitIfNeeded();
    void _CreateAovBindings();
    void _CleanupAovBindings();
    bool _Enabled() const;

    PXR_NS::HgiTextureHandle _GetInputTexture(
        PXR_NS::HdTaskContext* ctx, TfToken const& textureToken);
    PXR_NS::HdStGLSLProgramSharedPtr _GetComputeProgram();
    bool _CreateBufferResources(Hgi* hgi);
    HgiResourceBindingsSharedPtr _CreateResourceBindings(
        Hgi* hgi,
        PXR_NS::HgiTextureHandle const& defaultPrimIdTexture,
        PXR_NS::HgiTextureHandle const& defaultDepthTexture,
        PXR_NS::HgiTextureHandle const& basePrimIdTexture,
        PXR_NS::HgiTextureHandle const& baseDepthTexture,
        PXR_NS::HgiTextureHandle const& overlayPrimIdTexture,
        PXR_NS::HgiTextureHandle const& overlayDepthTexture,
        PXR_NS::HgiTextureHandle const& outputTexture);
    HgiComputePipelineSharedPtr _CreatePipeline(
        Hgi* hgi,
        uint32_t constantValuesSize,
        HgiShaderProgramHandle const& program);
    PXR_NS::TfToken _GetShaderFilePath();

    static constexpr int LOCAL_SIZE = 8;

    PXR_NS::HdRenderIndex* _renderIndex;

    PXR_NS::HgiTextureHandle _outputTexture;

    PXR_NS::HgiBufferHandle _overlayIdValuesBuffer;
    PXR_NS::HgiBufferHandle _hoverIdValuesBuffer;
    PXR_NS::HgiBufferHandle _activeIdValuesBuffer;

    OutlineMaskTaskParams _params;
    bool _isStormRenderer;
    bool _vpChanged;
    GfVec3i _workGroupCount;

    OutlineMaskTask() = delete;
    OutlineMaskTask(OutlineMaskTask const&) = delete;
    OutlineMaskTask& operator=(OutlineMaskTask const&) = delete;
};

template <class T>
struct HVT_API OutlineMaskConfig final {};

inline constexpr auto outlineMaskConfig = OutlineMaskConfig<OutlineMaskTaskParams const*>{};

} // namespace HVT_NS
