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
// Mutates: s.img, s.glyphs_drawn, s.active_tiers, s.dissolve_tiers,
//          s.all_active_glyphs (built here).

#include "text_run_stages.hpp"
#include <chronon3d/backends/text/text_render_resources.hpp>
#include "../../../blend2d/blend2d_bridge.hpp"
#include "../../../blend2d/blend2d_paint.hpp"  // canonical to_bl_rgba + build_bl_gradient
#include "../../../rasterizers/path/path_stroke.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
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
#include <cmath>
#include <optional>

// Forward-declare build_glyph_matrix in its canonical namespace so the
// lambda (declared in text_run_stages) can call it via unqualified lookup.
namespace chronon3d::renderer {
[[nodiscard]] BLMatrix2D build_glyph_matrix(const GlyphInstanceState& g);
}  // namespace chronon3d::renderer

namespace chronon3d::renderer::text_run_stages {

namespace {

using GlyphContour = std::pair<std::vector<Vec2>, bool>;

[[nodiscard]] std::vector<GlyphContour> flatten_glyph_contours(const BLPath& path) {
    const auto view = path.view();
    std::vector<GlyphContour> contours;
    std::vector<Vec2> points;
    bool closed = false;
    const auto flush = [&] {
        if (points.size() >= 2) contours.emplace_back(std::move(points), closed);
        points.clear();
        closed = false;
    };
    const auto point = [&](std::size_t index) {
        return Vec2{static_cast<float>(view.vertexData[index].x),
                    static_cast<float>(view.vertexData[index].y)};
    };
    const auto append_quad = [&](Vec2 from, Vec2 control, Vec2 to) {
        constexpr int steps = 12;
        for (int step = 1; step <= steps; ++step) {
            const float t = static_cast<float>(step) / steps;
            const float u = 1.0f - t;
            points.push_back(u * u * from + 2.0f * u * t * control + t * t * to);
        }
    };
    const auto append_cubic = [&](Vec2 from, Vec2 c1, Vec2 c2, Vec2 to) {
        constexpr int steps = 16;
        for (int step = 1; step <= steps; ++step) {
            const float t = static_cast<float>(step) / steps;
            const float u = 1.0f - t;
            points.push_back(u * u * u * from + 3.0f * u * u * t * c1 +
                             3.0f * u * t * t * c2 + t * t * t * to);
        }
    };

    for (std::size_t i = 0; i < view.size; ++i) {
        const auto command = view.commandData[i];
        if (command == BL_PATH_CMD_MOVE) {
            flush();
            points.push_back(point(i));
        } else if (command == BL_PATH_CMD_ON) {
            if (points.empty()) points.push_back(point(i));
            else points.push_back(point(i));
        } else if (command == BL_PATH_CMD_QUAD && i + 1 < view.size &&
                   view.commandData[i + 1] == BL_PATH_CMD_ON && !points.empty()) {
            append_quad(points.back(), point(i), point(i + 1));
            ++i;
        } else if (command == BL_PATH_CMD_CUBIC && i + 2 < view.size &&
                   view.commandData[i + 1] == BL_PATH_CMD_CUBIC &&
                   view.commandData[i + 2] == BL_PATH_CMD_ON && !points.empty()) {
            append_cubic(points.back(), point(i), point(i + 1), point(i + 2));
            i += 2;
        } else if (command == BL_PATH_CMD_CLOSE) {
            closed = true;
            flush();
        }
    }
    flush();
    return contours;
}

void stroke_glyph_outline(BLContext& context, const BLPath& path,
                          const std::vector<float>& dash_array, float dash_offset) {
    if (dash_array.empty()) {
        context.strokePath(path);
        return;
    }
    // Blend2D's context dash setters are exposed by the bundled backend but
    // do not affect strokePath on this build. Split the flattened glyph
    // contours with Chronon's canonical dash sampler, then stroke each run.
    for (const auto& [contour, closed] : flatten_glyph_contours(path)) {
        for (const auto& dash : renderer::dash_polyline_points(
                 contour, closed, dash_array, dash_offset)) {
            if (dash.size() < 2) continue;
            BLPath dashed_path;
            dashed_path.moveTo(dash.front().x, dash.front().y);
            for (std::size_t i = 1; i < dash.size(); ++i)
                dashed_path.lineTo(dash[i].x, dash[i].y);
            context.strokePath(dashed_path);
        }
    }
}

// PR3 — canonical to_bl_rgba pulled into text_run_stages namespace so
// unqualified calls from inside this TU's pipeline find it.  Same pattern
// as the original monolith (text_run_processor.cpp line 39).
using chronon3d::blend2d_bridge::paint::to_bl_rgba;

// ── SingleGlyphRun — fast per-glyph BL fill (PHASE 1.0 legacy path retained) ─
struct SingleGlyphRun {
    std::uint32_t    glyph_id{0};
    BLGlyphPlacement placement{};
    BLGlyphRun       bl_run{};

