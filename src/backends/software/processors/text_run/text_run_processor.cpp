#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>  // apply_text_material
#include <chronon3d/backends/text/text_render_resources.hpp>   // Fase 3 — TextRenderResources
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>  // WP-8 PR 8.0 — explicit-resolver plumbing
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <blend2d.h>
#include <blend2d/font.h>
#include <blend2d/path.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

#include "../../utils/blend2d_bridge.hpp"
#include "../../utils/blend2d_paint.hpp"  // PR3: canonical to_bl_rgba + build_bl_gradient

#ifdef CHRONON3D_ENABLE_TEXT
// R2: function now consumes the slim processor context POD.
#include <chronon3d/backends/software/software_processor_context.hpp>
#endif

namespace chronon3d::renderer {

// Forward-declared in detail:: header; brought into scope for this TU.
using detail::bucket_radius_for_tier;

// Defined in text_run_matrix.cpp — extracted from this file.
BLMatrix2D build_glyph_matrix(const GlyphInstanceState& g);

// PR3: adopt the canonical `to_bl_rgba` from `blend2d_bridge::paint`.
// Placed inside `namespace chronon3d::renderer` so unqualified callers
// from `draw_text_run` (and the lambdas it hosts) find it through
// normal lookup.  We do NOT place this inside the anonymous namespace
// below — anonymous namespaces do not propagate names to surrounding
// scope, so calls from code in `chronon3d::renderer` would not
// otherwise resolve.
using chronon3d::blend2d_bridge::paint::to_bl_rgba;

namespace {

// ── Helper: HbToBlGlyphRun for a single glyph (absolute positioning) ─

struct SingleGlyphRun {
    uint32_t glyph_id;
    BLGlyphPlacement placement;
    BLGlyphRun bl_run{};

