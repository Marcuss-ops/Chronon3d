#pragma once

// Canonical producer-surface sizing for graph producers.  Phase 1 keeps
// composition semantics unchanged: the returned origin is canvas-space and
// downstream nodes can still expand/composite the producer as before.

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::graph::detail {

enum class ProducerSurfaceKind : unsigned char {
    Video,
    Background,
    Image,
    Text,
};

struct ProducerSurfaceBounds {
    raster::BBox bounds{};
    bool tight{false};

    [[nodiscard]] int width() const noexcept {
        return std::max(1, bounds.x1 - bounds.x0);
    }
    [[nodiscard]] int height() const noexcept {
        return std::max(1, bounds.y1 - bounds.y0);
    }
};

inline void record_producer_surface(
    RenderCounters* counters,
    ProducerSurfaceKind kind,
    const ProducerSurfaceBounds& surface,
    int canvas_width,
    int canvas_height) noexcept
{
    if (!counters) return;
    const auto pixels = static_cast<std::uint64_t>(surface.width()) *
                        static_cast<std::uint64_t>(surface.height());
    const auto canvas_pixels = static_cast<std::uint64_t>(canvas_width) *
                               static_cast<std::uint64_t>(canvas_height);
    counters->producer_surface_pixels.fetch_add(pixels, std::memory_order_relaxed);
    counters->producer_canvas_pixels.fetch_add(canvas_pixels, std::memory_order_relaxed);
    if (surface.tight) {
        counters->tight_surface_count.fetch_add(1, std::memory_order_relaxed);
        if (kind == ProducerSurfaceKind::Text) {
            counters->producer_tight_text_count.fetch_add(1, std::memory_order_relaxed);
        } else if (kind == ProducerSurfaceKind::Image) {
            counters->producer_tight_image_count.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (kind == ProducerSurfaceKind::Text || kind == ProducerSurfaceKind::Image) {
        counters->full_canvas_overlay_count.fetch_add(1, std::memory_order_relaxed);
        if (kind == ProducerSurfaceKind::Text) {
            counters->producer_full_frame_text_count.fetch_add(1, std::memory_order_relaxed);
        } else {
            counters->producer_full_frame_image_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

[[nodiscard]] inline ProducerSurfaceBounds resolve_producer_surface_bounds(
    int canvas_width,
    int canvas_height,
    ProducerSurfaceKind kind,
    const raster::BBox* visual_bounds) noexcept
{
    const raster::BBox canvas{0, 0, canvas_width, canvas_height};
    if (kind == ProducerSurfaceKind::Video ||
        kind == ProducerSurfaceKind::Background ||
        !visual_bounds || visual_bounds->is_empty()) {
        return ProducerSurfaceBounds{canvas, false};
    }

    // Bounds used for a producer surface are clipped to the output canvas.
    // Keeping this in one place prevents SourceNode/TextRunNode from making
    // subtly different origin and rounding decisions.
    raster::BBox clipped{
        std::max(canvas.x0, visual_bounds->x0),
        std::max(canvas.y0, visual_bounds->y0),
        std::min(canvas.x1, visual_bounds->x1),
        std::min(canvas.y1, visual_bounds->y1)};
    if (clipped.is_empty()) {
        return ProducerSurfaceBounds{raster::BBox{0, 0, 1, 1}, true};
    }
    const auto canvas_pixels = static_cast<double>(canvas_width) * canvas_height;
    const auto producer_pixels = static_cast<double>(clipped.x1 - clipped.x0) *
                                 (clipped.y1 - clipped.y0);
    const bool is_overlay = kind == ProducerSurfaceKind::Image ||
                            kind == ProducerSurfaceKind::Text;
    // A full-canvas overlay remains full-canvas. This protects backgrounds
    // and preserves the exact old contract for sources that cover the frame.
    const bool differs_from_canvas =
        clipped.x0 != canvas.x0 || clipped.y0 != canvas.y0 ||
        clipped.x1 != canvas.x1 || clipped.y1 != canvas.y1;
    const bool tight = is_overlay && differs_from_canvas &&
                       producer_pixels < canvas_pixels;
    return ProducerSurfaceBounds{tight ? clipped : canvas, tight};
}

[[nodiscard]] inline ProducerSurfaceBounds resolve_local_producer_surface(
    int canvas_width,
    int canvas_height,
    ProducerSurfaceKind kind,
    Vec2 origin,
    Vec2 size) noexcept
{
    (void)origin;
    if (kind != ProducerSurfaceKind::Text || size.x <= 0.0f || size.y <= 0.0f) {
        return ProducerSurfaceBounds{
            raster::BBox{0, 0, canvas_width, canvas_height}, false};
    }
    return ProducerSurfaceBounds{
        raster::BBox{0, 0,
                     std::max<i32>(1, static_cast<i32>(std::ceil(size.x))),
                     std::max<i32>(1, static_cast<i32>(std::ceil(size.y)))},
        true};
}

} // namespace chronon3d::graph::detail
