#include "blend2d_stroke_options.hpp"

namespace chronon3d::blend2d_bridge::stroke_options {

void apply_bl2_stroke_state(BLContext& ctx, const chronon3d::PathStroke& stroke) {
    // ── width (screen pixels) ───────────────────────────────────────
    ctx.setStrokeWidth(static_cast<double>(stroke.width));

    // ── caps (sets start+inner+end BL2 cap in one call) ─────────────
    switch (stroke.cap) {
        case chronon3d::LineCap::Butt:
            ctx.setStrokeCaps(BL_STROKE_CAP_BUTT);
            break;
        case chronon3d::LineCap::Round:
            ctx.setStrokeCaps(BL_STROKE_CAP_ROUND);
            break;
        case chronon3d::LineCap::Square:
            ctx.setStrokeCaps(BL_STROKE_CAP_SQUARE);
            break;
    }

    // ── joins (single-join setter) ───────────────────────────────────
    switch (stroke.join) {
        case chronon3d::LineJoin::Miter:
            ctx.setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
            ctx.setStrokeMiterLimit(static_cast<double>(stroke.miter_limit));
            break;
        case chronon3d::LineJoin::Round:
            ctx.setStrokeJoin(BL_STROKE_JOIN_ROUND);
            break;
        case chronon3d::LineJoin::Bevel:
            ctx.setStrokeJoin(BL_STROKE_JOIN_BEVEL);
            break;
    }

    // ── dash: not touched.  Chronon's `prepare_stroke_contours`
    //     pre-splits the polyline along the dash pattern; BL2 would
    //     re-evaluate the pattern on top of that and diverge, so we
    //     leave BL2's run-time dash offset / array at their defaults.
}

} // namespace chronon3d::blend2d_bridge::stroke_options