    static SingleGlyphRun from(const PlacedGlyph& pg) {
        SingleGlyphRun result;
        result.glyph_id = pg.glyph_id;
        // Identity placement — we use ctx.transform() for positioning
        result.placement.placement.reset(0.0, 0.0);
        result.placement.advance.reset(0.0, 0.0);
        result.bl_run.glyphData = &result.glyph_id;
        result.bl_run.glyphAdvance = int8_t(sizeof(uint32_t));
        result.bl_run.placementData = &result.placement;
        result.bl_run.placementAdvance = int8_t(sizeof(BLGlyphPlacement));
        result.bl_run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
        result.bl_run.size = 1;
        return result;
    }
};



} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// draw_text_run
// ═══════════════════════════════════════════════════════════════════════════

graph::RenderOpResult draw_text_run(
    const SoftwareProcessorContext& rctx,
    TextRunDrawParams& params
) {
    // WP-8 PR 8.0 — function-scope resolver sourced from the renderer’s
    // own runtime, threaded into both caches used below (`get_face` for
    // the BLFontFace lookup and `TextRunPathBuilder::load` for the
    // FreeType stroke-path loading).  Replaces deep reads of
    // `runtime::typed_resolver_for_deep_code()` inside both caches.
    // `AssetResolver` is a value-member of `RenderRuntime`, so this
    // reference is stable for the renderer’s lifetime.
    // Fase 3 — font loading is handled by TextRenderResources.
    // The renderer receives a FontFaceHandle — no file I/O here.

    const auto& shape = params.shape;
    if (!shape.layout || shape.glyphs.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "draw_text_run: empty layout or empty glyph set"
        });
    }

    const auto& layout = *shape.layout;
    const std::string& font_path = layout.font.font_path;
    if (font_path.empty()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "draw_text_run: empty layout.font.font_path"
        });
    }

    CHRONON_ZONE_C("text_run_draw", trace_category::kText);

    // ── Safe Bbox Clipping ───────────────────────────────────────────
    // Pre-compute the world-space bbox of every glyph (2.5D-aware via
    // `compute_text_run_world_bbox`) and intersect with the framebuffer
    // bounds.  If the intersection is empty (text rotated far off-screen,
    // fully outside, etc.) we return false SILENTLY — no diagnostics, no
    // diagnostic insertion — so off-canvas text_run entries don't pollute
    // the per-frame log with shape-not-found errors.
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
            // world bbox does not intersect framebuffer at all — silent
            // success with zero items drawn (PR2: explicit outcome).
            return graph::RenderOpResult(graph::RenderOpOutcome{0});
        }
    }

    // ── Empty-framebuffer guard ────────────────────────────────────────────
    // A 0×0 or zero-dim framebuffer would let the BL bridge loop forever
    // (or divide by zero when scaling).  Treat as InvalidInput (the
    // caller's fb was malformed).
    if (params.fb.width() == 0 || params.fb.height() == 0) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput,
            "draw_text_run: zero-dimension framebuffer"
        });
    }

    // ── Resolve font handle via TextRenderResources ──────────────────
    FontFaceHandle font_handle;
    if (rctx.text_resources) {
        font_handle = rctx.text_resources->resolve_handle(
            font_path, layout.font_size, *rctx.asset_resolver);
    }
    if (!font_handle.valid()) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure,
            "draw_text_run: failed to load font face for " + font_path
        });
    }

    BLFont font;
    font.createFromFace(*font_handle.bl_face, layout.font_size);

    // ── Compute glyph bbox (the run's local image-local extent) ─────
    // Conservative padding accounts for blur, stroke, and 2.5D shears
    // before we know how aggressive the model_matrix will be.
    //
    // PR 11 — CrossfadeLayouts: extend the bbox to ALSO cover the
    // outgoing glyph positions from `shape.crossfade_glyphs` (if the
    // gap is active via `shape.crossfade_layout`).  A BLImage with
    // a too-tight bbox would clip the outgoing glyphs at the
    // edges of the canvas.  Shadow padding stays on the active
    // side only (shadows are tied to the active document's
    // paint/material).
    float min_x = 1e10f, min_y = 1e10f;
    float max_x = -1e10f, max_y = -1e10f;

    // Per-glyph bbox accumulator.  Walks a glyph vector against its
    // placed run and widens the running local-space min/max.  Used
    // for both the active side and the crossfade side below.
    auto expand_per_glyph = [&](const std::vector<GlyphInstanceState>& glyphs,
                                const PlacedGlyphRun& placed) {
        for (const auto& g : glyphs) {
            const float gx = g.layout_position.x + g.position.x;
            const float gy = g.layout_position.y + g.position.y;
            const float pad = g.blur + g.stroke_width + 8.0f;
            min_x = std::min(min_x, gx - pad);
            min_y = std::min(min_y, gy - pad);
            max_x = std::max(max_x, gx + placed.total_width /
                static_cast<float>(std::max(size_t(1), placed.glyphs.size())) + pad);
            max_y = std::max(max_y, gy + placed.total_height + pad);
        }
    };

    expand_per_glyph(shape.glyphs, layout.placed);
    if (shape.crossfade_layout && !shape.crossfade_glyphs.empty()) {
        expand_per_glyph(shape.crossfade_glyphs, shape.crossfade_layout->placed);
    }

    // Shadow padding stays on the active side only — shadows track
    // the active paint, not the crossfade paint.  Iterate shape.glyphs
    // again to fold shadow-blur / shadow-offset into the bbox.
    for (const auto& g : shape.glyphs) {
        const float gx = g.layout_position.x + g.position.x;
        const float gy = g.layout_position.y + g.position.y;
        for (const auto& sh : shape.shadows) {
            if (!sh.enabled) continue;
            const float sh_pad = sh.blur + std::abs(sh.offset.x) + std::abs(sh.offset.y) + 8.0f;
            min_x = std::min(min_x, gx - sh_pad);
            max_x = std::max(max_x, gx + sh_pad);
            min_y = std::min(min_y, gy - sh_pad);
            max_y = std::max(max_y, gy + sh_pad);
        }
    }

    const float margin = 8.0f;
    const int img_w = std::max(1, static_cast<int>(std::ceil(max_x - min_x + margin * 2.0f)));
    const int img_h = std::max(1, static_cast<int>(std::ceil(max_y - min_y + margin * 2.0f)));
    const float offset_x = min_x - margin;
    const float offset_y = min_y - margin;

    // ── Per-glyph blur tiers ─────────────────────────────────────────
    // Glyphs are grouped into 5 tiers by their per-glyph blur value.
    // Each tier is rasterized onto its own surface, blurred individually,
    // and then composited SRC_OVER onto the final run image.  This means
    // a glyph with blur=16 does NOT smear across glyphs with blur=0.
    //
    // Tier thresholds and box-blur radii:
    //   tier 0: blur = 0     → radius 0  (no blur)
    //   tier 1: blur 1–4     → radius 2
    //   tier 2: blur 5–8     → radius 7
    //   tier 3: blur 9–16    → radius 13
    //   tier 4: blur > 16    → radius 20 (capped)
    static constexpr int kBlurTierRadii[5] = { 0, 2, 7, 13, 20 };
    static constexpr int kNumBlurTiers = 5;

    auto classify_blur_tier = [](float blur) -> int {
        if (blur <= 0.0f) return 0;
        if (blur <= 4.0f) return 1;
        if (blur <= 8.0f) return 2;
        if (blur <= 16.0f) return 3;
        return 4;
    };

    // Keep bucket_radius for shadow passes (shadows composite per-shadow
    // so they don't need tiering — each shadow has a uniform blur radius).
    auto bucket_radius = [](float r) -> int {
        if (r <= 0.0f)  return 0;
        if (r <= 4.0f)  return kBlurTierRadii[1];
        if (r <= 8.0f)  return kBlurTierRadii[2];
        if (r <= 16.0f) return kBlurTierRadii[3];
        return kBlurTierRadii[4];
    };

    // ── Inline separable box-blur (sliding window, O(w×h)) ──────────
    // Operates on BL_FORMAT_PRGB32 in-place.  Uses a sliding window so
    // the per-pixel cost is O(1) instead of O(radius).  Total cost is
    // roughly 2 × w × h independent of radius.
    auto apply_box_blur = [](BLImage& image, int radius) {
        if (radius <= 0) return;
        BLImageData data;
        if (image.getData(&data) != BL_SUCCESS) return;
        const int w = data.size.w;
        const int h = data.size.h;
        if (w <= 0 || h <= 0) return;
        const int stride = static_cast<int>(data.stride / sizeof(uint32_t));
        uint32_t* base = static_cast<uint32_t*>(data.pixelData);

        std::vector<uint32_t> tmp(static_cast<size_t>(w) * static_cast<size_t>(h));

        const int r = radius;
        auto unpack = [](uint32_t c, int& a, int& rr, int& g, int& b) {
            a = static_cast<int>((c >> 24) & 0xFF);
            rr = static_cast<int>((c >> 16) & 0xFF);
            g = static_cast<int>((c >>  8) & 0xFF);
            b = static_cast<int>( c        & 0xFF);
        };
        auto pack = [](int a, int rr, int g, int b) -> uint32_t {
            return (static_cast<uint32_t>(std::clamp(a, 0, 255)) << 24) |
                   (static_cast<uint32_t>(std::clamp(rr, 0, 255)) << 16) |
                   (static_cast<uint32_t>(std::clamp(g, 0, 255)) <<  8) |
                   (static_cast<uint32_t>(std::clamp(b, 0, 255)));
        };

        // Horizontal pass (sliding window): tmp[y,x] = mean of base[y, x-r..x+r].
        for (int y = 0; y < h; ++y) {
            const uint32_t* row = base + static_cast<size_t>(y) * stride;
            int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

            // Initial window: pixels [0, min(w-1, r)]
            const int init_right = std::min(w - 1, r);
            for (int k = 0; k <= init_right; ++k) {
                int pa, pr, pg, pb;
                unpack(row[k], pa, pr, pg, pb);
                sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                ++count;
            }
            tmp[static_cast<size_t>(y) * w] =
                pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);

            for (int x = 1; x < w; ++x) {
                // Subtract pixel leaving the window
                const int leave = x - r - 1;
                if (leave >= 0) {
                    int pa, pr, pg, pb;
                    unpack(row[leave], pa, pr, pg, pb);
                    sum_a -= pa; sum_r -= pr; sum_g -= pg; sum_b -= pb;
                    --count;
                }
                // Add pixel entering the window
                const int enter = x + r;
                if (enter < w) {
                    int pa, pr, pg, pb;
                    unpack(row[enter], pa, pr, pg, pb);
                    sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                    ++count;
                }
                tmp[static_cast<size_t>(y) * w + x] =
                    pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);
            }
        }

        // Vertical pass (sliding window): base[y,x] = mean of tmp[y-r..y+r, x].
        for (int x = 0; x < w; ++x) {
            int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

            // Initial window: rows [0, min(h-1, r)]
            const int init_bottom = std::min(h - 1, r);
            for (int k = 0; k <= init_bottom; ++k) {
                int pa, pr, pg, pb;
                unpack(tmp[static_cast<size_t>(k) * w + x], pa, pr, pg, pb);
                sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                ++count;
            }
            base[static_cast<size_t>(0) * stride + x] =
                pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);

            for (int y = 1; y < h; ++y) {
                // Subtract row leaving the window
                const int leave = y - r - 1;
                if (leave >= 0) {
                    int pa, pr, pg, pb;
                    unpack(tmp[static_cast<size_t>(leave) * w + x], pa, pr, pg, pb);
                    sum_a -= pa; sum_r -= pr; sum_g -= pg; sum_b -= pb;
                    --count;
                }
                // Add row entering the window
                const int enter = y + r;
                if (enter < h) {
                    int pa, pr, pg, pb;
                    unpack(tmp[static_cast<size_t>(enter) * w + x], pa, pr, pg, pb);
                    sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                    ++count;
                }
                base[static_cast<size_t>(y) * stride + x] =
                    pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);
            }
        }
    };

