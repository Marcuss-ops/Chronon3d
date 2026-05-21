#pragma once

#include <chronon3d/core/counters.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli::telemetry {

inline std::vector<chronon3d::telemetry::CounterTelemetryRecord> capture_counters(const chronon3d::RenderCounters& counters) {
    return {
        {"pixels_touched", counters.pixels_touched.load(std::memory_order_relaxed)},
        {"cache_hits", counters.cache_hits.load(std::memory_order_relaxed)},
        {"cache_misses", counters.cache_misses.load(std::memory_order_relaxed)},
        {"nodes_executed", counters.nodes_executed.load(std::memory_order_relaxed)},
        {"layers_rendered", counters.layers_rendered.load(std::memory_order_relaxed)},
        {"text_glyphs_rasterized", counters.text_glyphs_rasterized.load(std::memory_order_relaxed)},
        {"images_sampled", counters.images_sampled.load(std::memory_order_relaxed)},
        {"blur_pixels", counters.blur_pixels.load(std::memory_order_relaxed)},
        {"simd_lerp_calls", counters.simd_lerp_calls.load(std::memory_order_relaxed)},
        {"tiles_total", counters.tiles_total.load(std::memory_order_relaxed)},
        {"tiles_hit", counters.tiles_hit.load(std::memory_order_relaxed)},
        {"tiles_miss", counters.tiles_miss.load(std::memory_order_relaxed)},
        {"tiles_partial", counters.tiles_partial.load(std::memory_order_relaxed)},
        {"node_cache_hash_collisions", counters.node_cache_hash_collisions.load(std::memory_order_relaxed)},
        {"clear_calls", counters.clear_calls.load(std::memory_order_relaxed)},
        {"clear_pixels", counters.clear_pixels.load(std::memory_order_relaxed)},
        {"clear_copy_pixels", counters.clear_copy_pixels.load(std::memory_order_relaxed)},
        {"composite_calls", counters.composite_calls.load(std::memory_order_relaxed)},
        {"composite_pixels", counters.composite_pixels.load(std::memory_order_relaxed)},
        {"transform_calls", counters.transform_calls.load(std::memory_order_relaxed)},
        {"transform_pixels", counters.transform_pixels.load(std::memory_order_relaxed)},
        {"effect_stack_calls", counters.effect_stack_calls.load(std::memory_order_relaxed)},
        {"effect_pixels", counters.effect_pixels.load(std::memory_order_relaxed)},
        {"layer_culling_tests", counters.layer_culling_tests.load(std::memory_order_relaxed)},
        {"layers_culled", counters.layers_culled.load(std::memory_order_relaxed)},
        {"layers_visible", counters.layers_visible.load(std::memory_order_relaxed)},
        {"framebuffer_allocations", counters.framebuffer_allocations.load(std::memory_order_relaxed)},
        {"framebuffer_reuses", counters.framebuffer_reuses.load(std::memory_order_relaxed)},
        {"framebuffer_bytes_allocated", counters.framebuffer_bytes_allocated.load(std::memory_order_relaxed)},
        {"framebuffer_bytes_peak", counters.framebuffer_bytes_peak.load(std::memory_order_relaxed)},
    };
}

