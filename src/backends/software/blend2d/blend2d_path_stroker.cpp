// ──────────────────────────────────────────────────────────────────────
// blend2d_path_stroker.cpp — PR5 BL2 stroke orchestrator
//
// Algorithm (mirrors PR4 fill):
//   1. A8 CoverageSurface sized (bbox.w × bbox.h), zero-filled.
//   2. BLContext on coverage, setCompOp(SRC_COPY), setStrokeStyle=BLRgba32(255),
//      setStrokeOptions(make_bl_stroke_options(stroke)).
//   3. For each prepared_contour.visible_subpath (already trim+ash):
//        - Build BLPath in screen-relative coords (offset by bbox origin).
//        - BLContext::strokePath(BLPath).
//      The dash is OFF in BLStrokeOptions because Chronon's
//      `prepare_stroke_contours` already split the polyline along the
//      dash pattern at the precompute stage.
//   4. Per-pixel shade + compositor blend:
//
//        base_color = (gradient sample at screen point) OR stroke.color
//        out.rgb   = base.rgb * stroke_color_modulation.rgb
//        out.a     = base.a * stroke_color_modulation.a * cov
//        row[x]    = compositor::blend(out, row[x], Normal)
//
// Edge case: stroke_color_modulation.a == 0 OR cov == 0 => no
// composite.  Coverage is rounded by BL2 to 8-bit A8 then divided by
// 255 in `coverage()`.
// ──────────────────────────────────────────────────────────────────────

#include "blend2d_path_stroker.hpp"

#include "../rasterizers/path/path_rasterizer.hpp"  // resolve_fill_color
#include "blend2d_stroke_options.hpp"
#include "coverage_surface.hpp"

#include <blend2d.h>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <glm/glm.hpp>

namespace chronon3d::renderer {

using chronon3d::blend2d_bridge::CoverageSurface;
using chronon3d::blend2d_bridge::stroke_options::apply_bl2_stroke_state;

bool rasterize_path_stroke_blend2d(
    chronon3d::Framebuffer& fb,
    const chronon3d::PathShape& path,
    const chronon3d::Mat4& /*model - strokes are pre-projected*/,
    const chronon3d::Color& stroke_color_modulation,
    const chronon3d::renderer::detail::PreparedStrokeContour* prepared,
    std::size_t prepared_count,
    const chronon3d::raster::BBox& bbox,
    const RenderState* /*state - mask/clip deferred to PR7*/)
{
    using namespace chronon3d;
    if (!path.stroke.enabled) return false;
    if (bbox.is_empty()) return false;
    if (!prepared || prepared_count == 0) return false;

    const int roi_w = bbox.x1 - bbox.x0;
    const int roi_h = bbox.y1 - bbox.y0;
    if (roi_w <= 0 || roi_h <= 0) return false;
    if (path.stroke.width <= 0.0f) return false;

    // ── 1. A8 coverage surface ──────────────────────────────────────
    CoverageSurface cov(roi_w, roi_h);
    if (cov.empty()) return false;
    cov.fill(0x00);

    // ── 2+3. BL2 stroke pass ────────────────────────────────────────
    {
        BLContext ctx(cov.image());
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        // The A8 coverage surface stores OUTLINE coverage only; colour
        // is resolved per-pixel on the Chronon side so the linear-float
        // pipeline (gradient sampling, alpha modulation) is preserved.
        ctx.setStrokeStyle(BLRgba32(255, 255, 255, 255));
        apply_bl2_stroke_state(ctx, path.stroke);

        const Vec2 origin{static_cast<f32>(bbox.x0), static_cast<f32>(bbox.y0)};
        for (std::size_t i = 0; i < prepared_count; ++i) {
            const auto& prep = prepared[i];
            for (const auto& sub : prep.visible_subpaths) {
                const auto& pts_sub = sub.points;
                if (pts_sub.size() < 2) continue;

                BLPath bl_path;
                bl_path.moveTo(
                    static_cast<double>(pts_sub.front().x - origin.x),
                    static_cast<double>(pts_sub.front().y - origin.y));
                for (std::size_t k = 1; k < pts_sub.size(); ++k) {
                    bl_path.lineTo(
                        static_cast<double>(pts_sub[k].x - origin.x),
                        static_cast<double>(pts_sub[k].y - origin.y));
                }
                ctx.strokePath(bl_path);
            }
        }
        ctx.end();
    }

    // ── 4. Resolve + Chronon shade + compositor blend ───────────────
    // When PathStroke.gradient is present we still resolve per-pixel
    // through resolve_fill_color() so the gradient geometry matches
    // the bbox-local LinearGradient/RadialGradient/ConicGradient
    // semantics exactly as the legacy loop did.  stroke.color is the
    // fallback (and dotted onto the gradient's stop colours via the
    // existing primitive modulation).
    const bool has_gradient = path.stroke.gradient.has_value();
    Fill gradient_fill{};
    if (has_gradient) {
        gradient_fill.enabled  = true;
        gradient_fill.type     = path.stroke.gradient->type;
        gradient_fill.gradient = *path.stroke.gradient;
    }

    const float mod_a = stroke_color_modulation.a;

    for (int ly = 0; ly < roi_h; ++ly) {
        const int sy = bbox.y0 + ly;
        if (sy < 0 || sy >= fb.height()) continue;
        Color* row = fb.pixels_row(sy);
        for (int lx = 0; lx < roi_w; ++lx) {
            const int sx = bbox.x0 + lx;
            if (sx < 0 || sx >= fb.width()) continue;
            const float cov_f = cov.coverage(lx, ly);
            if (cov_f <= 0.0f) continue;

            const Vec2 sp{
                static_cast<f32>(sx) + 0.5f,
                static_cast<f32>(sy) + 0.5f
            };
            Color base = has_gradient
                ? resolve_fill_color(gradient_fill, sp, bbox, /*opacity=*/1.0f)
                : path.stroke.color.to_linear();

            // Per-primitive modulation: RGB multiplies the base; A
            // multiplies the cov alpha and the base alpha.
            Color out{
                base.r * stroke_color_modulation.r,
                base.g * stroke_color_modulation.g,
                base.b * stroke_color_modulation.b,
                base.a * mod_a * cov_f
            };
            row[sx] = chronon3d::compositor::blend(out, row[sx], BlendMode::Normal);
        }
    }
    return true;
}

} // namespace chronon3d::renderer
