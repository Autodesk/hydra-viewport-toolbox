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

class FramePass;

namespace Outline
{

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
    /// Opt-in: a bare Install() with no selection then costs nothing. Enable for the
    /// whole-scene internal-edge look; disable (default) for performance on heavy scenes.
    bool enableDefaultOutlines { false };

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

/// Path buckets the outline highlight pass consumes each frame. The four buckets are the contract
/// shared with the underlying mask shader:
///
/// - selectedPaths : drives the base prim-IDs collection rendered into texture
/// - leadPath      : the "lead" item; colored distinctly when set
/// - hoverPaths    : currently-hovered candidates; colored as hover and merged into base
/// - overlayPaths  : independent layer (e.g. manipulators); rendered into its own texture
///
/// Hosts that don't have a lead concept may leave leadPath empty; hosts
/// without overlays may leave overlayPaths empty.
///
/// \note leadPath MUST be one of selectedPaths (or hoverPaths) when set. The base prim-ID
/// texture is rasterized from selectedPaths + hoverPaths only; leadPath is used purely to
/// recolor the matching primId. A leadPath that is not in those buckets is never rasterized,
/// so its lead outline will silently not appear.
struct HVT_API OutlineInputs
{
    PXR_NS::SdfPathVector selectedPaths;
    PXR_NS::SdfPath       leadPath;
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

    bool operator==(OutlineInputs const& other) const
    {
        return selectedPaths == other.selectedPaths && leadPath == other.leadPath
            && hoverPaths == other.hoverPaths && overlayPaths == other.overlayPaths
            && excludePaths == other.excludePaths && isHoverSelected == other.isHoverSelected;
    }

    bool operator!=(OutlineInputs const& other) const { return !(*this == other); }
};

/// OutlineManager is a feature-level wrapper around the five internal outline tasks
/// (three OutlinePrimIdsTask passes + one OutlineMaskTask + one OutlineOverlayTask).
///
/// It owns the task IDs, AOV texture bindings, and ordering inside the frame pass
/// so consumers don't have to rediscover the correct wiring. State is push-based:
/// call SetInputs() / SetStyle() whenever the host's selection or style changes.
///
/// The class deliberately knows nothing about how the host tracks selection
/// (signal/observer/scene-index/etc.). Wire your host-side selection source to
/// SetInputs() in whatever way fits your application.
///
/// \note Thread-safety: OutlineManager is NOT internally synchronized. Call every
/// method (Install, SetInputs, SetStyle) from the thread that drives the frame pass's
/// per-frame commit -- i.e. the render thread. SetInputs()/SetStyle() mutate shared state
/// that the task commit callbacks read during TaskManager::CommitTaskValues(), and
/// Install() mutates the frame pass's task list; calling any of them from another thread
/// races the commit with no lock. If a host must feed selection from another thread,
/// marshal it onto the render thread before calling these methods (or add external
/// synchronization). Destroying the manager is safe from any thread (see the lifetime
/// note on Install()).
class HVT_API OutlineManager
{
public:
    OutlineManager();
    ~OutlineManager();

    OutlineManager(OutlineManager const&)            = delete;
    OutlineManager& operator=(OutlineManager const&) = delete;

    /// Install the five outline tasks into the given frame pass, in the correct
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
    /// \note The five outline tasks remain in their fixed internal order
    /// (prim-IDs passes -> mask -> overlay); atPos order only controls where the
    /// group as a whole lands in the frame pass.
    ///
    /// \note The outline tasks source their per-frame viewport parameters
    /// (render-buffer size, camera, framing, window policy) directly from
    /// \p framePass on every commit, so the host does not push them.
    ///
    /// \note The installed tasks are owned by the frame pass's TaskManager, not by this
    /// manager. Their commit callbacks hold a weak reference to the manager's state, so if
    /// the manager is destroyed while its tasks are still installed the next commit safely
    /// no-ops. Commits only run while \p framePass is alive, and the tasks are removed when
    /// \p framePass is destroyed, so no explicit task removal is ever needed.
    ///
    /// \pre \p framePass MUST outlive this OutlineManager. The manager caches the frame pass
    /// and reads it on every commit; it must not be used after the pass is destroyed. There is
    /// no re-install path -- a second Install() is ignored (emits TF_WARN). If a host recreates
    /// its frame pass (renderer switch, resize-driven rebuild, etc.), destroy and recreate the
    /// OutlineManager alongside the new pass. This matches HVT convention: everything installed
    /// into a frame pass is owned by it and torn down with it.
    ///
    /// \note Call from the render (commit) thread only -- see the class thread-safety note.
    void Install(FramePass& framePass,
                 PXR_NS::SdfPath const& atPos = PXR_NS::SdfPath(),
                 TaskManager::InsertionOrder order = TaskManager::InsertionOrder::insertAtEnd);

    /// Push new path inputs. Cheap when the inputs haven't changed.
    /// Call from the render (commit) thread only -- see the class thread-safety note.
    void SetInputs(OutlineInputs inputs);

    /// Push new style. Cheap when the style hasn't changed.
    /// Call from the render (commit) thread only -- see the class thread-safety note.
    void SetStyle(OutlineStyle style);

    /// Optional debug read-back. A "hit" is a no-op SetInputs() call (inputs unchanged);
    /// a "miss" is a call that triggered re-evaluation on the next commit.
    struct CacheStats
    {
        size_t hits              = 0;
        size_t misses            = 0;
        size_t totalQueries      = 0;
        size_t maxInputPathCount = 0;
        size_t avgInputPathCount = 0;
    };
    CacheStats GetCacheStats() const;

private:
    // Not a classic pimpl: this is the manager's mutable state (style, inputs, task IDs, cached
    // frame pass, stats), held by shared_ptr specifically so the per-task commit callbacks can
    // capture a weak_ptr and lock()-check it. Those callbacks live in tasks owned by the frame
    // pass, so they outlive this manager; the weak_ptr lets a commit that runs after the manager
    // is destroyed safely no-op instead of dereferencing freed state.
    //
    // The built-in HVT managers (RenderBufferManager, LightingManager, SelectionHelper) get the
    // same callback safety with a plain unique_ptr pimpl because the frame pass owns *them* as
    // shared_ptr SettingsProviders -- the callbacks weak-reference the manager itself. This
    // OutlineManager is deliberately host-owned (by value) and decoupled from the frame pass
    // ownership graph, so there is no manager-level shared_ptr to weak-reference; the shared/weak
    // boundary is pushed down to this state instead. A raw pointer here could not be liveness-checked.
    class SharedState;
    std::shared_ptr<SharedState> _state;
};

} // namespace HVT_NS::Outline

} // namespace HVT_NS