#ifdef CHRONON3D_ENABLE_TEXT
    // Fase 3 — FreeType face is pre-loaded in the FontFaceHandle.
    // No file I/O here — the handle already carries the FT_Face.
    const bool ft_loaded = (font_handle.ft_face != nullptr);
#else
    const bool ft_loaded = false;
#endif

    // ── Reusable per-layer renderer ──────────────────────────────────
    // Renders shape.glyphs into `target`.  When `override_color` is set,
    // every glyph is filled (and stroked) with that color — used for the
    // shadow pass.  When `override_color` is nullopt, glyph-level
    // `g.fill` / `g.stroke` are honoured, with `shape.paint.fill` as a
    // default for glyphs whose fill is the "no override" sentinel.
    //
    // `only_tier` (optional): when set, only glyphs whose `g.blur` maps
    // to this tier are drawn.  When nullopt, all glyphs are drawn
    // (existing shadow-pass behavior).
    auto draw_run_layer = [&](
        BLImage& target,
        std::optional<Color> override_color,
        int blur_radius,
        const std::vector<GlyphInstanceState>& source_glyphs,
        const PlacedGlyphRun& source_placed,
        std::optional<int> only_tier = std::nullopt
    ) -> size_t {
        size_t drawn = 0;
        BLContext ctx(target);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.setCompOp(BL_COMP_OP_SRC_OVER);

        for (size_t gi = 0; gi < source_glyphs.size(); ++gi) {
            const auto& g = source_glyphs[gi];

            // ── Per-glyph blur tier filter ──────────────────────────
            // When `only_tier` is set, skip glyphs whose blur value
            // maps to a different tier.  This enables the main-run
            // tiered compositing loop below.
            if (only_tier.has_value()
                && classify_blur_tier(g.blur) != *only_tier) {
                continue;
            }

            // Effective stroke: per-glyph if animator set it, else fall
            // back to shape.paint.stroke when no override is in effect.
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

            BLMatrix2D glyph_mat = build_glyph_matrix(g);
            glyph_mat.translate(-offset_x, -offset_y);

            ctx.save();
            ctx.setTransform(glyph_mat);

#ifdef CHRONON3D_ENABLE_TEXT
            // Stroke first (underneath fill).  Override color wins when
            // supplied; otherwise the effective stroke from above.
            if (eff_stroke.a > 0.0f && eff_stroke_w > 0.0f && ft_loaded) {
                BLPath path = font_handle.outlines->build_outline(
                    font_handle.ft_face,
                    layout.placed.glyphs[gi].glyph_id, 0.0f, 0.0f);
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
                ctx.fillGlyphRun(BLPoint(0.0, 0.0), font, sgr.bl_run);
            }

            ctx.restore();
            ++drawn;
        }
        ctx.end();

        if (drawn > 0 && blur_radius > 0) {
            apply_box_blur(target, blur_radius);
        }
        return drawn;
    };

    // ── Shadow stack (drawn UNDER the main fill) ─────────────────────
    // Each enabled shadow in `shape.shadows` produces a shadow BLImage
    // with the shadow's color, then composites with the shadow's offset
    // folded into the model matrix.  Multiple shadows compose via
    // over-blending (order = vector order).
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled || shadow.opacity <= 0.0f) continue;

        BLImage shadow_img(img_w, img_h, BL_FORMAT_PRGB32);
        const Color shadow_color = {
            shadow.color.r, shadow.color.g, shadow.color.b,
            shadow.color.a * shadow.opacity
        };
        const size_t sh_drawn = draw_run_layer(
            shadow_img, shadow_color, detail::bucket_radius_for_tier(shadow.blur),
            shape.glyphs, layout.placed);
        if (sh_drawn == 0) continue;

        // Shadow translation: model + image-local offset + shadow offset.
        Mat4 shadow_model = params.model_matrix;
        shadow_model = glm::translate(
            shadow_model,
            Vec3(offset_x + shadow.offset.x, offset_y + shadow.offset.y, 0.0f));

        chronon3d::blend2d_bridge::composite_bl_image_transformed(
            params.fb, shadow_img, shadow_model,
            params.opacity, BlendMode::Normal);
    }

    // ── Main run: tiered per-glyph blur compositing ──────────────────
    //
    // Instead of drawing all glyphs to one image and blurring with the
    // single max_blur (which smears blur-0 glyphs), we group glyphs into
    // 5 blur tiers, rasterize each tier onto its own surface, apply the
    // tier's box-blur radius, then composite SRC_OVER onto a final image.
    //
    // This means glyphs with blur=0 stay sharp while adjacent blur=16
    // glyphs are fully blurred — the core motivation for per-glyph blur.
    BLImage img(img_w, img_h, BL_FORMAT_PRGB32);
    // Clear the final composite image to transparent.
    {
        BLContext ctx(img);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.end();
    }

    size_t glyphs_drawn = 0;
    for (int tier = 0; tier < kNumBlurTiers; ++tier) {
        BLImage tier_img(img_w, img_h, BL_FORMAT_PRGB32);
        const size_t drawn = draw_run_layer(
            tier_img, std::nullopt, kBlurTierRadii[tier],
            shape.glyphs, layout.placed, tier);
        if (drawn == 0) continue;
        glyphs_drawn += drawn;

        // Composite tier_img onto the final image SRC_OVER.
        {
            BLContext ctx(img);
            ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            ctx.blitImage(BLPoint(0, 0), tier_img);
            ctx.end();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // PR 11 — CrossfadeLayouts: render the outgoing side too
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // Walk the same 5 blur tiers on shape.crossfade_glyphs, then
    // composite each tier SRC_OVER onto the final `img` with
    // `setGlobalAlpha(1 - shape.crossfade_mix)` so the outgoing
    // contribution fades against the active side.  Matches
    // ActiveTextState::mix semantics: mix=0 ⇒ all-outgoing /
    // no-active, mix=1 ⇒ all-active / no-outgoing.
    //
    // No-op when the crossfade slot is empty (we're out of the gap):
    // `shape.crossfade_layout` is nullptr and
    // `shape.crossfade_glyphs` is empty by lifecycle contract on
    // apply_active_state_to_text_run_shape (PR 11).  Skipping that
    // frame keeps the settled-tail path a no-op.
    if (shape.crossfade_layout && !shape.crossfade_glyphs.empty()) {
        const f32 cf_fade = std::clamp(
            1.0f - shape.crossfade_mix, 0.0f, 1.0f);
        for (int tier = 0; tier < kNumBlurTiers; ++tier) {
            BLImage tier_img(img_w, img_h, BL_FORMAT_PRGB32);
            const size_t drawn = draw_run_layer(
                tier_img, std::nullopt, kBlurTierRadii[tier],
                shape.crossfade_glyphs, shape.crossfade_layout->placed, tier);
            if (drawn == 0) continue;
            glyphs_drawn += drawn;

            // Composite the crossfade tier onto the final image with
            // the (1-mix) global alpha fold.  Material application
            // (gradient / bevel) is performed once at the end over
            // both sides' contributions.
            {
                BLContext ctx(img);
                ctx.setCompOp(BL_COMP_OP_SRC_OVER);
                ctx.setGlobalAlpha(static_cast<double>(cf_fade));
                ctx.blitImage(BLPoint(0, 0), tier_img);
                ctx.end();
            }
        }
    }

    if (glyphs_drawn == 0) {
        // Nothing visible underneath (e.g. all glyphs had opacity=0 or
        // fell into empty tiers); PR2: explicit outcome with 0 items.
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }

    // ── Apply TextMaterial (gradient, bevel, etc.) ───────────────────
    if (shape.material.enabled) {
        apply_text_material(img, shape.material);
    }

    // ── Composite MAIN onto framebuffer — full model_matrix ──────────
    // Compose the user's model with the run-local translate so the
    // image content fills the framebuffer correctly under rotation /
    // scale / shear.  composite_bl_image_transformed has a fast path
    // for simple-translation matrices so the common case is still cheap.
    Mat4 full_model = params.model_matrix;
    full_model = glm::translate(full_model, Vec3(offset_x, offset_y, 0.0f));

    chronon3d::blend2d_bridge::composite_bl_image_transformed(
        params.fb, img, full_model, params.opacity, BlendMode::Normal);

    if (rctx.counters) {
        rctx.counters->text_glyphs_rasterized.fetch_add(
            static_cast<uint64_t>(glyphs_drawn),
            std::memory_order_relaxed
        );
    }

    // PR2: no diagnostic_mode here — caller (TextRunNode / multi_source_node) owns diagnostics via ctx.policy.diagnostics_enabled.
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs_drawn});
}

