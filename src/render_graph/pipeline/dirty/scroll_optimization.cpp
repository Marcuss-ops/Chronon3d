// ---------------------------------------------------------------------------
// dirty/scroll_optimization.cpp — Scrolling shortcut for dirty regions
// ---------------------------------------------------------------------------

#include "scroll_optimization.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <memory>

namespace chronon3d::graph::detail {

std::optional<raster::BBox> try_scroll_optimization(
    SoftwareRenderer* sw_renderer,
    const Camera2_5D& cam25d,
    i32 width,
    i32 height
) {
    if (!sw_renderer->frame_history().prev_camera_valid || !sw_renderer->frame_history().prev_camera.enabled || !cam25d.enabled) {
        return std::nullopt;
    }

    const Vec3& cp = cam25d.position;
    const Vec3& pp = sw_renderer->frame_history().prev_camera.position;
    Vec3 camera_delta = cp - pp;

    const bool camera_params_compatible =
        std::abs(sw_renderer->frame_history().prev_camera.zoom - cam25d.zoom) < 1e-3f;

    if (!camera_params_compatible || std::abs(camera_delta.z) >= 1e-3f) {
        return std::nullopt;
    }

    const i32 dx = -static_cast<i32>(std::round(camera_delta.x));
    const i32 dy = -static_cast<i32>(std::round(camera_delta.y));

    const bool scroll_eligible =
        std::abs(camera_delta.x - std::round(camera_delta.x)) < 0.1f &&
        std::abs(camera_delta.y - std::round(camera_delta.y)) < 0.1f &&
        (dx != 0 || dy != 0) &&
        std::abs(dx) < width &&
        std::abs(dy) < height;

    if (!scroll_eligible) return std::nullopt;

    CHRONON_ZONE_C("scroll_optimization", trace_category::kFrame);

    if (sw_renderer->buffer_ring().prev_framebuffer().use_count() > 1) {
        sw_renderer->buffer_ring().prev_framebuffer() = std::make_shared<Framebuffer>(*sw_renderer->buffer_ring().prev_framebuffer());
    }
    sw_renderer->buffer_ring().prev_framebuffer()->shift(dx, dy);

    raster::BBox strip{0, 0, width, height};
    if (dx > 0) strip.x1 = dx;
    else if (dx < 0) strip.x0 = width + dx;
    if (dy > 0) strip.y1 = dy;
    else if (dy < 0) strip.y0 = height + dy;
    return strip;
}

} // namespace chronon3d::graph::detail
