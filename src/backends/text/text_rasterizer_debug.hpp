// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// FASE 3 Step 2 — Debug overlay extracted from text_rasterizer_render.cpp
//
// draw_debug_overlays(): draws TextShape bounding box, ink bounds,
// and baseline overlays onto the rendered image (TICKET-007 per-instance
// debug_cfg path).
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <blend2d.h>

namespace chronon3d {
namespace detail {

/// Draw debug overlays (box rect, ink bounds, baseline) onto the
/// rendered image when debug_cfg->text_bbox() is enabled.
inline void draw_debug_overlays(
    BLImage& img,
    const DebugConfig* debug_cfg,
    const TextShape& t,
    float padding,
    int ink_left,
    int ink_right,
    int ink_top,
    int ink_bottom,
    const TextLayoutResult& layout_res,
    const BLFont& font,
    float text_start_x,
    int img_w)
{
    if (!debug_cfg || !debug_cfg->text_bbox() || t.text.empty()) return;

    BLContext dbg(img);
    dbg.setCompOp(BL_COMP_OP_SRC_OVER);

    const float pad_f = padding * 0.5f;

    if (t.box.enabled) {
        dbg.setStrokeStyle(BLRgba32(255, 48, 48, 200));
        dbg.setStrokeWidth(2.0f);
        dbg.strokeRect(pad_f, pad_f, t.box.size.x, t.box.size.y);

        const float cx = pad_f + t.box.size.x * 0.5f;
        const float cy = pad_f + t.box.size.y * 0.5f;
        dbg.setStrokeStyle(BLRgba32(0, 255, 255, 200));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeLine(cx - 14.0f, cy, cx + 14.0f, cy);
        dbg.strokeLine(cx, cy - 14.0f, cx, cy + 14.0f);
    }

    if (ink_left < ink_right) {
        const float ink_x = static_cast<float>(ink_left);
        const float ink_y = static_cast<float>(ink_top);
        const float ink_w2 = static_cast<float>(ink_right - ink_left);
        const float ink_h = static_cast<float>(ink_bottom - ink_top);
        dbg.setStrokeStyle(BLRgba32(48, 255, 48, 200));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeRect(ink_x, ink_y, ink_w2, ink_h);

        const float icx = static_cast<float>(ink_left + ink_right) * 0.5f;
        const float icy = static_cast<float>(ink_top + ink_bottom) * 0.5f;
        dbg.setStrokeStyle(BLRgba32(255, 255, 48, 200));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeLine(icx - 8.0f, icy, icx + 8.0f, icy);
        dbg.strokeLine(icx, icy - 8.0f, icy, icy + 8.0f);
    }

    if (!layout_res.lines.empty()) {
        const float baseline_y = text_start_x + layout_res.lines[0].position.y + font.metrics().ascent;
        dbg.setStrokeStyle(BLRgba32(48, 128, 255, 180));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeLine(0.0f, baseline_y, static_cast<float>(img_w), baseline_y);
    }
    dbg.end();
}

} // namespace detail
} // namespace chronon3d
