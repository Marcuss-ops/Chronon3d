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
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::graph::text_run {

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
    float opacity{1.0f};
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
    std::span<const GpuTextGlyph> glyphs);

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

} // namespace chronon3d::graph::text_run
