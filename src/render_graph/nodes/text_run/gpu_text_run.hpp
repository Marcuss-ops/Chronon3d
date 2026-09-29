// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// gpu_text_run.hpp — canonical GPU text-run lowering contract.
//
// Internal-only header: lives under src/render_graph/nodes/text_run/, NOT in
// include/chronon3d/. Native text is lowered through the prepared MTSDF atlas;
// unsupported shapes fail closed and are handed to the explicit software
// fallback owned by text_run_execution.cpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/runtime/gpu_text_run_cache.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace chronon3d::runtime { class GpuGlyphAtlas; }

namespace chronon3d::graph::text_run {

/// GPU placement for one glyph when its animated state is submitted as a
/// separate MTSDF run.
///
/// Prepared glyph planes keep their immutable coordinates in run/layout
/// space.  A per-glyph scale must therefore be applied to the glyph's ink
/// around its layout origin, exactly like the software text matrix does.  If
/// the run translation were left unchanged, scaling a glyph at x=500 would
/// also move it by 500 * (scale - 1) pixels; the resulting frame-to-frame
/// drift is visible as text jitter during phrase emphasis animations.
struct GpuGlyphPlacement {
    float tx{0.0f};
    float ty{0.0f};
    float sx{1.0f};
    float sy{1.0f};
};

[[nodiscard]] inline GpuGlyphPlacement make_gpu_glyph_placement(
    const GlyphInstanceState& glyph,
    float run_tx,
    float run_ty,
    float run_sx,
    float run_sy) noexcept {
    // Scale.z is the existing text 2.5D uniform scale.  Keeping it here
    // makes the GPU placement match build_glyph_matrix() for the axes that
    // this MTSDF path supports (rotation/skew are rejected separately).
    const float glyph_scale_x = glyph.scale.x * glyph.scale.z;
    const float glyph_scale_y = glyph.scale.y * glyph.scale.z;

    // The immutable plane already contains layout_position.  Compensate the
    // run origin so only the glyph-local ink is scaled, while position and
    // baseline/anchor offsets remain in the same space as the CPU renderer.
    const float origin_x = glyph.position.x;
    const float origin_y = glyph.position.y + glyph.baseline_shift;
    return GpuGlyphPlacement{
        run_tx + run_sx * (origin_x +
                           (glyph.layout_position.x + glyph.anchor.x) *
                               (1.0f - glyph_scale_x)),
        run_ty + run_sy * (origin_y +
                           (glyph.layout_position.y + glyph.anchor.y) *
                               (1.0f - glyph_scale_y)),
        run_sx * glyph_scale_x,
        run_sy * glyph_scale_y};
}

/// Return true when the already-resolved glyph states fit the native MTSDF
/// dynamic-record contract. Animator evaluation itself stays in the canonical
/// text driver; resolved records are uploaded per glyph for native rendering.
[[nodiscard]] bool can_lower_gpu_text_animation(const TextRunShape& shape) noexcept;

/// True when the run-level emphasis profile can be sampled directly into one
/// TextRunDynamic record (one word, one selector, no per-glyph variation).
[[nodiscard]] bool can_lower_gpu_run_level_animation(const TextRunShape& shape) noexcept;

/// Sample the run-level emphasis profile into the caller-owned dynamic record.
/// The caller initializes transform/opacity/color before invoking this helper;
/// the animation values are composed onto those fields.
void sample_gpu_run_level_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept;

/// Legacy run-level selector sampler kept for callers that explicitly use
/// the compact single-selector contract. The MTSDF TextRun path now uploads
/// the canonical resolved GlyphInstanceState values per glyph.
void sample_gpu_text_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept;

/// Resolve and dispatch the prepared MTSDF representation of an already-shaped
/// text run. If the native contract is unavailable this returns an explicit
/// error; the caller owns the software fallback policy.
[[nodiscard]] graph::RenderOpResult draw_cached_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity);

/// Prepare the MTSDF representation of a TextRun into the persistent GPU glyph atlas
/// and return a prepared record suitable for cached dispatch.
[[nodiscard]] std::shared_ptr<const runtime::GpuPreparedTextRun> prepare_mtsdf_text_run(
    runtime::GpuGlyphAtlas& glyph_atlas,
    const assets::AssetResolver* resolver,
    const TextRunShape& shape);

} // namespace chronon3d::graph::text_run
