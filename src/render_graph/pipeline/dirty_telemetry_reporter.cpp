#include "dirty_telemetry_reporter.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <cmath>

namespace chronon3d::graph {

double compute_and_apply_dirty_metrics(
    const detail::DirtyRectOutput& dirty_out,
    int width,
    int height,
    RenderCounters* counters,
    SoftwareRenderer* sw_renderer)
{
    double dirty_ratio = 1.0;
    u64 dirty_union_area_pixels = 0;

    if (dirty_out.dirty_rect) {
        const int dw = std::max(0, dirty_out.dirty_rect->x1 - dirty_out.dirty_rect->x0);
        const int dh = std::max(0, dirty_out.dirty_rect->y1 - dirty_out.dirty_rect->y0);
        const double area = static_cast<double>(dw) * static_cast<double>(dh);
        const double total = static_cast<double>(width) * height;
        if (total > 0) dirty_ratio = area / total;
        dirty_union_area_pixels = static_cast<u64>(area);
    }

    if (counters) {
        counters->dirty_union_area_pixels.store(
            dirty_union_area_pixels, std::memory_order_relaxed);
    }

    if (sw_renderer) {
        sw_renderer->dirty_telemetry().last_dirty_area_ratio = dirty_ratio;
    }

    return dirty_ratio;
}

void log_dirty_debug(
    SoftwareRenderer* sw_renderer,
    bool diagnostics_enabled,
    const detail::DirtyRectOutput& dirty_out,
    Frame frame)
{
    if (!sw_renderer || !diagnostics_enabled) return;

    if (dirty_out.dirty_rect) {
        spdlog::info(
            "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=[{},{} -> {},{}] prev_frame={}",
            static_cast<int>(frame),
            dirty_out.use_dirty_rects ? 1 : 0,
            sw_renderer->buffer_ring().prev_framebuffer() ? 1 : 0,
            dirty_out.dirty_rect->x0, dirty_out.dirty_rect->y0,
            dirty_out.dirty_rect->x1, dirty_out.dirty_rect->y1,
            static_cast<int>(sw_renderer->frame_history().prev_frame));
    } else {
        spdlog::info(
            "[dirty-debug] frame={} use_dirty_rects={} prev_fb={} dirty_rect=null prev_frame={}",
            static_cast<int>(frame),
            dirty_out.use_dirty_rects ? 1 : 0,
            sw_renderer->buffer_ring().prev_framebuffer() ? 1 : 0,
            static_cast<int>(sw_renderer->frame_history().prev_frame));
    }
}

void record_dirty_telemetry(
    SoftwareRenderer* sw_renderer,
    const detail::DirtyRectOutput& dirty_out,
    bool use_tile_execution,
    bool graph_reused)
{
    if (!sw_renderer) return;

    sw_renderer->update_dirty_telemetry(
        dirty_out.use_dirty_rects, dirty_out.dirty_rect,
        use_tile_execution, false, graph_reused);
}

} // namespace chronon3d::graph
