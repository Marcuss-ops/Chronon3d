// ---------------------------------------------------------------------------
// diagnostics/bbox_overlay.cpp
// ---------------------------------------------------------------------------

#include "bbox_overlay.hpp"
#include "../../rasterizers/line_rasterizer.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer::diagnostics {

void draw_bbox_overlay(Framebuffer& fb, const raster::BBox& bbox, const Color& color) {
    if (bbox.is_empty()) {
        return;
    }

    const f32 x0 = static_cast<f32>(bbox.x0);
    const f32 y0 = static_cast<f32>(bbox.y0);
    const f32 x1 = static_cast<f32>(std::max(bbox.x0, bbox.x1 - 1));
    const f32 y1 = static_cast<f32>(std::max(bbox.y0, bbox.y1 - 1));

    renderer::bline(fb, {x0, y0}, {x1, y0}, color);
    renderer::bline(fb, {x1, y0}, {x1, y1}, color);
    renderer::bline(fb, {x1, y1}, {x0, y1}, color);
    renderer::bline(fb, {x0, y1}, {x0, y0}, color);
}

} // namespace chronon3d::renderer::diagnostics
