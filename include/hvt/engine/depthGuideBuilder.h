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

#include <hvt/engine/tileIterator.h>
#include <hvt/tasks/depthGuide.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/base/gf/vec2i.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/texture.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// clang-format on

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace HVT_NS
{

class FramePass;

/// Builds the depth guide of the SSAO task for a tiled render (see DepthGuide and
/// SSAOTaskParams::depthGuide).
///
/// The guide is a full-image R32F texture holding, per texel, the raw depth of the closest
/// surface of its scale x scale block of image pixels (a min-reduce), in the row order of the
/// backend's textures. The builder produces it with a depth-only sweep over the image: each
/// sweep tile is rendered by the application (through a callback) on a dedicated depth-only
/// frame pass, then min-reduced straight into the guide on the GPU. Memory is bounded by the
/// guide plus one tile's buffers; no full-image CPU buffer is allocated and nothing is read
/// back on the GPU path.
///
/// The built guide is shared: it outlives the builder, and a rebuild leaves the guides already
/// handed out untouched.
///
/// \code
/// HVT_NS::DepthGuideBuilder guideBuilder(hgi);
/// if (HVT_NS::DepthGuidePtr guide =
///         guideBuilder.Build(depthFramePass, imageSize, /*scale=*/4, tileSize, renderTile))
/// {
///     HVT_NS::SetSSAODepthGuide(*framePass.GetTaskManager(), guide);
///     // Initializes the TileIterator (the guide shrinks the SSAO margin), then renders the
///     // tiles.
/// }
/// \endcode
class HVT_API DepthGuideBuilder
{
public:
    /// How the min-reduce of each sweep tile into the guide is performed.
    enum class Reduction
    {
        /// A compute pass reduces each tile's resolved depth straight into the guide texture.
        /// No read-back; CPU memory is O(1).
        Gpu,
        /// Each tile's resolved depth is read back and reduced into a guide-sized CPU buffer,
        /// uploaded once. The reference implementation for tests, and a fallback where compute
        /// is unavailable. CPU memory: the guide plus one tile.
        Cpu
    };

    /// Renders one sweep tile. The builder has already set the framing (tile.framing) and the
    /// render buffer size of the sweep frame pass; the callback sets the same camera, clipping
    /// range and MSAA settings as the shaded pass, renders, and waits for the GPU. It must not
    /// change the framing or the render buffer size.
    /// \param tile The tile to render, from the builder's sweep TileIterator.
    using RenderTileFn = std::function<void(TileContext const& tile)>;

    /// Constructor.
    /// \param hgi The Hgi instance the guide texture and the reduction run on.
    /// \param reduction How each sweep tile is min-reduced into the guide (GPU by default).
    explicit DepthGuideBuilder(PXR_NS::Hgi* hgi, Reduction reduction = Reduction::Gpu);

    /// Destructor; destroys the reduction resources. The guides already built stay alive.
    ~DepthGuideBuilder();

    DepthGuideBuilder(DepthGuideBuilder const&)            = delete;
    DepthGuideBuilder& operator=(DepthGuideBuilder const&) = delete;

    /// Returns the guide memory, in bytes, for an image size and scale i.e., the size of the
    /// ceil(imageSize / scale) R32F guide. Zero for a non-positive size or scale.
    static size_t GetByteSize(PXR_NS::GfVec2i const& imageSize, int scale);

    /// Returns the smallest scale in {1, 2, 4} whose guide fits the given byte budget and the
    /// maximum texture dimension, or 0 when none does.
    /// \param imageSize The full image size, in pixels.
    /// \param budgetBytes The guide memory budget, in bytes.
    /// \note The maximum texture dimension is conservatively 16384 pixels per side, as Hgi does
    /// not report a limit.
    static int ChooseScale(PXR_NS::GfVec2i const& imageSize, size_t budgetBytes);

    /// Returns the sweep tile margin a scale needs: scale - 1, rounded up to even (0, 2, 4 for
    /// the scales 1, 2, 4). The margin guarantees that a pixel block straddling a tile border is
    /// still inside its owner's render buffer, so no cross-tile reduction is needed.
    static int GetSweepMargin(int scale);

    /// Runs the depth-only sweep over the image and fills the guide.
    ///
    /// The builder splits the image with its own TileIterator (margin GetSweepMargin(scale),
    /// tile size from sweepTileSizeHint, independent of the shaded tiles). For each sweep tile,
    /// it sets the tile framing and render buffer size on the frame pass, calls renderTile, then
    /// reduces the tile's resolved depth AOV into the guide. The sweep stops at the first tile
    /// that fails.
    ///
    /// The frame pass must be a dedicated depth-only pass, created with the DepthOnly preset
    /// task list (ViewportEngine::CreateFramePass with FramePass::PresetTaskLists::DepthOnly);
    /// any other pass is a coding error. Build sets the render outputs of the pass to the depth
    /// AOV, so the sweep allocates no color render buffer.
    ///
    /// \param sweepFramePass The depth-only frame pass the render-tile callback renders with.
    /// \param imageSize The full image size, in pixels.
    /// \param scale The guide downsampling scale; the guide is ceil(imageSize / scale). 1 is
    /// the full resolution.
    /// \param sweepTileSizeHint The desired sweep tile render buffer size, in pixels. Larger
    /// sweep tiles mean fewer geometry passes but larger transient buffers; the shaded tile
    /// size is a good default.
    /// \param renderTile Renders one sweep tile (see RenderTileFn).
    /// \return The complete guide, or null on invalid arguments or a failure.
    DepthGuidePtr Build(FramePass& sweepFramePass, PXR_NS::GfVec2i const& imageSize, int scale,
        PXR_NS::GfVec2i const& sweepTileSizeHint, RenderTileFn const& renderTile);

    /// Starts a new guide for an image size and scale, releasing the builder's reference to any
    /// previous one. Lower-level entry point, for applications that drive their own sweep
    /// loop: render each tile with tile.framing, then call AccumulateTile with the tile and its
    /// resolved depth AOV.
    /// \return False, leaving no guide, on invalid arguments.
    bool Begin(PXR_NS::GfVec2i const& imageSize, int scale);

    /// Min-reduces the owned guide texels of one sweep tile into the guide.
    /// \param tile The tile, as produced by a TileIterator with a margin of at least
    /// GetSweepMargin(scale), and rendered with tile.framing.
    /// \param resolvedTileDepth The tile's resolved depth AOV texture.
    /// \return False, leaving the guide incomplete, on invalid arguments or a failure.
    /// \note A tile owns the guide texels whose block of image pixels starts in the tile's
    /// valid region, so every guide texel is written exactly once and no clear is needed. A
    /// block reaches scale - 1 pixels past the valid region, so a tile margin smaller than
    /// that is a coding error.
    bool AccumulateTile(TileContext const& tile, PXR_NS::HgiTextureHandle const& resolvedTileDepth);

    /// Returns true once every guide texel was written exactly once i.e., the sweep covered the
    /// image.
    bool IsComplete() const;

    /// Returns the guide once complete (see IsComplete), or null.
    DepthGuidePtr GetGuide() const;

private:
    void _ResetGuide();
    void _DestroyGpuResources();

    // Creates the compute program, pipeline, sampler and resource bindings of the GPU
    // reduction, or returns false.
    bool _PrepareGpuReduction(PXR_NS::HgiTextureHandle const& resolvedTileDepth);
    // Min-reduces the owned guide texels of the tile on the GPU.
    bool _AccumulateTileGpu(PXR_NS::GfVec2i const& pixelOffset, PXR_NS::GfVec2i const& guideOrigin,
        PXR_NS::GfVec2i const& guideCount, PXR_NS::HgiTextureHandle const& resolvedTileDepth);
    // Min-reduces the owned guide texels of the tile on the CPU staging guide.
    bool _AccumulateTileCpu(PXR_NS::GfVec2i const& pixelOffset, PXR_NS::GfVec2i const& guideOrigin,
        PXR_NS::GfVec2i const& guideCount, PXR_NS::GfVec2i const& bufferSize,
        PXR_NS::HgiTextureHandle const& resolvedTileDepth);
    // Uploads the CPU staging guide into the guide texture, once the sweep is complete.
    void _UploadCpuGuide();

    PXR_NS::Hgi* _hgi { nullptr };
    Reduction _reduction { Reduction::Gpu };

    // The guide being built, and its texture.
    std::shared_ptr<DepthGuide> _guide;
    PXR_NS::HgiTextureHandle _guideTexture;
    int _scale { 0 };
    PXR_NS::GfVec2i _imageSize { 0, 0 };
    PXR_NS::GfVec2i _guideSize { 0, 0 };

    // The number of guide texels the accumulated tiles own; the sweep is complete when this
    // reaches the guide texel count.
    size_t _accumulatedTexels { 0 };

    // The CPU reduction staging buffer, in the row order of the backend's textures.
    std::vector<float> _cpuGuide;
    // The CPU reduction read-back of one tile, reused across tiles.
    std::vector<float> _cpuTileDepth;

    // The GPU reduction resources, created on the first tile and destroyed with the builder.
    PXR_NS::HgiSamplerHandle _sampler;
    PXR_NS::HgiShaderProgramHandle _computeProgram;
    PXR_NS::HgiComputePipelineHandle _computePipeline;
    PXR_NS::HgiResourceBindingsHandle _resourceBindings;
    uint64_t _resourceBindingsHash { 0 };
};

} // namespace HVT_NS
