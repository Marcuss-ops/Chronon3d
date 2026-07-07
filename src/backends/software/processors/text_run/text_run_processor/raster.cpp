// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/processors/text_run/text_run_processor/raster.cpp
// ═══════════════════════════════════════════════════════════════════════════
//
// M1.5#6 — Stage 2 of the draw_text_run pipeline: rasterize_prepared_run.
//
// Owns:
//   - SingleGlyphRun helper (fast per-glyph BL fill).
//   - build_blur_tiers — O(G) per-side classification into 5 tiers.
//   - render_tier_to_image (file-local lambda in rasterize_prepared_run):
//       renders glyphs of a tier into a BLImage, applies per-tier blur,
//       and returns glyphs drawn.  Captures `s` (`scratch_handle` +
//       `span_handles` + `span_fonts` + `per_glyph_span_idx` + the canonical
//       `kBlurTierRadii`).
//   - Stage 4 (shadow stack), Stage 5 (main tiered), Stage 6 (crossfade side)
//       render loops.
//   - Downsample pass when s.ss > 1.
//
// Mutates: s.img, s.glyphs_drawn, s.active_tiers, s.crossfade_tiers,
//          s.all_active_glyphs (built here).

#include "text_run_stages.hpp"
#include "../../../utils/blend2d_bridge.hpp"
#include "../../../utils/blend2d_paint.hpp"  // canonical to_bl_rgba + build_bl_gradient
// NOTE: `build_glyph_matrix` is defined in text_run_matrix.cpp (same
// linker target).  The function lives in `chronon3d::renderer` namespace;
// we forward-declare it below in `chrono::renderer` scope so unqualified
// lookup from text_run_stages's lambda finds it via parent-namespace
// name visibility rules.  Linker resolves at link time.
//
// `detail::bucket_radius_for_tier` is inlined in
// include/chronon3d/backends/software/text_run_processor.hpp (the public
// header for the module); text_run_stages.hpp transitively pulls it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Forward-declare build_glyph_matrix in its canonical namespace so the
// lambda (declared in text_run_stages) can call it via unqualified lookup.
namespace chronon3d::renderer {
[[nodiscard]] BLMatrix2D build_glyph_matrix(const GlyphInstanceState& g);
}  // namespace chronon3d::renderer

namespace chronon3d::renderer::text_run_stages {

namespace {

// PR3 — canonical to_bl_rgba pulled into text_run_stages namespace so
// unqualified calls from inside this TU's pipeline find it.  Same pattern
// as the original monolith (text_run_processor.cpp line 39).
using chronon3d::blend2d_bridge::paint::to_bl_rgba;

// ── SingleGlyphRun — fast per-glyph BL fill (PHASE 1.0 legacy path retained) ─
struct SingleGlyphRun {
    std::uint32_t    glyph_id{0};
    BLGlyphPlacement placement{};
    BLGlyphRun       bl_run{};

