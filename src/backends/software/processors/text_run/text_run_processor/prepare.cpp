// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/processors/text_run/text_run_processor/prepare.cpp
// ═══════════════════════════════════════════════════════════════════════════
//
// M1.5#6 — Stage 1 of the draw_text_run pipeline: prepare_text_run.
//
// Owns:
//   - Input validation (Stage 0 in pre-M1.5#6).
//   - World-bbox intersection silent fast-out (off-canvas text skips the
//     heavy raster pass — `Outcome{0}` early return).
//   - framebuffer dimension guard.
//   - Scratch Handle acquisition (the ONLY place this is done).
//   - Per-span font handle resolution (Phase 1.4 multi-font path) OR
//     single-font alias (legacy / pre-Phase-1.4).
//   - Glyph bbox expansion (active side + crossfade side) and shadow
//     padding.
//   - Image dimensions + supersample factor precomputation (FASE 3b).
//
// Populates TextRunStageState fully (raster uses s.span_handles + s.img_w +
// s.raster_space + s.active_tiers + ...).

#include "text_run_stages.hpp"
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// TICKET-TEXT-CLIP-ASCENT — diagnostic spdlog include.
// Gated on CHRONON3D_TEXT_CLIP_DEBUG env var; no-op in production
// builds when the var is unset.  Logs baseline metrics + final scratch
// surface dimensions + raster space scale.
#include <spdlog/spdlog.h>