// NOTE: compute_text_run_world_bbox() has been moved to
// src/text/text_run_geometry.cpp (chronon3d_text_core) so the render graph
// can compute bounding boxes without linking the software backend.

// ═══════════════════════════════════════════════════════════════════════════
// create_text_run_processor — ShapeFactory entry
// ═══════════════════════════════════════════════════════════════════════════
//
// The software-registry back-mapping for `ShapeType::TextRun`.  Because
// the canonical renderer path for text runs goes through the
// `TextRunNode` in the render graph (driven by `draw_text_run()` above),
// the registry ShapeProcessor is a thin no-op marker: it carries the
// `is_text_run_shape=true` RenderNode flag and forwards through the
// graph-builder.  This keeps `layer.shape("shape.text_run", ...)` and
// `LayerBuilder::text_run(...)` semantically identical (both flow to a
// TextRunNode downstream).
//
// Why not just call `draw_text_run` directly here?  Because the
// `ShapeProcessor` interface receives a `RenderNode` per node, while
// `draw_text_run` operates on a `SoftwareRenderer` + `TextRunDrawParams`
// pair.  Bridging requires the compositor to invoke the renderer with
// the right z-layer / sample-time context — exactly what the graph
// provides via the TextRunNode's `execute()`.  Keeping the registry
// processor thin avoids duplicating that logic.

#ifdef CHRONON3D_ENABLE_TEXT
std::unique_ptr<ShapeProcessor> create_text_run_processor() {
    struct TextRunProcessor : ShapeProcessor {
        // No-op: the TextRunNode in the render graph handles rasterization.
        // This processor exists only as a registry marker.
        void draw(const SoftwareProcessorContext&, Framebuffer&, const RenderNode&,
                  const RenderState&, const Camera&, i32, i32) override {}

        raster::BBox compute_world_bbox(const Shape&, const Mat4&, f32) override {
            return {0, 0, 0, 0};
        }

        bool hit_test(const Shape&, Vec2, f32) override {
            return false;
        }
    };
    return std::make_unique<TextRunProcessor>();
}
#endif // CHRONON3D_ENABLE_TEXT

} // namespace chronon3d::renderer
