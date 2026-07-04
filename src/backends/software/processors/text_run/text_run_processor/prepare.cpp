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
// s.ss + s.active_tiers + ...).

#include "text_run_stages.hpp"
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>


namespace chronon3d::renderer::text_run_stages {

// ── Local helpers ───────────────────────────────────────────────────────────

// (Lifted verbatim from the anon-namespace of the old monolith.)
// Walks `glyphs` against its placed run, padding for blur + stroke + safety.
static void expand_per_glyph_bbox(
    float& min_x, float& min_y, float& max_x, float& max_y,
    const std::vector<GlyphInstanceState>& glyphs,
    const PlacedGlyphRun& placed
) {
    for (const auto& g : glyphs) {
        const float gx = g.layout_position.x + g.position.x;
        const float gy = g.layout_position.y + g.position.y;
        const float pad = g.blur + g.stroke_width + 8.0f;
        min_x = std::min(min_x, gx - pad);
        min_y = std::min(min_y, gy - pad);
        max_x = std::max(max_x, gx + placed.total_width /
            static_cast<float>(std::max(std::size_t{1}, placed.glyphs.size())) + pad);
        max_y = std::max(max_y, gy + placed.total_height + pad);
    }
}

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

    // ── World-bbox vs framebuffer intersection (silent fast-out) ──────────
    {
        const raster::BBox world_bbox =
            compute_text_run_world_bbox(shape, params.model_matrix, 0.0f);
        const raster::BBox fb_bbox{
            0, 0,
            static_cast<i32>(params.fb.width()),
            static_cast<i32>(params.fb.height())
        };
        const i32 x_lo = std::max(world_bbox.x0, fb_bbox.x0);
        const i32 x_hi = std::min(world_bbox.x1, fb_bbox.x1);
        const i32 y_lo = std::max(world_bbox.y0, fb_bbox.y0);
        const i32 y_hi = std::min(world_bbox.y1, fb_bbox.y1);
        if (x_hi <= x_lo || y_hi <= y_lo) {
            // Off-canvas text: silent success with explicit Outcome{0}.
            s.silent_success_empty = true;
            return graph::RenderOpResult(graph::RenderOpOutcome{0});
        }
    }

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
    expand_per_glyph_bbox(s.min_x, s.min_y, s.max_x, s.max_y,
                          shape.glyphs, layout.placed);
    if (shape.crossfade_layout && !shape.crossfade_glyphs.empty()) {
        expand_per_glyph_bbox(s.min_x, s.min_y, s.max_x, s.max_y,
                              shape.crossfade_glyphs,
                              shape.crossfade_layout->placed);
    }

    // ── Stage 1.2 shadow padding (active side ONLY — shadows track paint) ─
    for (const auto& g : shape.glyphs) {
        const float gx = g.layout_position.x + g.position.x;
        const float gy = g.layout_position.y + g.position.y;
        for (const auto& sh : shape.shadows) {
            if (!sh.enabled) continue;
            const float sh_pad = sh.blur + std::abs(sh.offset.x) +
                std::abs(sh.offset.y) + 8.0f;
            s.min_x = std::min(s.min_x, gx - sh_pad);
            s.max_x = std::max(s.max_x, gx + sh_pad);
            s.min_y = std::min(s.min_y, gy - sh_pad);
            s.max_y = std::max(s.max_y, gy + sh_pad);
        }
    }

    // ── Stage 2 derived image dimensions + supersample factor ────────────
    constexpr float kMargin = 8.0f;
    s.img_w = std::max(1, static_cast<int>(std::ceil(s.max_x - s.min_x + kMargin * 2.0f)));
    s.img_h = std::max(1, static_cast<int>(std::ceil(s.max_y - s.min_y + kMargin * 2.0f)));
    s.offset_x = s.min_x - kMargin;
    s.offset_y = s.min_y - kMargin;

    const float layer_scale = extract_uniform_scale(params.model_matrix);
    s.ss = supersampling_factor(layer_scale);
    s.ss_img_w = s.img_w * s.ss;
    s.ss_img_h = s.img_h * s.ss;
    s.ss_offset_x = s.offset_x * static_cast<float>(s.ss);
    s.ss_offset_y = s.offset_y * static_cast<float>(s.ss);

    // Outcome reports span_handles.size() (1 fanned out at the orchestrator).
    return graph::RenderOpResult(graph::RenderOpOutcome{s.span_handles.size()});
}

} // namespace chronon3d::renderer::text_run_stages
