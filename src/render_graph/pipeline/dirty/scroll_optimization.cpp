// ---------------------------------------------------------------------------
// dirty/scroll_optimization.cpp — Scrolling shortcut for dirty regions
// ---------------------------------------------------------------------------

#include "scroll_optimization.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cstring>
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

    auto& prev = sw_renderer->buffer_ring().prev_framebuffer();
    if (prev.use_count() > 1) {
        // The previous framebuffer is still referenced elsewhere (e.g. the
        // video encoder is reading it), so we cannot mutate it in-place.
        // We still need a writable copy for the in-place shift. Reuse a
        // pooled framebuffer instead of allocating a fresh one.
        const auto t0 = profiling::now();
        auto* pool = sw_renderer->buffer_ring().pool();
        if (pool) {
            auto fresh = pool->acquire_shared(width, height, false);
            if (fresh->data() != prev->data()) {
                std::memcpy(fresh->data(), prev->data(),
                            static_cast<size_t>(width) * height * sizeof(Color));
            }
            prev = std::move(fresh);
        } else {
            prev = std::make_shared<Framebuffer>(*prev);
        }
        if (profiling::g_current_counters) {
            profiling::g_current_counters->scroll_opt_copy_ms.fetch_add(
                static_cast<uint64_t>(profiling::duration_ms(t0, profiling::now())),
                std::memory_order_relaxed);
        }
    }
    prev->shift(dx, dy);

    raster::BBox strip{0, 0, width, height};
    if (dx > 0) strip.x1 = dx;
    else if (dx < 0) strip.x0 = width + dx;
    if (dy > 0) strip.y1 = dy;
    else if (dy < 0) strip.y0 = height + dy;
    return strip;
}

} // namespace chronon3d::graph::detail
