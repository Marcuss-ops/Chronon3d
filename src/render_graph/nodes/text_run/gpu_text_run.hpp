// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// gpu_text_run.hpp — packed-atlas builder for the GPU text-run fast path.
//
// Internal-only header: lives under src/render_graph/nodes/text_run/, NOT in
// include/chronon3d/.  It implements the "packed atlas builder" half of the
// GPU text path: the caller supplies already-rasterized glyph bitmaps (the
// CPU glyph atlas / Blend2D is the bitmap source), this module packs them
// into ONE packed atlas texture and dispatches RenderBackend::
// draw_text_run_surface (the GPU text-run kernel) in a single pass.
//
// This module is deliberately backend-neutral: it knows nothing about
// Blend2D, FreeType or HarfBuzz.  It only consumes premultiplied RGBA
// bitmaps + per-glyph destination placement, so the same code paths works
// for whatever bitmap source the render loop eventually wires in.
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

/// Return true when the text animator stack can be lowered to the GPU text
/// dynamic record without rebuilding glyph state. The supported profiles are
/// the canonical forward glyph typewriter and a full-run word emphasis whose
/// position/scale/opacity/fill are uniform across the run.
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

/// Samples the per-glyph GPU animator contract into one dynamic record.
/// Shaping, glyph atlas generation and selector compilation remain outside
/// the frame loop; the Vulkan text shaders evaluate the selector and the
/// cumulative tracking offset per glyph.
void sample_gpu_text_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept;

/// One rasterized glyph bitmap + its destination placement.
///
/// `rgba` is a tightly-packed PREMULTIPLIED RGBA float buffer of exactly
/// `width * height * 4` elements (the convention produced by the CPU glyph
/// atlas and consumed by the surface upload path).  `dst_x`/`dst_y` is the
/// top-left origin of the glyph quad in destination-canvas pixel space.
/// `opacity` is the per-glyph premultiplied opacity (the caller folds the
/// layer opacity in before packing).
struct GpuTextGlyph {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<float> rgba;
    std::int32_t dst_x{0};
    std::int32_t dst_y{0};
    float scale_x{1.0f};
    float scale_y{1.0f};
    float opacity{1.0f};
    std::shared_ptr<const std::vector<float>> shared_rgba;
    float highlight_start_frame{-1.0f};
    float highlight_end_frame{-1.0f};
    [[nodiscard]] std::span<const float> pixels() const noexcept {
        if (shared_rgba) return *shared_rgba;
        return rgba;
    }
};

/// Pack `glyphs` into one packed atlas surface and composite them into
/// `destination` via `RenderBackend::draw_text_run_surface`.
///
/// This is the "packed atlas builder" primitive: it owns glyph packing
/// (shelf packing, fixed atlas width), the atlas upload, the per-glyph
/// `GlyphInstance` construction, and the single-kernel dispatch.  The
/// transient atlas surface is released before returning; only the
/// destination's surface handle persists (attached via
/// `ensure_native_surface`).
///
/// Returns `RenderOpOutcome{glyphs.size()}` on success, or a
/// `RenderBackendError`.  An empty input is a valid no-op success.  Callers
/// that cannot guarantee a native surface (no backend / surface registry)
/// receive `UnsupportedCapability` and must fall back to `draw_text_run`.
[[nodiscard]] graph::RenderOpResult draw_packed_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    std::span<const GpuTextGlyph> glyphs,
    float current_frame = 0.0f,
    const Color& highlight_color = Color{},
    bool highlight_enabled = false);

/// Use the renderer-owned CPU glyph atlas as the bitmap source and keep the
/// packed GPU atlas in the runtime GpuAssetCache. This is the graph bridge for
/// already-shaped text: shaping stays CPU-side, glyph placement is updated per
/// frame, while the atlas upload is reused across frames/jobs.
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
