#include "pipe_export_session_internal.hpp"

#include <algorithm>
#include <atomic>

namespace chronon3d::cli::detail {

FrameProfileSample sample_frame_profile(const RenderLoopContext& ctx) {
    FrameProfileSample s;
    if (!ctx.counters) return s;
    const auto load = [](const auto& c) {
        return c.load(std::memory_order_relaxed);
    };
    s.timeline_eval_us = load(ctx.counters->timeline_eval_wall_us);
    s.text_us = load(ctx.counters->text_layout_wall_ms) * 1000
              + load(ctx.counters->text_rasterization_wall_ms) * 1000
              + load(ctx.counters->text_shaping_wall_ms) * 1000
              + load(ctx.counters->text_bidi_wall_ms) * 1000
              + load(ctx.counters->glyph_cache_lookup_wall_us)
              + load(ctx.counters->glyph_atlas_upload_wall_us)
              + load(ctx.counters->text_draw_wall_us);
    s.graph_prepare_us = load(ctx.counters->graph_resolve_layers_wall_us)
                       + load(ctx.counters->graph_dirty_rect_wall_us)
                       + load(ctx.counters->graph_build_wall_us);
    s.graph_execute_us = load(ctx.counters->graph_execute_wall_us);
    s.compositing_us = load(ctx.counters->clearnode_wall_ms) * 1000
                     + load(ctx.counters->compositenode_blend_wall_ms) * 1000
                     + load(ctx.counters->compositenode_setup_wall_ms) * 1000
                     + load(ctx.counters->compositenode_copy_wall_ms) * 1000
                     + load(ctx.counters->compositenode_dispatch_wall_ms) * 1000;
    s.effects_us = load(ctx.counters->effect_stack_total_wall_ms) * 1000;
    s.surface_us = load(ctx.counters->framebuffer_acquire_wall_us)
                 + load(ctx.counters->framebuffer_clear_wall_us)
                 + load(ctx.counters->framebuffer_lifetime_wall_us);
    s.overhead_us = load(ctx.counters->node_overhead_wall_us)
                  + load(ctx.counters->node_dispatch_wall_us)
                  + load(ctx.counters->node_schedule_wall_us)
                  + load(ctx.counters->telemetry_emit_wall_us);
    s.image_draw_us = load(ctx.counters->image_draw_wall_us);
    s.image_draw_count = load(ctx.counters->image_draw_count);
    s.text_shaping_ms = load(ctx.counters->text_shaping_wall_ms);
    s.text_bidi_ms = load(ctx.counters->text_bidi_wall_ms);
    s.text_layout_ms = load(ctx.counters->text_layout_wall_ms);
    s.text_glyph_lookup_us = load(ctx.counters->glyph_cache_lookup_wall_us);
    s.text_raster_ms = load(ctx.counters->text_rasterization_wall_ms);
    s.text_atlas_upload_us = load(ctx.counters->glyph_atlas_upload_wall_us);
    s.text_draw_us = load(ctx.counters->text_draw_wall_us);
    s.node_lookup_us = load(ctx.counters->node_cache_lookup_wall_us);
    return s;
}

FrameTimingProjection project_frame_timings(
    const FrameProfileSample& before,
    const FrameProfileSample& after,
    double frame_ms) {
    FrameTimingProjection p;
    p.breakdown.timeline_eval_ms = static_cast<double>(after.timeline_eval_us - before.timeline_eval_us) / 1000.0;
    p.breakdown.text_ms = static_cast<double>(after.text_us - before.text_us) / 1000.0;
    p.breakdown.graph_prepare_ms = static_cast<double>(after.graph_prepare_us - before.graph_prepare_us) / 1000.0;
    p.breakdown.graph_execute_ms = static_cast<double>(after.graph_execute_us - before.graph_execute_us) / 1000.0;
    p.breakdown.compositing_ms = static_cast<double>(after.compositing_us - before.compositing_us) / 1000.0;
    p.breakdown.effects_ms = static_cast<double>(after.effects_us - before.effects_us) / 1000.0;
    p.breakdown.surface_management_ms = static_cast<double>(after.surface_us - before.surface_us) / 1000.0;
    p.breakdown.backend_overhead_ms = static_cast<double>(after.overhead_us - before.overhead_us) / 1000.0;
    const double accounted_ms = p.breakdown.timeline_eval_ms + p.breakdown.text_ms +
                                p.breakdown.graph_prepare_ms + p.breakdown.graph_execute_ms +
                                p.breakdown.compositing_ms + p.breakdown.effects_ms +
                                p.breakdown.surface_management_ms + p.breakdown.backend_overhead_ms;
    p.breakdown.accounted_cpu_ms = accounted_ms;
    p.breakdown.unaccounted_cpu_ms = std::max(0.0, frame_ms - accounted_ms);

    p.image_timing.draw_ms = static_cast<double>(after.image_draw_us - before.image_draw_us) / 1000.0;
    p.image_timing.draw_count = after.image_draw_count - before.image_draw_count;
    p.text_timing.shaping_ms = static_cast<double>(after.text_shaping_ms - before.text_shaping_ms);
    p.text_timing.bidi_ms = static_cast<double>(after.text_bidi_ms - before.text_bidi_ms);
    p.text_timing.layout_ms = static_cast<double>(after.text_layout_ms - before.text_layout_ms);
    p.text_timing.glyph_cache_lookup_ms = static_cast<double>(after.text_glyph_lookup_us - before.text_glyph_lookup_us) / 1000.0;
    p.text_timing.raster_ms = static_cast<double>(after.text_raster_ms - before.text_raster_ms);
    p.text_timing.atlas_upload_ms = static_cast<double>(after.text_atlas_upload_us - before.text_atlas_upload_us) / 1000.0;
    p.text_timing.draw_ms = static_cast<double>(after.text_draw_us - before.text_draw_us) / 1000.0;
    p.node_lookup_ms = static_cast<double>(after.node_lookup_us - before.node_lookup_us) / 1000.0;
    return p;
}

} // namespace chronon3d::cli::detail