    static SingleGlyphRun from(const PlacedGlyph& pg) {
        SingleGlyphRun r;
        r.glyph_id = pg.glyph_id;
        r.placement.placement.reset(0.0, 0.0);
        r.placement.advance.reset(0.0, 0.0);
        r.bl_run.glyphData = &r.glyph_id;
        r.bl_run.glyphAdvance = static_cast<int8_t>(sizeof(std::uint32_t));
        r.bl_run.placementData = &r.placement;
        r.bl_run.placementAdvance = static_cast<int8_t>(sizeof(BLGlyphPlacement));
        r.bl_run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
        r.bl_run.size = 1;
        return r;
    }
};

// ── build_blur_tiers — O(G), once per side ─────────────────────────────────
//
// Glyphs grouped into 5 tiers by per-glyph blur.  Each tier rasterizes onto
// its own surface, blurs at the tier's radius, then composites onto the
// final image SRC_OVER.  Tier mapping (matches kBlurTierRadii values):
//   blur  > 16  -> tier 4 (radius 20, capped)
//   blur  > 8   -> tier 3 (radius 13)
//   blur  > 4   -> tier 2 (radius 7)
//   blur  > 0   -> tier 1 (radius 2)
//   blur == 0   -> tier 0 (radius 0, no blur)
[[nodiscard]] BlurTiers build_blur_tiers(
    const std::vector<GlyphInstanceState>& glyphs
) {
    BlurTiers tiers;
    for (std::uint32_t gi = 0; gi < glyphs.size(); ++gi) {
        const float blur = glyphs[gi].blur;
        int tier = 0;
        if      (blur > 16.0f) tier = 4;
        else if (blur >  8.0f) tier = 3;
        else if (blur >  4.0f) tier = 2;
        else if (blur >  0.0f) tier = 1;
        tiers[tier].push_back(gi);
    }
    return tiers;
}

} // anonymous namespace

// ═══ rasterize_prepared_run — Stage 2 entry ═════════════════════════════════

[[nodiscard]] graph::RenderOpResult rasterize_prepared_run(
    const SoftwareProcessorContext& rctx,
    const TextRunDrawParams&        params,
    TextRunStageState&              s
) {
    (void)rctx;  // (consumed implicitly via scratch_handle + s.* field-set in Stage 1)

    // TICKET-TEXT-CLEANUP-4: silent_success_empty short-circuit removed.
    // Off-canvas text is now always rasterized (bbox approximation may be wrong).

    const auto& shape = params.shape;
    const auto& layout = *shape.layout;

    // ── Tier pre-classification (O(G) per side) ──────────────────────────
    s.active_tiers    = build_blur_tiers(shape.glyphs);
    s.crossfade_tiers = (shape.crossfade_layout && !shape.crossfade_glyphs.empty())
        ? build_blur_tiers(shape.crossfade_glyphs)
        : BlurTiers{};

    // All-active-glyphs index array for the shadow pass (no tiering).
    s.all_active_glyphs.resize(shape.glyphs.size());
    for (std::uint32_t gi = 0; gi < shape.glyphs.size(); ++gi) {
        s.all_active_glyphs[gi] = gi;
    }

    // ── Per-layer per-tier renderer (captures s.* + params.shape for paint) ─
    //
    // Renders glyphs of `tier_glyphs` from `source_glyphs` (paired with
    // `source_placed`) into `target`.  When `override_color` is set, every
    // glyph is filled (and stroked) with that color — the shadow pass.
    // Otherwise per-glyph fill/stroke are honored with shape.paint fallback.
    //
    // TICKET-TEXT-CLEANUP-3: render_offset_x/y replaced by TextRasterSpace.
    // Use to_surface_matrix() to combine glyph matrix with raster space.
    auto render_tier_to_image = [&](
        BLImage& target,
        std::optional<Color> override_color,
        int blur_radius,
        const std::vector<GlyphInstanceState>& source_glyphs,
        const PlacedGlyphRun& source_placed,
        const std::vector<std::uint32_t>& tier_glyphs,
        const TextRasterSpace& raster_space
    ) -> std::size_t {
        std::size_t drawn = 0;
        BLContext ctx(target);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.setCompOp(BL_COMP_OP_SRC_OVER);

        for (std::uint32_t gi : tier_glyphs) {
            const auto& g = source_glyphs[gi];

            Color eff_stroke = g.stroke;
            f32    eff_stroke_w = g.stroke_width;
            if (!override_color.has_value()
                && g.stroke.a <= 0.0f
                && shape.paint.stroke_enabled) {
                eff_stroke = shape.paint.stroke_color;
                eff_stroke_w = shape.paint.stroke_width;
            }

            const float op_alpha = override_color.has_value()
                ? static_cast<float>(override_color->a) * g.opacity
                : g.opacity;
            if (op_alpha <= 0.0f && eff_stroke.a <= 0.0f) continue;

            BLMatrix2D glyph_mat = to_surface_matrix(
                build_glyph_matrix(g), raster_space);

            ctx.save();
            ctx.setTransform(glyph_mat);

#ifdef CHRONON3D_ENABLE_TEXT
            const std::size_t span_idx = s.per_glyph_span_idx[gi];
            const FontFaceHandle& span_handle = s.span_handles[span_idx];
            if (eff_stroke.a > 0.0f && eff_stroke_w > 0.0f
                && span_handle.ft_face != nullptr
                && span_handle.outlines != nullptr) {
                BLPath path = span_handle.outlines->build_path(
                    span_handle.ft_face,
                    source_placed.glyphs[gi].glyph_id, 0.0f, 0.0f);
                if (!path.empty()) {
                    Color out_stroke = eff_stroke;
                    if (override_color.has_value()) {
                        out_stroke = *override_color;
                        out_stroke.a = eff_stroke.a * g.opacity;
                    }
                    ctx.setStrokeStyle(to_bl_rgba(out_stroke));
                    ctx.setStrokeWidth(static_cast<double>(eff_stroke_w));
                    ctx.strokePath(path);
                }
            }
#endif

            if (op_alpha > 0.0f) {
                ctx.setGlobalAlpha(static_cast<double>(op_alpha));

                Color final_fill = g.fill;
                if (!override_color.has_value()
                    && (g.fill.r >= 0.999f && g.fill.g >= 0.999f
                        && g.fill.b >= 0.999f && g.fill.a >= 0.999f)
                    && (shape.paint.fill.r != 1.0f || shape.paint.fill.g != 1.0f
                        || shape.paint.fill.b != 1.0f || shape.paint.fill.a != 1.0f)) {
                    // "Default white" sentinel: TextPaint fill wins.
                    final_fill = shape.paint.fill;
                }
                if (override_color.has_value()) {
                    final_fill = *override_color;
                }
                ctx.setFillStyle(to_bl_rgba(final_fill));

                auto sgr = SingleGlyphRun::from(source_placed.glyphs[gi]);
                const BLFont& span_font = s.span_fonts[s.per_glyph_span_idx[gi]];
                ctx.fillGlyphRun(BLPoint(0.0, 0.0), span_font, sgr.bl_run);
            }

            ctx.restore();
            ++drawn;
        }
        ctx.end();

        if (drawn > 0 && blur_radius > 0) {
            apply_separable_box_blur(target, blur_radius, s);
        }
        return drawn;
    };

    // ── Stage 4 — Shadow stack (under main fill) ──────────────────────────
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled || shadow.opacity <= 0.0f) continue;

