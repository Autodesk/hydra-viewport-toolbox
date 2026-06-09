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

#include <hvt/engine/taskManager.h>
#include <hvt/tasks/outline/outlineMaskTask.h>
#include <hvt/tasks/outline/outlineOverlayTask.h>

#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace HVT_NS
{

PXR_NAMESPACE_USING_DIRECTIVE;

class FramePass;

// VisualizationMode (debug mask-pass modes) is defined in outlineMaskTask.h and
// reused here, so OutlineStyle and OutlineMaskTaskParams share one definition.

/// Visual style for the outline effect. Defaults are a usable starting point;
/// hosts should call SetStyle() to apply their theme rather than relying on these values.
/// Colors are RGBA in linear space.
struct HVT_API OutlineStyle
{
    PXR_NS::GfVec4f selectedColor { 1.0f, 1.0f, 1.0f, 1.0f };
    PXR_NS::GfVec4f selectedHoverColor { 1.0f, 0.84f, 0.0f, 1.0f };
    PXR_NS::GfVec4f selectionLeadColor { 0.0f, 0.8f, 1.0f, 1.0f };
    PXR_NS::GfVec4f selectionLeadHoverColor { 1.0f, 0.84f, 0.0f, 1.0f };
    PXR_NS::GfVec4f overlayColor { 1.0f, 1.0f, 1.0f, 0.7f };
    PXR_NS::GfVec4f overlayHoverColor { 1.0f, 0.84f, 0.0f, 1.0f };
    PXR_NS::GfVec4f unselectedHoverColor { 1.0f, 0.84f, 0.0f, 1.0f };
    PXR_NS::GfVec4f defaultColor { 0.5f, 0.5f, 0.5f, 1.0f };

    /// Render a faint outline on every prim in the scene (visible internal edges).
    /// Disable for performance on heavy scenes.
    bool enableDefaultOutlines { true };

    /// Edge softness: 0.0 = hard edges, 1.0 = full coverage-based anti-aliasing.
    float softnessStrength { 1.0f };

    /// Exponent for softness falloff. Lower = thicker lines.
    float softnessFalloff { 0.4f };

    /// Post-mask Gaussian blur (None / 3x3 / 5x5).
    BlurMode blurMode { BlurMode::Blur3x3 };

    /// Multiplier applied to the blur output (default 1.0).
    float blurIntensity { 1.0f };

    /// Mask-pass debug visualization (normal rendering uses VISUALIZE_MASK_3x3).
    VisualizationMode maskVisualizationMode { VisualizationMode::VISUALIZE_MASK_3x3 };

    bool operator==(OutlineStyle const& other) const
    {
        return selectedColor == other.selectedColor
            && selectedHoverColor == other.selectedHoverColor
            && selectionLeadColor == other.selectionLeadColor
            && selectionLeadHoverColor == other.selectionLeadHoverColor
            && overlayColor == other.overlayColor
            && overlayHoverColor == other.overlayHoverColor
            && unselectedHoverColor == other.unselectedHoverColor
            && defaultColor == other.defaultColor
            && enableDefaultOutlines == other.enableDefaultOutlines
            && softnessStrength == other.softnessStrength
            && softnessFalloff == other.softnessFalloff
            && blurMode == other.blurMode
            && blurIntensity == other.blurIntensity
            && maskVisualizationMode == other.maskVisualizationMode;
    }

    bool operator!=(OutlineStyle const& other) const { return !(*this == other); }
};

/// Path buckets the outline consumes each frame. The four buckets are the contract
/// shared with the underlying mask shader:
///
/// - selectedPaths : drives the base prim-IDs collection rendered into texture
/// - activePath    : the "lead" item; colored distinctly when set
/// - hoverPaths    : currently-hovered candidates; colored as hover and merged into base
/// - overlayPaths  : independent layer (e.g. manipulators); rendered into its own texture
///
/// Hosts that don't have an active/lead concept may leave activePath empty; hosts
/// without overlays may leave overlayPaths empty.
struct HVT_API OutlineInputs
{
    PXR_NS::SdfPathVector selectedPaths;
    PXR_NS::SdfPath       activePath;
    PXR_NS::SdfPathVector hoverPaths;
    PXR_NS::SdfPathVector overlayPaths;

    /// Paths excluded from the default (whole-scene) outline bucket only. Hosts use
    /// this to keep transient / manipulator roots out of the faint internal-edge
    /// outlines drawn when enableDefaultOutlines is set. Ignored when empty, and has
    /// no effect on the selected / hover / overlay buckets.
    PXR_NS::SdfPathVector excludePaths;

    /// True when a single hovered candidate is already in the selection set (uses
    /// selectedHoverColor / selectionLeadHoverColor in the mask shader).
    bool isHoverSelected { false };
};

/// Outline is a feature-level wrapper around the three outline tasks
/// (OutlinePrimIdsTask + OutlineMaskTask + OutlineOverlayTask).
///
/// It owns the task IDs, AOV texture bindings, and ordering inside the frame pass
/// so consumers don't have to rediscover the correct wiring. State is push-based:
/// call SetInputs() / SetStyle() whenever the host's selection or style changes.
///
/// The class deliberately knows nothing about how the host tracks selection
/// (signal/observer/scene-index/etc.). Wire your host-side selection source to
/// SetInputs() in whatever way fits your application.
class HVT_API Outline
{
public:
    Outline();
    ~Outline();

    Outline(Outline const&)            = delete;
    Outline& operator=(Outline const&) = delete;

    /// Install the three outline tasks into the given frame pass, in the correct
    /// internal order and with correct AOV bindings. Call once per frame pass.
    ///
    /// \param framePass The frame pass to install the tasks into.
    /// \param atPos     Anchor task path used to position the outline tasks relative
    ///                  to an existing task in the pass (e.g. the color-correction
    ///                  task). When empty, behavior follows order: insertAtEnd
    ///                  appends to the pass; insertBefore/insertAfter is treated
    ///                  the same as insertAtEnd.
    /// \param order     Where to place the outline tasks relative to atPos.
    ///                  Mirrors TaskManager::InsertionOrder semantics.
    ///
    /// \note The three outline tasks remain in their fixed internal order
    /// (prim-IDs -> mask -> overlay); atPos order only controls where the
    /// group as a whole lands in the frame pass.
    ///
    /// \note The outline tasks source their per-frame viewport parameters
    /// (render-buffer size, camera, framing, window policy) directly from
    /// \p framePass on every commit, so the host does not push them. This means
    /// \p framePass must outlive this Outline instance (the same lifetime the
    /// installed task IDs already require).
    void Install(FramePass& framePass,
                 PXR_NS::SdfPath const& atPos = PXR_NS::SdfPath(),
                 TaskManager::InsertionOrder order = TaskManager::InsertionOrder::insertAtEnd);

    /// Push new path inputs. Cheap when the inputs haven't changed.
    void SetInputs(OutlineInputs inputs);

    /// Push new style. Cheap when the style hasn't changed.
    void SetStyle(OutlineStyle style);

    /// Optional debug read-back. A "hit" is a no-op SetInputs() call (inputs unchanged);
    /// a "miss" is a call that triggered re-evaluation on the next commit.
    struct CacheStats
    {
        size_t hits             = 0;
        size_t misses           = 0;
        size_t totalQueries     = 0;
        size_t maxCollectionSize = 0;
        size_t avgCollectionSize = 0;
    };
    CacheStats GetCacheStats() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace HVT_NS