namespace chronon3d::renderer::text_run_stages {

// ── Local helpers ───────────────────────────────────────────────────────────

// NOTE: expand_per_glyph_bbox() was removed in TICKET-TEXT-CLEANUP-2.
// Replaced by the canonical compute_text_run_visual_bounds() from
// text_run_geometry.hpp — single source of truth for per-glyph local-space
// bounds.  The old function used placed.total_width / glyph_count as an
// x-extent estimate and lacked 2.5D shear/scale padding.

// (Lifted verbatim from FASE 3b helpers.)
[[nodiscard]] static float extract_uniform_scale(const Mat4& model) {
    const float sx = std::sqrt(model[0][0] * model[0][0] + model[0][1] * model[0][1]);
    const float sy = std::sqrt(model[1][0] * model[1][0] + model[1][1] * model[1][1]);
    return std::max(sx, sy);
}

[[nodiscard]] static int supersampling_factor(float scale) {
    if (scale <= 1.25f) return 1;
    if (scale <= 2.5f)  return 2;
    return 4;
}

// ═══ prepare_text_run — Stage 1 entry ══════════════════════════════════════

[[nodiscard]] graph::RenderOpResult prepare_text_run(
    const SoftwareProcessorContext& rctx,
    const TextRunDrawParams&        params,
    TextRunStageState&              s
) {
    const auto& shape = params.shape;

    // ── Stage 0 input validation ─────────────────────────────────────────
    if (!shape.layout || shape.glyphs.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "prepare_text_run: empty layout or empty glyph set"
        });
    }

    const auto& layout = *shape.layout;
    if (layout.font.font_path.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "prepare_text_run: empty layout.font.font_path"
        });
    }

    // TICKET-TEXT-CLEANUP-4: world-bbox vs framebuffer intersection
    // fast-out removed entirely (was silent_success_empty).  The bbox
    // approximation can be wrong (2.5D shear, animation), and silently
    // skipping visible text is worse than rendering one extra off-canvas
    // frame.  The world_bbox computation that was here is now unused —
    // removed along with the fast-out to avoid wasted work per frame.

    // ── Empty-framebuffer guard ───────────────────────────────────────────
    if (params.fb.width() == 0 || params.fb.height() == 0) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "prepare_text_run: zero-dimension framebuffer"
        });
    }

    // ── Text resources + asset_resolver presence ─────────────────────────
    if (!rctx.text_resources || !rctx.asset_resolver) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "prepare_text_run: text_resources or asset_resolver is null"
        });
    }

    // ── Scratch Handle acquire (RAII; released on scope exit) ─────────────
    s.scratch_handle = acquire_scratch_handle(rctx);
    if (!s.scratch_handle) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "prepare_text_run: failed to acquire scratch state from TextScratchManager"
        });
    }

    // ── Per-span font resolve (Phase 1.4 multi-font OR single-font alias) ─
    const bool has_font_spans = !layout.font_spans.empty();
    if (has_font_spans) {
        s.span_handles.reserve(layout.font_spans.size());
        s.span_fonts.reserve(layout.font_spans.size());
        for (const auto& spn : layout.font_spans) {
            FontFaceHandle span_handle = rctx.text_resources->resolve_handle(
                spn.font.font_path, layout.font_size, *rctx.asset_resolver);
            if (!span_handle.valid()) {
                return graph::RenderOpResult(graph::RenderBackendError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "prepare_text_run: failed to load font face for span ('" +
                    spn.font.font_path + "', Phase 1.4 multi-font path)"
                });
            }
            s.span_handles.push_back(std::move(span_handle));
            BLFont span_blfont;
            span_blfont.createFromFace(*s.span_handles.back().bl_face, layout.font_size);
            s.span_fonts.push_back(std::move(span_blfont));
        }
        s.per_glyph_span_idx.assign(layout.placed.glyphs.size(), 0);
        for (std::size_t si = 0; si < layout.font_spans.size(); ++si) {
            const auto& spn = layout.font_spans[si];
            const std::uint32_t span_end = std::min<std::uint32_t>(
                spn.glyph_end,
                static_cast<std::uint32_t>(s.per_glyph_span_idx.size()));
            for (std::uint32_t gi = spn.glyph_begin; gi < span_end; ++gi) {
                s.per_glyph_span_idx[gi] = si;
            }
        }
    } else {
        FontFaceHandle single_handle = rctx.text_resources->resolve_handle(
            layout.font.font_path, layout.font_size, *rctx.asset_resolver);
        if (!single_handle.valid()) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::ExecutionFailure,
                "prepare_text_run: failed to load font face for " +
                layout.font.font_path
            });
        }
        s.span_handles.push_back(std::move(single_handle));
        BLFont single_blfont;
        single_blfont.createFromFace(*s.span_handles.back().bl_face, layout.font_size);
        s.span_fonts.push_back(std::move(single_blfont));
        s.per_glyph_span_idx.assign(layout.placed.glyphs.size(), 0);
    }

    // ── Stage 1.1 bbox expansion (active side + crossfade side) ──────────
    // Uses the canonical compute_text_run_visual_bounds() which handles
    // both active + crossfade sides, 2.5D shear/scale padding, and
    // per-glyph advance estimation (replacing the old total_width/glyph_count
    // heuristic).
    if (auto local_bounds = compute_text_run_visual_bounds(shape)) {
        s.min_x = std::min(s.min_x, local_bounds->min_x);
        s.min_y = std::min(s.min_y, local_bounds->min_y);
        s.max_x = std::max(s.max_x, local_bounds->max_x);
        s.max_y = std::max(s.max_y, local_bounds->max_y);
    }

    // ── Stage 1.2 shadow padding (active side ONLY — shadows track paint) ─
    // TICKET-TEXT-CLIP-ASCENT — same baseline/ascent fix as in
    // compute_text_run_visual_bounds(): previously `s.min_y = gy - sh_pad`
    // and `s.max_y = gy + sh_pad` cut off ascenders and descenders in
    // the shadow scratch surface.  Use real baseline metrics so the
    // shadow scratch surface is sized to the actual glyph ink extent
    // (plus the shadow's blur + offset + 8 px safety padding).
    const float shadow_ascent  = std::max(
        0.0f, shape.layout->placed.ascent);
    const float shadow_descent = std::max(
        0.0f, shape.layout->placed.descent);
    const auto& shadow_placed  = shape.layout->placed;
    for (std::size_t gi = 0; gi < shape.glyphs.size(); ++gi) {
        const auto& g = shape.glyphs[gi];
        const float gx = g.layout_position.x + g.position.x;
        const float gy = g.layout_position.y + g.position.y;
        const float shadow_scale_y = std::abs(g.scale.y * g.scale.z);
        const float shadow_advance = std::max(
            1.0f, std::abs(shadow_placed.glyphs[gi].advance_x));
        for (const auto& sh : shape.shadows) {
            if (!sh.enabled) continue;
            const float sh_pad = sh.blur + std::abs(sh.offset.x) +
                std::abs(sh.offset.y) + 8.0f;
            s.min_x = std::min(s.min_x, gx - sh_pad);
            s.max_x = std::max(s.max_x, gx + shadow_advance + sh_pad);
            s.min_y = std::min(
                s.min_y, gy - shadow_ascent * shadow_scale_y - sh_pad);
            s.max_y = std::max(
                s.max_y, gy + shadow_descent * shadow_scale_y + sh_pad);
        }
    }

    // ── Stage 2 derived image dimensions + supersample factor ────────────
    constexpr float kMargin = 8.0f;
    s.img_w = std::max(1, static_cast<int>(std::ceil(s.max_x - s.min_x + kMargin * 2.0f)));
    s.img_h = std::max(1, static_cast<int>(std::ceil(s.max_y - s.min_y + kMargin * 2.0f)));
    s.offset_x = s.min_x - kMargin;
    s.offset_y = s.min_y - kMargin;

    const float layer_scale = extract_uniform_scale(params.model_matrix);
    s.raster_space.scale = supersampling_factor(layer_scale);
    s.ss_img_w = s.img_w * s.raster_space.scale;
    s.ss_img_h = s.img_h * s.raster_space.scale;
    s.raster_space.offset_x = s.offset_x * static_cast<float>(s.raster_space.scale);
    s.raster_space.offset_y = s.offset_y * static_cast<float>(s.raster_space.scale);

    // ── Stage 1.3 diagnostic log (env-var gated, cached once per proc) ──
    // TICKET-TEXT-CLIP-ASCENT — confirms the bbox math without rebuilding
    // the renderer.  Set CHRONON3D_TEXT_CLIP_DEBUG=1 in the render
    // environment to enable.  Logs:
    //   - final local-bbox (min_x/max_x/min_y/max_y from compute_text_run_visual_bounds)
    //   - scratch image dimensions + offset
    //   - raster space scale + supersampled dimensions
    //   - placed ascent + descent + total_height (run-wide metrics)
    //   - per-glyph (FIRST + LAST) baseline gy + top/bottom (gy - ascent,
    //     gy + descent) + surface_top/surface_bottom (projected by
    //     s.offset_y and s.raster_space.scale).  The bug detector
    //     (user spec verbatim):
    //         surface_top  <  0              ==> ascender clipped above
    //         surface_bottom >  ss_img_h     ==> descender clipped below
    //         last_glyph surface_right > ss_img_w ==> right-edge clipped
    static const bool kClipDebugEnabled = []() {
        const char* env = std::getenv("CHRONON3D_TEXT_CLIP_DEBUG");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    if (kClipDebugEnabled) {
        spdlog::warn(
            "[text-clip-debug] text='{}' "
            "local_bounds=(min_x={:.2f}, min_y={:.2f}, "
            "max_x={:.2f}, max_y={:.2f}) "
            "scratch img={}x{} offset=({:.2f},{:.2f}) "
            "raster_space scale={} ss_img={}x{} "
            "placed ascent={:.2f} descent={:.2f} total_height={:.2f}",
            layout.source_text,
            s.min_x, s.min_y, s.max_x, s.max_y,
            s.img_w, s.img_h,
            s.offset_x, s.offset_y,
            s.raster_space.scale,
            s.ss_img_w, s.ss_img_h,
            layout.placed.ascent,
            layout.placed.descent,
            layout.placed.total_height);
        // Per-glyph (FIRST + LAST) diagnostic -- user-spec verbatim:
        //   surface_top    = (gy - ascent    - s.offset_y) * s.raster_space.scale
        //   surface_bottom = (gy + descent - s.offset_y) * s.raster_space.scale
        // The loop iterates {0, size-1}; if shape has glyphs it always
        // logs both ends so end-of-run clipping is detected separately
        // from start-of-run clipping.
        const float ss_scale_f = static_cast<float>(s.raster_space.scale);
        for (std::size_t gi : {std::size_t{0}, shape.glyphs.size() - 1}) {
            if (shape.glyphs.empty()) break;
            const auto& g = shape.glyphs[gi];
            const float gx = g.layout_position.x + g.position.x;
            const float gy = g.layout_position.y + g.position.y;
            const float top          = gy - layout.placed.ascent;
            const float bottom       = gy + layout.placed.descent;
            const float surface_top    = (top    - s.offset_y) * ss_scale_f;
            const float surface_bottom = (bottom - s.offset_y) * ss_scale_f;
            spdlog::warn(
                "[text-clip-debug:glyph] i={} gx={:.2f} gy={:.2f} "
                "top={:.2f} bottom={:.2f} "
                "surface_top={:.2f} surface_bottom={:.2f}",
                gi, gx, gy,
                top, bottom,
                surface_top, surface_bottom);
        }
    }

    // Outcome reports span_handles.size() (1 fanned out at the orchestrator).
    return graph::RenderOpResult(graph::RenderOpOutcome{s.span_handles.size()});
}

} // namespace chronon3d::renderer::text_run_stages