        BLImage shadow_img = acquire_surface(
            s, /* shape has no crossfade-side shadows */ s.img_w, s.img_h);
        if (shadow_img.empty()) shadow_img = BLImage(s.img_w, s.img_h, BL_FORMAT_PRGB32);

        const Color shadow_color = {
            shadow.color.r, shadow.color.g, shadow.color.b,
            shadow.color.a * shadow.opacity
        };
        const TextRasterSpace shadow_space{1, s.offset_x, s.offset_y};
        const std::size_t sh_drawn = render_tier_to_image(
            shadow_img, shadow_color,
            detail::bucket_radius_for_tier(shadow.blur),
            shape.glyphs, layout.placed, s.all_active_glyphs,
            shadow_space);
        if (sh_drawn == 0) {
            release_surface(s, std::move(shadow_img));
            continue;
        }

        Mat4 shadow_model = params.model_matrix;
        shadow_model = glm::translate(
            shadow_model,
            Vec3(s.offset_x + shadow.offset.x, s.offset_y + shadow.offset.y, 0.0f));

        chronon3d::blend2d_bridge::composite_bl_image_transformed(
            params.fb, shadow_img, shadow_model,
            params.opacity, BlendMode::Normal);
        release_surface(s, std::move(shadow_img));
    }

    // ── Stage 5 — Main run: tiered per-glyph blur compositing ────────────
    s.img = acquire_surface(s, s.ss_img_w, s.ss_img_h);
    if (s.img.empty()) s.img = BLImage(s.ss_img_w, s.ss_img_h, BL_FORMAT_PRGB32);
    {
        BLContext ctx(s.img);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.end();
    }

    s.glyphs_drawn = 0;
    for (std::size_t tier = 0; tier < kNumBlurTiers; ++tier) {
        if (s.active_tiers[tier].empty()) continue;
        BLImage tier_img = acquire_surface(s, s.ss_img_w, s.ss_img_h);
        if (tier_img.empty()) tier_img = BLImage(s.ss_img_w, s.ss_img_h, BL_FORMAT_PRGB32);
        const std::size_t drawn = render_tier_to_image(
            tier_img, std::nullopt, kBlurTierRadii[tier],
            shape.glyphs, layout.placed, s.active_tiers[tier],
            s.raster_space);
        if (drawn == 0) {
            release_surface(s, std::move(tier_img));
            continue;
        }
        s.glyphs_drawn += drawn;

        {
            BLContext ctx(s.img);
            ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            ctx.blitImage(BLPoint(0, 0), tier_img);
            ctx.end();
        }
        release_surface(s, std::move(tier_img));
    }

    // ── Stage 6 — Crossfade side (PR 11) ─────────────────────────────────
    if (shape.crossfade_layout && !shape.crossfade_glyphs.empty()) {
        const f32 cf_fade = std::clamp(
            1.0f - shape.crossfade_mix, 0.0f, 1.0f);
        for (std::size_t tier = 0; tier < kNumBlurTiers; ++tier) {
            if (s.crossfade_tiers[tier].empty()) continue;
            BLImage tier_img = acquire_surface(s, s.ss_img_w, s.ss_img_h);
            if (tier_img.empty()) tier_img = BLImage(s.ss_img_w, s.ss_img_h, BL_FORMAT_PRGB32);
            const std::size_t drawn = render_tier_to_image(
                tier_img, std::nullopt, kBlurTierRadii[tier],
                shape.crossfade_glyphs, shape.crossfade_layout->placed,
                s.crossfade_tiers[tier],
                s.raster_space);
            if (drawn == 0) {
                release_surface(s, std::move(tier_img));
                continue;
            }
            s.glyphs_drawn += drawn;

            {
                BLContext ctx(s.img);
                ctx.setCompOp(BL_COMP_OP_SRC_OVER);
                ctx.setGlobalAlpha(static_cast<double>(cf_fade));
                ctx.blitImage(BLPoint(0, 0), tier_img);
                ctx.end();
            }
            release_surface(s, std::move(tier_img));
        }
    }

    if (s.glyphs_drawn == 0) {
        // Release back to pool; explicit Outcome{0}.
        release_surface(s, std::move(s.img));
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }

    // ── Stage 7 (raster tail) — Downsample supersampled image (FASE 3b) ──
    if (s.raster_space.scale > 1) {
        BLImage ds_img = acquire_surface(s, s.img_w, s.img_h);
        if (ds_img.empty()) ds_img = BLImage(s.img_w, s.img_h, BL_FORMAT_PRGB32);

        downsample_supersampled(ds_img, s.img, s.raster_space.scale, s);
        release_surface(s, std::move(s.img));  // free supersampled
        s.img = std::move(ds_img);
    }

    return graph::RenderOpResult(graph::RenderOpOutcome{s.glyphs_drawn});
}

} // namespace chronon3d::renderer::text_run_stages