    explicit SingleGlyphRun(const PlacedGlyph& pg) noexcept {
        glyph_id = pg.glyph_id;
        placement.placement.reset(0.0, 0.0);
        placement.advance.reset(0.0, 0.0);
        bl_run.glyphData = &glyph_id;
        bl_run.glyphAdvance = static_cast<int8_t>(sizeof(glyph_id));
        bl_run.placementData = &placement;
        bl_run.placementAdvance = static_cast<int8_t>(sizeof(placement));
        bl_run.placementType = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;
        bl_run.size = 1;
    }

    SingleGlyphRun(const SingleGlyphRun&) = delete;
    SingleGlyphRun& operator=(const SingleGlyphRun&) = delete;
    SingleGlyphRun(SingleGlyphRun&&) = delete;
    SingleGlyphRun& operator=(SingleGlyphRun&&) = delete;
};

// A text shadow is the blur of the union of the glyph masks, not the
// source-over accumulation of one translucent shadow per glyph.  The latter
// saturates in the overlaps between adjacent glyphs and produces the opaque
// rectangular band that used to appear behind typewriter overlays.
[[nodiscard]] bool has_binary_glyph_opacity(
    const std::vector<GlyphInstanceState>& glyphs) noexcept {
    return std::all_of(glyphs.begin(), glyphs.end(), [](const auto& glyph) {
        return glyph.opacity <= 1e-4f || glyph.opacity >= 1.0f - 1e-4f;
    });
}

// The shadow colour/opacity is applied after the union mask has been blurred.
// Scaling all four bytes preserves PRGB32 premultiplication and is independent
// of the platform byte order used by Blend2D.
void scale_prgb32_alpha(BLImage& image, float factor) noexcept {
    factor = std::clamp(factor, 0.0f, 1.0f);
    BLImageData data{};
    if (image.empty() || image.getData(&data) != BL_SUCCESS ||
        data.format != BL_FORMAT_PRGB32 || !data.pixelData) {
        return;
    }
    for (int y = 0; y < data.size.h; ++y) {
        auto* row = static_cast<std::uint8_t*>(data.pixelData) +
            static_cast<std::size_t>(y) * data.stride;
        for (int x = 0; x < data.size.w * 4; ++x) {
            row[x] = static_cast<std::uint8_t>(std::clamp(
                std::lround(static_cast<float>(row[x]) * factor), 0L, 255L));
        }
    }
}

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
    // TICKET-TEXT-TIMING-V1 — time glyph rasterization (the per-frame text
    // cost) so raster vs draw vs cache lookup are separable in telemetry.
    const auto raster_start = profiling::now();

    // TICKET-TEXT-CLEANUP-4: silent_success_empty short-circuit removed.
    // Off-canvas text is now always rasterized (bbox approximation may be wrong).

    const auto& shape = params.shape;
    const auto& layout = *shape.layout;

