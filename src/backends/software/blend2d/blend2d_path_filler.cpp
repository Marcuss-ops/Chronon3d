// ──────────────────────────────────────────────────────────────────────
// blend2d_path_filler.cpp — PR4 BL2 fill orchestrator
//
// Algorithm (matches `coverage_surface=Blend2D` × `shading=Chronon`
// hybrid proposed in the PR4 design doc):
//
//   1. Construct an A8 CoverageSurface sized (bbox.w × bbox.h).
//   2. Build a BLPath in SCREEN space re-based to (bbox.x0, bbox.y0).
//   3. Open a BLContext, fillAll() with α=0, setCompOp(SRC_COPY),
//      fillStyle=BLRgba32(255), fillPath(BLPath, BL_FILL_RULE_EVEN_ODD).
//      The resulting α bytes are the analytic-AA fractional coverage.
//   4. For each ROI pixel whose coverage > 0:
//        a. Resolve the Chronon fill colour at the screen-space point
//           using the existing resolve_fill_color() helper (Solid/
//           Linear/Radial/Conic — same code path as the custom loop).
//        b. Modulate by coverage and opacity.
//        c. Composite into the Framebuffer with compositor::blend()
//           (BlendMode::Normal) — keeps the linear-float fb intact.
//      Out-of-coverage pixels are left untouched; stroke is the
//      legacy loop's responsibility.
// ──────────────────────────────────────────────────────────────────────

#include "blend2d_path_filler.hpp"

#include "../rasterizers/path/path_rasterizer.hpp"  // resolve_fill_color (sibling rasterizer header)
#include "blend2d_path_builder.hpp"
#include "coverage_surface.hpp"

#include <blend2d.h>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <glm/glm.hpp>

namespace chronon3d::renderer {

using chronon3d::blend2d_bridge::CoverageSurface;
using chronon3d::blend2d_bridge::path_builder::make_bl_path;

bool rasterize_path_fill_blend2d(
    chronon3d::Framebuffer& fb,
    const chronon3d::PathShape& path,
    const chronon3d::Mat4& model,
    chronon3d::f32 opacity,
    const chronon3d::raster::BBox& bbox,
    const RenderState* /*state - mask/clip deferred to PR5*/)
{
    using namespace chronon3d;
    if (path.commands.empty()) return false;
    if (bbox.is_empty()) return false;
    if (!path.fill.enabled) return false;

    const int roi_w = bbox.x1 - bbox.x0;
    const int roi_h = bbox.y1 - bbox.y0;
    if (roi_w <= 0 || roi_h <= 0) return false;

    // ── 1. A8 coverage surface (1 byte / pixel) ─────────────────────
    CoverageSurface cov(roi_w, roi_h);
    if (cov.empty()) return false;
    cov.fill(0x00);  // zero coverage before fillPath()

    // ── 2+3. Build BLPath in screen space, rasterize with SRC_COPY ──
    {
        BLContext ctx(cov.image());
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(255, 255, 255, 255));
        // Chronon's custom path renderer walks `inside = !inside` per
        // contour, so the equivalent BL2 rule is BL_FILL_RULE_EVEN_ODD.
        // It must be set on the context before `fillPath` — pass it as
        // a fillPath argument has no overload (BL2 reads from context).
        ctx.setFillRule(BL_FILL_RULE_EVEN_ODD);
        const BLPath bl_path = make_bl_path(
            path, model, Vec2{static_cast<f32>(bbox.x0), static_cast<f32>(bbox.y0)});
        if (!bl_path.empty()) {
            ctx.fillPath(bl_path);
        }
        ctx.end();
    }

    // ── 4. Shade + composite ────────────────────────────────────────
    // Walk the ROI once, reading the A8 byte and pushing the modulated
    // colour through `compositor::blend`.  This is the same linear-
    // float composition the legacy loop uses; the only thing that
    // changed is WHERE the coverage comes from (BL2 analytic AA vs
    // hard PIP inside/outside boolean).
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
            Color c = resolve_fill_color(path.fill, sp, bbox, opacity);
            // Modulate alpha by BL2 coverage: keeps the linear-float
            // framebuffer untouched on the RGB axis for fully covered
            // pixels (cov_f = 1.0) and softens edges.
            c.a *= cov_f;
            row[sx] = chronon3d::compositor::blend(c, row[sx], BlendMode::Normal);
        }
    }
    return true;
}

} // namespace chronon3d::renderer