inline void add_counters(chronon3d::RenderCounters& dst, const chronon3d::RenderCounters& src) {
    dst.pixels_touched.fetch_add(src.pixels_touched.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.cache_hits.fetch_add(src.cache_hits.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.cache_misses.fetch_add(src.cache_misses.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.nodes_executed.fetch_add(src.nodes_executed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.layers_rendered.fetch_add(src.layers_rendered.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.text_glyphs_rasterized.fetch_add(src.text_glyphs_rasterized.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.images_sampled.fetch_add(src.images_sampled.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.blur_pixels.fetch_add(src.blur_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.simd_lerp_calls.fetch_add(src.simd_lerp_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.tiles_total.fetch_add(src.tiles_total.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.tiles_hit.fetch_add(src.tiles_hit.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.tiles_miss.fetch_add(src.tiles_miss.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.tiles_partial.fetch_add(src.tiles_partial.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.node_cache_hash_collisions.fetch_add(src.node_cache_hash_collisions.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clear_calls.fetch_add(src.clear_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clear_pixels.fetch_add(src.clear_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clear_copy_pixels.fetch_add(src.clear_copy_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.composite_calls.fetch_add(src.composite_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.composite_pixels.fetch_add(src.composite_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.transform_calls.fetch_add(src.transform_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.transform_pixels.fetch_add(src.transform_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.effect_stack_calls.fetch_add(src.effect_stack_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.effect_pixels.fetch_add(src.effect_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.layer_culling_tests.fetch_add(src.layer_culling_tests.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.layers_culled.fetch_add(src.layers_culled.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.layers_visible.fetch_add(src.layers_visible.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_allocations.fetch_add(src.framebuffer_allocations.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_reuses.fetch_add(src.framebuffer_reuses.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_bytes_allocated.fetch_add(src.framebuffer_bytes_allocated.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_bytes_peak.fetch_add(src.framebuffer_bytes_peak.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

inline void record_output_run(const std::string& composition_id,
                              const std::string& output_path,
                              bool success,
                              int frames_total,
                              int frames_written,
                              double wall_time_ms,
                              double render_ms,
                              double encode_ms,
                              const std::string& started_at_iso = {},
                              const std::vector<chronon3d::telemetry::PhaseTelemetryRecord>& phases = {},
                              const std::vector<chronon3d::telemetry::CounterTelemetryRecord>& counters = {},
                              const chronon3d::RenderCounters* counters_src = nullptr,
                              const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& frames = {}) {
    chronon3d::telemetry::TelemetryManager::instance().initialize_default_stores();

    chronon3d::telemetry::RenderTelemetryRecord run;
    run.run_id = chronon3d::telemetry::TelemetryManager::generate_uuid();
    run.composition_id = composition_id;
    run.output_path = output_path;
    run.success = success;
    run.frames_total = frames_total;
    run.frames_written = frames_written;
    run.wall_time_ms = wall_time_ms;
    run.render_ms = render_ms;
    run.encode_ms = encode_ms;
    run.effective_fps = wall_time_ms > 0.0 ? (frames_written * 1000.0 / wall_time_ms) : 0.0;
    run.bytes_allocated_peak = chronon3d::telemetry::TelemetryManager::get_peak_memory_usage();
    run.started_at_iso = started_at_iso;
    run.finished_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();

    if (counters_src) {
        run.pixels_touched = counters_src->pixels_touched.load(std::memory_order_relaxed);
        run.cache_hits = counters_src->cache_hits.load(std::memory_order_relaxed);
        run.cache_misses = counters_src->cache_misses.load(std::memory_order_relaxed);
        run.nodes_executed = counters_src->nodes_executed.load(std::memory_order_relaxed);
        run.layers_rendered = counters_src->layers_rendered.load(std::memory_order_relaxed);
        run.text_glyphs_rasterized = counters_src->text_glyphs_rasterized.load(std::memory_order_relaxed);
        run.images_sampled = counters_src->images_sampled.load(std::memory_order_relaxed);
        run.blur_pixels = counters_src->blur_pixels.load(std::memory_order_relaxed);
        run.simd_lerp_calls = counters_src->simd_lerp_calls.load(std::memory_order_relaxed);
        run.tiles_total = counters_src->tiles_total.load(std::memory_order_relaxed);
        run.tiles_hit = counters_src->tiles_hit.load(std::memory_order_relaxed);
        run.tiles_miss = counters_src->tiles_miss.load(std::memory_order_relaxed);
        run.tiles_partial = counters_src->tiles_partial.load(std::memory_order_relaxed);
        run.node_cache_hash_collisions = counters_src->node_cache_hash_collisions.load(std::memory_order_relaxed);
        run.clear_calls = counters_src->clear_calls.load(std::memory_order_relaxed);
        run.clear_pixels = counters_src->clear_pixels.load(std::memory_order_relaxed);
        run.composite_calls = counters_src->composite_calls.load(std::memory_order_relaxed);
        run.composite_pixels = counters_src->composite_pixels.load(std::memory_order_relaxed);
        run.transform_calls = counters_src->transform_calls.load(std::memory_order_relaxed);
        run.transform_pixels = counters_src->transform_pixels.load(std::memory_order_relaxed);
        run.effect_stack_calls = counters_src->effect_stack_calls.load(std::memory_order_relaxed);
        run.effect_pixels = counters_src->effect_pixels.load(std::memory_order_relaxed);
        run.layer_culling_tests = counters_src->layer_culling_tests.load(std::memory_order_relaxed);
        run.layers_culled = counters_src->layers_culled.load(std::memory_order_relaxed);
        run.layers_visible = counters_src->layers_visible.load(std::memory_order_relaxed);
        run.framebuffer_allocations = counters_src->framebuffer_allocations.load(std::memory_order_relaxed);
        run.framebuffer_reuses = counters_src->framebuffer_reuses.load(std::memory_order_relaxed);
        run.framebuffer_bytes_allocated = counters_src->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
        run.framebuffer_bytes_peak = counters_src->framebuffer_bytes_peak.load(std::memory_order_relaxed);
    }

    const auto resolved_counters = counters.empty() && counters_src
        ? capture_counters(*counters_src)
        : counters;

    chronon3d::telemetry::TelemetryManager::instance().record_run(run, frames, phases, resolved_counters);
}

} // namespace chronon3d::cli::telemetry