    // ── Tier pre-classification (O(G) per side) ──────────────────────────
    s.active_tiers    = build_blur_tiers(shape.glyphs);
    s.dissolve_tiers = (shape.dissolve_layout && !shape.dissolve_glyphs.empty())
        ? build_blur_tiers(shape.dissolve_glyphs)
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
        const TextRasterSpace& raster_space,
        const std::vector<std::size_t>& glyph_span_indices
    ) -> std::size_t {
        std::size_t drawn = 0;
        BLContext ctx(target);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.setCompOp(BL_COMP_OP_SRC_OVER);

        BLImage fill_pattern_image;
        BLImage stroke_pattern_image;
        const auto make_pattern_image = [](const std::shared_ptr<const graphics::PatternTile>& tile,
                                           BLImage& image) {
            if (!tile || !tile->valid()) return;
            image = BLImage(static_cast<int>(tile->width), static_cast<int>(tile->height),
                            BL_FORMAT_PRGB32);
            BLImageData data{};
            if (image.makeMutable(&data) != BL_SUCCESS || !data.pixelData) {
                image.reset();
                return;
            }
            const auto* source = tile->rgba.data();
            auto* destination = static_cast<std::uint8_t*>(data.pixelData);
            for (std::uint32_t y = 0; y < tile->height; ++y) {
                auto* row = destination + static_cast<std::size_t>(y) * data.stride;
                for (std::uint32_t x = 0; x < tile->width; ++x) {
                    const auto index = (static_cast<std::size_t>(y) * tile->width + x) * 4;
                    const std::uint32_t alpha = source[index + 3];
                    row[x * 4 + 0] = static_cast<std::uint8_t>(source[index + 2] * alpha / 255);
                    row[x * 4 + 1] = static_cast<std::uint8_t>(source[index + 1] * alpha / 255);
                    row[x * 4 + 2] = static_cast<std::uint8_t>(source[index + 0] * alpha / 255);
                    row[x * 4 + 3] = static_cast<std::uint8_t>(alpha);
                }
            }
        };
        make_pattern_image(shape.paint.fill_pattern, fill_pattern_image);
        make_pattern_image(shape.paint.stroke_pattern, stroke_pattern_image);

        for (std::uint32_t gi : tier_glyphs) {
            // Animated glyph state and immutable placed layout can briefly
            // differ while a text run is refreshed. Skip the orphan state
            // instead of indexing the shorter layout (or span map).
            if (gi >= source_glyphs.size() ||
                gi >= source_placed.glyphs.size() ||
                gi >= glyph_span_indices.size()) {
                continue;
            }
            const auto& g = source_glyphs[gi];
            const std::size_t span_idx = glyph_span_indices[gi];
            if (span_idx >= s.span_fonts.size() ||
                span_idx >= s.span_handles.size()) {
                continue;
            }

            Color eff_stroke = g.stroke;
            f32    eff_stroke_w = g.stroke_width;
            if (!override_color.has_value()
                && g.stroke.a <= 0.0f
                && shape.paint.stroke_enabled) {
                eff_stroke = shape.paint.stroke_color;
                eff_stroke_w = shape.paint.stroke_width;
            }
            // Stroke (like fill) must be gated by the per-glyph selector
            // weight encoded in g.opacity.  Without this a typewriter
            // glyph with opacity 0 still paints its stroke as a solid
            // navy rectangle that "pops" during the reveal.  The
            // shadow path uses override_color and is gated via op_alpha,
            // but the main path left stroke ungated.
            const float eff_stroke_alpha = eff_stroke.a * g.opacity;
            const float op_alpha = override_color.has_value()
                ? static_cast<float>(override_color->a) * g.opacity
                : g.opacity;
            if (op_alpha <= 0.0f && eff_stroke_alpha <= 0.0f) continue;

            BLMatrix2D glyph_mat = to_surface_matrix(
                build_glyph_matrix(g), raster_space);

            ctx.save();
            ctx.setTransform(glyph_mat);

#ifdef CHRONON3D_ENABLE_TEXT
            // Per-glyph animated background (drawn behind stroke/fill).
            if (g.background.a > 0.0f && op_alpha > 0.0f) {
                const BLFont& span_font = s.span_fonts[glyph_span_indices[gi]];
                const BLFontMetrics fm = span_font.metrics();
                const double bg_y0 = -fm.ascent;
                const double bg_h  = fm.ascent + fm.descent;
                const double bg_x0 = source_placed.glyphs[gi].bbox_x0;
                const double bg_w  = source_placed.glyphs[gi].bbox_x1 - bg_x0;
                const double pad_x = span_font.size() * g.background_padding_em;
                const double pad_y = span_font.size() * g.background_padding_em;
                const double radius = span_font.size() * g.background_radius_em;
                ctx.setFillStyle(to_bl_rgba(g.background));
                ctx.setGlobalAlpha(static_cast<double>(g.background.a * op_alpha));
                if (radius > 0.0) {
                    ctx.fillRoundRect(BLRect(bg_x0 - pad_x, bg_y0 - pad_y,
                                             bg_w + 2.0 * pad_x,
                                             bg_h + 2.0 * pad_y), radius);
                } else {
                    ctx.fillRect(BLRect(bg_x0 - pad_x, bg_y0 - pad_y,
                                        bg_w + 2.0 * pad_x,
                                        bg_h + 2.0 * pad_y));
                }
                ctx.setGlobalAlpha(1.0);
            }

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
                    // GlyphOutlineBuilder emits outlines via FT_Load_Glyph
                    // after FT_Set_Pixel_Sizes(font_size).  The outline is
                    // therefore already in device pixels at the requested
                    // size (26.6 fixed-point scaled), not in font design
                    // units.  BLContext::setStrokeWidth is in the same
                    // pre-transform user space (pixel space), so the
                    // authored width is already in the correct units.
                    // The previous units_per_EM/font_size scaling assumed
                    // design units and inflated a 3.5 px stroke to ~54 px
                    // (1000/64) — the "allagato" navy blob.  Keep the
                    // width in device pixels; scale only by the raster
                    // supersampling factor so the downsampled result keeps
                    // the authored width.
                    double stroke_width_user = static_cast<double>(eff_stroke_w);
                    if (raster_space.scale > 1) {
                        stroke_width_user *=
                            static_cast<double>(raster_space.scale);
                    }
                    std::optional<BLGradient> text_stroke_gradient;
                    if (!override_color.has_value() && shape.paint.stroke_style &&
                        shape.paint.stroke_style->enabled &&
                        !shape.paint.stroke_style->gradient.color_stops.empty() &&
                        layout.bounds.x > 0.0f && layout.line_height > 0.0f) {
                        auto glyph_stroke = *shape.paint.stroke_style;
                        auto& gradient = glyph_stroke.gradient;
                        const Vec2 delta{source_placed.glyphs[gi].x / layout.bounds.x,
                                         source_placed.glyphs[gi].y / layout.line_height};
                        gradient.start -= delta;
                        gradient.end -= delta;
                        gradient.center -= delta;
                        const float ascent = !s.span_fonts.empty()
                            ? static_cast<float>(s.span_fonts.front().metrics().ascent)
                            : layout.font_size * 0.8f;
                        text_stroke_gradient = blend2d_bridge::paint::build_bl_gradient(
                            glyph_stroke, 0.0f, -ascent,
                            layout.bounds.x, layout.line_height);
                    }
                    if (text_stroke_gradient) {
                        ctx.setGlobalAlpha(static_cast<double>(g.opacity * shape.paint.stroke_color.a));
                        ctx.setStrokeStyle(*text_stroke_gradient);
                    } else if (!override_color && !stroke_pattern_image.empty()) {
                        const auto& tile = *shape.paint.stroke_pattern;
                        const auto& m = tile.transform;
                        BLMatrix2D pattern_transform(m[0], m[1], m[2], m[3],
                            m[4] - source_placed.glyphs[gi].x,
                            m[5] - source_placed.glyphs[gi].y);
                        BLPattern pattern(stroke_pattern_image, BL_EXTEND_MODE_REPEAT,
                                          pattern_transform);
                        ctx.setGlobalAlpha(static_cast<double>(g.opacity));
                        ctx.setStrokeStyle(pattern);
                    } else {
                        ctx.setStrokeStyle(to_bl_rgba(out_stroke));
                    }
                    ctx.setStrokeWidth(stroke_width_user);
                    ctx.setStrokeCaps(shape.paint.stroke_cap == LineCap::Round
                        ? BL_STROKE_CAP_ROUND
                        : shape.paint.stroke_cap == LineCap::Square
                            ? BL_STROKE_CAP_SQUARE : BL_STROKE_CAP_BUTT);
                    ctx.setStrokeJoin(shape.paint.stroke_join == LineJoin::Round
                        ? BL_STROKE_JOIN_ROUND
                        : shape.paint.stroke_join == LineJoin::Bevel
                            ? BL_STROKE_JOIN_BEVEL : BL_STROKE_JOIN_MITER_CLIP);
                    ctx.setStrokeMiterLimit(shape.paint.stroke_miter_limit);
                    stroke_glyph_outline(ctx, path, shape.paint.stroke_dash_array,
                                         shape.paint.stroke_dash_offset);
                    if (text_stroke_gradient) ctx.setGlobalAlpha(1.0);
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
                std::optional<BLGradient> text_fill_gradient;
                if (!override_color.has_value() && shape.paint.fill_style &&
                    shape.paint.fill_style->enabled &&
                    !shape.paint.fill_style->gradient.color_stops.empty() &&
                    layout.bounds.x > 0.0f && layout.line_height > 0.0f) {
                    // Glyphs are drawn in their own translated user space.
                    // Shift normalized gradient geometry by the glyph origin
                    // so adjacent glyphs sample one continuous text-run ramp.
                    auto glyph_fill = *shape.paint.fill_style;
                    auto& gradient = glyph_fill.gradient;
                    const Vec2 delta{source_placed.glyphs[gi].x / layout.bounds.x,
                                     source_placed.glyphs[gi].y / layout.line_height};
                    gradient.start -= delta;
                    gradient.end -= delta;
                    gradient.center -= delta;
                    const float ascent = !s.span_fonts.empty()
                        ? static_cast<float>(s.span_fonts.front().metrics().ascent)
                        : layout.font_size * 0.8f;
                    text_fill_gradient = blend2d_bridge::paint::build_bl_gradient(
                        glyph_fill, 0.0f, -ascent, layout.bounds.x, layout.line_height);
                }
                if (!override_color && !fill_pattern_image.empty()) {
                    const auto& tile = *shape.paint.fill_pattern;
                    const auto& m = tile.transform;
                    BLMatrix2D pattern_transform(m[0], m[1], m[2], m[3],
                        m[4] - source_placed.glyphs[gi].x,
                        m[5] - source_placed.glyphs[gi].y);
                    BLPattern pattern(fill_pattern_image, BL_EXTEND_MODE_REPEAT,
                                      pattern_transform);
                    ctx.setGlobalAlpha(static_cast<double>(op_alpha * shape.paint.fill.a));
                    ctx.setFillStyle(pattern);
                } else if (text_fill_gradient) ctx.setFillStyle(*text_fill_gradient);
                else ctx.setFillStyle(to_bl_rgba(final_fill));

                if (gi >= glyph_span_indices.size() ||
                    glyph_span_indices[gi] >= s.span_fonts.size()) {
                    return 0;
                }
                SingleGlyphRun sgr{source_placed.glyphs[gi]};
                const BLFont& span_font = s.span_fonts[glyph_span_indices[gi]];
                const BLResult result = ctx.fillGlyphRun(
                    BLPoint(0.0, 0.0), span_font, sgr.bl_run);
                if (result != BL_SUCCESS) {
                    return 0;
                }
                if (!override_color.has_value()) {
                    for (const auto& overlay :
                         source_placed.glyphs[gi].fill_paint_overlays) {
                        SingleGlyphRun overlay_run{PlacedGlyph{
                            .glyph_id = overlay.glyph_id}};
                        ctx.save();
                        ctx.translate(overlay.x_offset, overlay.y_offset);
                        ctx.setCompOp(BL_COMP_OP_SRC_ATOP);
                        ctx.setFillStyle(to_bl_rgba(overlay.color));
                        ctx.setGlobalAlpha(static_cast<double>(op_alpha));
                        (void)ctx.fillGlyphRun(BLPoint(0.0, 0.0), span_font,
                                               overlay_run.bl_run);
                        ctx.setCompOp(BL_COMP_OP_SRC_OVER);
                        ctx.setGlobalAlpha(1.0);
                        ctx.restore();
                    }
                }
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

        const f32 incoming_alpha = (shape.dissolve_layout && !shape.dissolve_glyphs.empty())
            ? shape.dissolve_mix : 1.0f;
        const bool union_shadow_mask = has_binary_glyph_opacity(shape.glyphs);
        const Color shadow_color = {
            shadow.color.r, shadow.color.g, shadow.color.b,
            union_shadow_mask ? 1.0f
                              : shadow.color.a * shadow.opacity * incoming_alpha
        };
        const TextRasterSpace shadow_space{1, s.offset_x, s.offset_y};
        const std::size_t sh_drawn = render_tier_to_image(
            shadow_img, shadow_color,
            detail::bucket_radius_for_tier(shadow.blur),
            shape.glyphs, layout.placed, s.all_active_glyphs,
            shadow_space, s.per_glyph_span_idx);
        if (sh_drawn == 0) {
            release_surface(s, std::move(shadow_img));
            continue;
        }
        if (union_shadow_mask) {
            scale_prgb32_alpha(
                shadow_img, shadow.color.a * shadow.opacity * incoming_alpha);
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
            s.raster_space, s.per_glyph_span_idx);
        if (drawn == 0) {
            release_surface(s, std::move(tier_img));
            continue;
        }
        s.glyphs_drawn += drawn;

        {
            BLContext ctx(s.img);
            ctx.setCompOp(BL_COMP_OP_SRC_OVER);
            const f32 incoming_alpha = (shape.dissolve_layout && !shape.dissolve_glyphs.empty())
                ? shape.dissolve_mix : 1.0f;
            ctx.setGlobalAlpha(static_cast<double>(incoming_alpha));
            ctx.blitImage(BLPoint(0, 0), tier_img);
            ctx.end();
        }
        release_surface(s, std::move(tier_img));
    }

    // ── Stage 6 — Dissolve outgoing side (PR 11) ──────────────────────────
    if (shape.dissolve_layout && !shape.dissolve_glyphs.empty()
        && !s.dissolve_per_glyph_span_idx.empty()) {
        const f32 cf_fade = std::clamp(
            1.0f - shape.dissolve_mix, 0.0f, 1.0f);

        // Outgoing shadows: same shadow settings applied symmetrically.
        const auto& cf_layout = *shape.dissolve_layout;
        std::vector<std::uint32_t> all_outgoing_glyphs(shape.dissolve_glyphs.size());
        for (std::uint32_t gi = 0; gi < shape.dissolve_glyphs.size(); ++gi) {
            all_outgoing_glyphs[gi] = gi;
        }
        for (const auto& shadow : shape.shadows) {
            if (!shadow.enabled || shadow.opacity <= 0.0f) continue;

            BLImage shadow_img = acquire_surface(s, s.img_w, s.img_h);
            if (shadow_img.empty()) shadow_img = BLImage(s.img_w, s.img_h, BL_FORMAT_PRGB32);

            const bool union_shadow_mask = has_binary_glyph_opacity(
                shape.dissolve_glyphs);
            const Color shadow_color = {
                shadow.color.r, shadow.color.g, shadow.color.b,
                union_shadow_mask ? 1.0f
                                  : shadow.color.a * shadow.opacity * cf_fade
            };
            const TextRasterSpace shadow_space{1, s.offset_x, s.offset_y};
            const std::size_t sh_drawn = render_tier_to_image(
                shadow_img, shadow_color,
                detail::bucket_radius_for_tier(shadow.blur),
                shape.dissolve_glyphs, cf_layout.placed, all_outgoing_glyphs,
                shadow_space, s.dissolve_per_glyph_span_idx);
            if (sh_drawn == 0) {
                release_surface(s, std::move(shadow_img));
                continue;
            }
            if (union_shadow_mask) {
                scale_prgb32_alpha(
                    shadow_img, shadow.color.a * shadow.opacity * cf_fade);
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

        for (std::size_t tier = 0; tier < kNumBlurTiers; ++tier) {
            if (s.dissolve_tiers[tier].empty()) continue;
            BLImage tier_img = acquire_surface(s, s.ss_img_w, s.ss_img_h);
            if (tier_img.empty()) tier_img = BLImage(s.ss_img_w, s.ss_img_h, BL_FORMAT_PRGB32);
            const std::size_t drawn = render_tier_to_image(
                tier_img, std::nullopt, kBlurTierRadii[tier],
                shape.dissolve_glyphs, shape.dissolve_layout->placed,
                s.dissolve_tiers[tier],
                s.raster_space, s.dissolve_per_glyph_span_idx);
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

    // Populate the renderer-owned canonical glyph atlas after the first
    // successful raster.  The Vulkan graph path can then reuse the exact
    // glyph bitmaps on the next frame/job without rasterizing text again.
    // Only cache the stable, single-span, non-effect path: animated glyph
    // paint, dissolve and shadows must remain on the software path until
    // they have an equivalent native representation.
    if (rctx.text_resources
        && s.raster_space.scale == 1
        && s.span_fonts.size() == 1
        && s.per_glyph_span_idx.size() == layout.placed.glyphs.size()
        && shape.dissolve_layout == nullptr
        && shape.paint.stroke_enabled == false) {
        const Color& fill = shape.paint.fill;
        const auto channel = [](float value) -> std::uint32_t {
            return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const std::uint32_t fill_rgba =
            (channel(fill.r) << 24u) | (channel(fill.g) << 16u)
            | (channel(fill.b) << 8u) | channel(fill.a);
        rctx.text_resources->store_glyph_atlas_from_placed_run(
            layout.font.font_path,
            s.img,
            layout.placed,
            s.span_fonts.front(),
            -s.offset_x,
            -s.offset_y,
            layout.font_size,
            fill_rgba);
    }

    // TICKET-TEXT-TIMING-V1 — accumulate only on the successful raster path
    // (the zero-glyph early return above skips attribution).
    if (rctx.counters) {
        rctx.counters->text_rasterization_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_ms(raster_start))),
            std::memory_order_relaxed);
    }

    return graph::RenderOpResult(graph::RenderOpOutcome{s.glyphs_drawn});
}

} // namespace chronon3d::renderer::text_run_stages
