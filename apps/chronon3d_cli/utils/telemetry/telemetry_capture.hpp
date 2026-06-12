#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli::telemetry {

/// Snapshots all atomic counters into a CounterTelemetryRecord vector.
/// Also enriches with live framebuffer pool stats.
inline std::vector<chronon3d::telemetry::CounterTelemetryRecord> capture_counters(const chronon3d::RenderCounters& counters) {
    std::vector<chronon3d::telemetry::CounterTelemetryRecord> result = {
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
        {"graph_resolve_layers_ms", counters.graph_resolve_layers_ms.load(std::memory_order_relaxed)},
        {"graph_dirty_rect_ms", counters.graph_dirty_rect_ms.load(std::memory_order_relaxed)},
        {"graph_build_ms", counters.graph_build_ms.load(std::memory_order_relaxed)},
        {"graph_execute_ms", counters.graph_execute_ms.load(std::memory_order_relaxed)},
        {"graph_total_ms", counters.graph_total_ms.load(std::memory_order_relaxed)},
        {"compiled_graph_refresh_ms", counters.compiled_graph_refresh_ms.load(std::memory_order_relaxed)},
        {"cache_eval_ms", counters.cache_eval_ms.load(std::memory_order_relaxed)},
        {"dirty_eval_ms", counters.dirty_eval_ms.load(std::memory_order_relaxed)},
        {"input_resolve_ms", counters.input_resolve_ms.load(std::memory_order_relaxed)},
        {"framebuffer_lifetime_ms", counters.framebuffer_lifetime_ms.load(std::memory_order_relaxed)},
        {"node_schedule_ms", counters.node_schedule_ms.load(std::memory_order_relaxed)},
        {"node_dispatch_ms", counters.node_dispatch_ms.load(std::memory_order_relaxed)},
        {"node_execute_actual_ms", counters.node_execute_actual_ms.load(std::memory_order_relaxed)},
        {"node_overhead_ms", counters.node_overhead_ms.load(std::memory_order_relaxed)},
        {"level_parallel_count", counters.level_parallel_count.load(std::memory_order_relaxed)},
        {"level_sequential_count", counters.level_sequential_count.load(std::memory_order_relaxed)},
        {"telemetry_emit_ms", counters.telemetry_emit_ms.load(std::memory_order_relaxed)},
        {"predicted_bbox_ms", counters.predicted_bbox_ms.load(std::memory_order_relaxed)},
        {"clone_context_ms", counters.clone_context_ms.load(std::memory_order_relaxed)},
        {"state_assign_ms", counters.state_assign_ms.load(std::memory_order_relaxed)},
        {"clear_calls", counters.clear_calls.load(std::memory_order_relaxed)},
        {"clear_pixels", counters.clear_pixels.load(std::memory_order_relaxed)},
        {"clearnode_copy_pixels", counters.clearnode_copy_pixels.load(std::memory_order_relaxed)},
        {"composite_copy_pixels", counters.composite_copy_pixels.load(std::memory_order_relaxed)},
        {"clearnode_bytes_avoided", counters.clearnode_bytes_avoided.load(std::memory_order_relaxed)},
        {"clearnode_memcpy_bytes", counters.clearnode_memcpy_bytes.load(std::memory_order_relaxed)},
        {"clearnode_memcpy_calls", counters.clearnode_memcpy_calls.load(std::memory_order_relaxed)},
        {"clearnode_detach_shared_count", counters.clearnode_detach_shared_count.load(std::memory_order_relaxed)},
        {"clearnode_partial_clip_copy_count", counters.clearnode_partial_clip_copy_count.load(std::memory_order_relaxed)},
        {"clearnode_full_clip_skip_count", counters.clearnode_full_clip_skip_count.load(std::memory_order_relaxed)},
        {"prev_fb_use_count_sum", counters.prev_fb_use_count_sum.load(std::memory_order_relaxed)},
        {"prev_fb_use_count_samples", counters.prev_fb_use_count_samples.load(std::memory_order_relaxed)},
        {"prev_fb_use_count_peak", counters.prev_fb_use_count_peak.load(std::memory_order_relaxed)},
        {"composite_calls", counters.composite_calls.load(std::memory_order_relaxed)},
        {"composite_pixels", counters.composite_pixels.load(std::memory_order_relaxed)},
        {"transform_calls", counters.transform_calls.load(std::memory_order_relaxed)},
        {"transform_pixels", counters.transform_pixels.load(std::memory_order_relaxed)},
        {"effect_stack_calls", counters.effect_stack_calls.load(std::memory_order_relaxed)},
        {"effect_pixels", counters.effect_pixels.load(std::memory_order_relaxed)},
        {"effect_stack_total_ms", counters.effect_stack_total_ms.load(std::memory_order_relaxed)},
        {"layer_culling_tests", counters.layer_culling_tests.load(std::memory_order_relaxed)},
        {"layers_culled", counters.layers_culled.load(std::memory_order_relaxed)},
        {"layers_visible", counters.layers_visible.load(std::memory_order_relaxed)},
        {"framebuffer_allocations", counters.framebuffer_allocations.load(std::memory_order_relaxed)},
        {"framebuffer_reuses", counters.framebuffer_reuses.load(std::memory_order_relaxed)},
        {"framebuffer_bytes_allocated", counters.framebuffer_bytes_allocated.load(std::memory_order_relaxed)},
        {"framebuffer_bytes_peak", profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed)},
        {"dirty_rect_count", counters.dirty_rect_count.load(std::memory_order_relaxed)},
        {"dirty_pixels", counters.dirty_pixels.load(std::memory_order_relaxed)},
        {"dirty_full_fallbacks", counters.dirty_full_fallbacks.load(std::memory_order_relaxed)},
        {"dirty_full_fallback_predicted_bounds_missing",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::PredictedBoundsMissing)]
                .load(std::memory_order_relaxed)},
        {"dirty_full_fallback_composite_missing_input_bounds",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::CompositeMissingInputBounds)]
                .load(std::memory_order_relaxed)},
        {"dirty_full_fallback_transform_bounds_unknown",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::TransformBoundsUnknown)]
                .load(std::memory_order_relaxed)},
        {"dirty_full_fallback_effect_bounds_unknown",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::EffectBoundsUnknown)]
                .load(std::memory_order_relaxed)},
        {"framebuffer_acquire_ms", counters.framebuffer_acquire_ms.load(std::memory_order_relaxed)},
        {"framebuffer_clear_ms", counters.framebuffer_clear_ms.load(std::memory_order_relaxed)},
        {"clearnode_ms", counters.clearnode_ms.load(std::memory_order_relaxed)},
        {"clearnode_restore_ms", counters.clearnode_restore_ms.load(std::memory_order_relaxed)},
        {"clearnode_restore_rect_count", counters.clearnode_restore_rect_count.load(std::memory_order_relaxed)},
        {"clearnode_restore_pixels", counters.clearnode_restore_pixels.load(std::memory_order_relaxed)},
        {"clearnode_restore_bytes", counters.clearnode_restore_bytes.load(std::memory_order_relaxed)},
        {"clearnode_restore_full_frame_count", counters.clearnode_restore_full_frame_count.load(std::memory_order_relaxed)},
        {"clearnode_restore_dirty_rect_count", counters.clearnode_restore_dirty_rect_count.load(std::memory_order_relaxed)},
        {"clearnode_restore_noop_count", counters.clearnode_restore_noop_count.load(std::memory_order_relaxed)},
        {"clearnode_memcpy_ms", counters.clearnode_memcpy_ms.load(std::memory_order_relaxed)},
        {"clearnode_acquire_ms", counters.clearnode_acquire_ms.load(std::memory_order_relaxed)},
        {"clearnode_clear_ms", counters.clearnode_clear_ms.load(std::memory_order_relaxed)},
        {"compositenode_blend_ms", counters.compositenode_blend_ms.load(std::memory_order_relaxed)},
        {"compositenode_setup_ms", counters.compositenode_setup_ms.load(std::memory_order_relaxed)},
        {"compositenode_copy_ms", counters.compositenode_copy_ms.load(std::memory_order_relaxed)},
        {"compositenode_row_ms", counters.compositenode_row_ms.load(std::memory_order_relaxed)},
        {"compositenode_acquire_ms", counters.compositenode_acquire_ms.load(std::memory_order_relaxed)},
        {"compositenode_dispatch_ms", counters.compositenode_dispatch_ms.load(std::memory_order_relaxed)},
        {"compositenode_overhead_ms", counters.compositenode_overhead_ms.load(std::memory_order_relaxed)},
        {"compositenode_internal_us", counters.compositenode_internal_us.load(std::memory_order_relaxed)},
        {"framebuffer_pool_clear_ms", counters.framebuffer_pool_clear_ms.load(std::memory_order_relaxed)},
        {"framebuffer_enqueue_ms", counters.framebuffer_enqueue_ms.load(std::memory_order_relaxed)},
        {"framebuffer_pool_empty_alloc", counters.framebuffer_pool_empty_alloc.load(std::memory_order_relaxed)},
        {"framebuffer_pool_best_fit_reuse", counters.framebuffer_pool_best_fit_reuse.load(std::memory_order_relaxed)},
        {"framebuffer_pool_exact_hit", counters.framebuffer_pool_exact_hit.load(std::memory_order_relaxed)},
        {"framebuffer_buffer_returned_to_pool_count", counters.framebuffer_buffer_returned_to_pool_count.load(std::memory_order_relaxed)},
        {"framebuffer_prealloc_created", counters.framebuffer_prealloc_created.load(std::memory_order_relaxed)},
        {"unaligned_memory_copies", counters.unaligned_memory_copies.load(std::memory_order_relaxed)},
        {"frame_conversion_copy_ms", counters.frame_conversion_copy_ms.load(std::memory_order_relaxed)},
        {"video_graph_eval_ms", counters.video_graph_eval_ms.load(std::memory_order_relaxed)},
        {"video_conversion_ms", counters.video_conversion_ms.load(std::memory_order_relaxed)},
        {"video_pipe_write_ms", counters.video_pipe_write_ms.load(std::memory_order_relaxed)},
        {"video_ffmpeg_latency_ms", counters.video_ffmpeg_latency_ms.load(std::memory_order_relaxed)},
        {"io_queue_push_blocked_ms", counters.io_queue_push_blocked_ms.load(std::memory_order_relaxed)},
        {"io_queue_pop_wait_ms", counters.io_queue_pop_wait_ms.load(std::memory_order_relaxed)},
        {"io_writer_idle_wait_ms", counters.io_writer_idle_wait_ms.load(std::memory_order_relaxed)},
        {"io_queue_peak_depth", counters.io_queue_peak_depth.load(std::memory_order_relaxed)},
        {"ffmpeg_pipe_write_blocked_ms", counters.ffmpeg_pipe_write_blocked_ms.load(std::memory_order_relaxed)},
        {"converted_frame_cache_hits",  counters.converted_frame_cache_hits.load(std::memory_order_relaxed)},
        {"converted_frame_cache_misses", counters.converted_frame_cache_misses.load(std::memory_order_relaxed)},
        {"ffmpeg_flush_ms", counters.ffmpeg_flush_ms.load(std::memory_order_relaxed)},
        {"process_context_switches_voluntary", counters.process_context_switches_voluntary.load(std::memory_order_relaxed)},
        {"process_context_switches_involuntary", counters.process_context_switches_involuntary.load(std::memory_order_relaxed)},
        {"os_page_faults_major", counters.os_page_faults_major.load(std::memory_order_relaxed)},
        {"os_page_faults_minor", counters.os_page_faults_minor.load(std::memory_order_relaxed)},
        {"ffmpeg_cpu_user_pct", counters.ffmpeg_cpu_user_pct.load(std::memory_order_relaxed)},
        {"ffmpeg_cpu_sys_pct", counters.ffmpeg_cpu_sys_pct.load(std::memory_order_relaxed)},
        {"llc_references", counters.llc_references.load(std::memory_order_relaxed)},
        {"llc_misses", counters.llc_misses.load(std::memory_order_relaxed)},
        {"system_logical_cores", counters.system_logical_cores.load(std::memory_order_relaxed)},
        {"system_ram_total_mb", counters.system_ram_total_mb.load(std::memory_order_relaxed)},
        {"system_ram_available_min_mb", counters.system_ram_available_min_mb.load(std::memory_order_relaxed)},
        {"process_cpu_user_ms", counters.process_cpu_user_ms.load(std::memory_order_relaxed)},
        {"process_cpu_sys_ms", counters.process_cpu_sys_ms.load(std::memory_order_relaxed)},
        {"process_rss_peak_mb", counters.process_rss_peak_mb.load(std::memory_order_relaxed)},
        {"tbb_arena_max_concurrency", counters.tbb_arena_max_concurrency.load(std::memory_order_relaxed)},
        {"tbb_active_workers_peak", counters.tbb_active_workers_peak.load(std::memory_order_relaxed)},
        {"parallel_regions_count", counters.parallel_regions_count.load(std::memory_order_relaxed)},
        {"parallel_regions_skipped_small_level", counters.parallel_regions_skipped_small_level.load(std::memory_order_relaxed)},
        {"used_parallel_clear", counters.used_parallel_clear.load(std::memory_order_relaxed)},
        {"skipped_clear_small", counters.skipped_clear_small.load(std::memory_order_relaxed)},
        {"used_parallel_transform", counters.used_parallel_transform.load(std::memory_order_relaxed)},
        {"skipped_transform_small", counters.skipped_transform_small.load(std::memory_order_relaxed)},
        {"used_parallel_composite", counters.used_parallel_composite.load(std::memory_order_relaxed)},
        {"skipped_composite_small", counters.skipped_composite_small.load(std::memory_order_relaxed)},
        {"skipped_encoder_backpressure", counters.skipped_encoder_backpressure.load(std::memory_order_relaxed)},
        {"graph_executed_frames", counters.graph_executed_frames.load(std::memory_order_relaxed)},
        {"graph_skipped_frames", counters.graph_skipped_frames.load(std::memory_order_relaxed)},
        {"graph_executed_ms_sum", counters.graph_executed_ms_sum.load(std::memory_order_relaxed)},
        {"graph_skipped_ms_sum", counters.graph_skipped_ms_sum.load(std::memory_order_relaxed)},
        {"framebuffer_pool_capacity", 0},
        {"framebuffer_pool_available_count", 0},
        {"framebuffer_pool_current_bytes", 0},
        {"framebuffer_pool_total_allocations", 0},
        {"framebuffer_pool_total_reuses", 0},
    };

    // Enrich with live framebuffer pool stats if available
    if (chronon3d::profiling::g_current_framebuffer_pool) {
        auto pool_stats = chronon3d::profiling::g_current_framebuffer_pool->stats();
        for (auto& counter : result) {
            if (counter.counter_name == "framebuffer_pool_capacity") {
                counter.counter_value = pool_stats.max_bytes;
            } else if (counter.counter_name == "framebuffer_pool_available_count") {
                counter.counter_value = pool_stats.available_count;
            } else if (counter.counter_name == "framebuffer_pool_current_bytes") {
                counter.counter_value = pool_stats.current_bytes;
            } else if (counter.counter_name == "framebuffer_pool_total_allocations") {
                counter.counter_value = pool_stats.total_allocations;
            } else if (counter.counter_name == "framebuffer_pool_total_reuses") {
                counter.counter_value = pool_stats.total_reuses;
            }
        }
    }

    return result;
}

/// Captures non-zero graph phase timings from counters as PhaseTelemetryRecords.
inline std::vector<chronon3d::telemetry::PhaseTelemetryRecord> capture_graph_phase_records(const chronon3d::RenderCounters& counters) {
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;
    const auto add_phase = [&](const char* name, uint64_t value) {
        if (value > 0) {
            phases.push_back({name, static_cast<double>(value)});
        }
    };

    add_phase("graph_resolve_layers_ms", counters.graph_resolve_layers_ms.load(std::memory_order_relaxed));
    add_phase("graph_dirty_rect_ms", counters.graph_dirty_rect_ms.load(std::memory_order_relaxed));
    add_phase("graph_build_ms", counters.graph_build_ms.load(std::memory_order_relaxed));
    add_phase("graph_execute_ms", counters.graph_execute_ms.load(std::memory_order_relaxed));
    add_phase("graph_total_ms", counters.graph_total_ms.load(std::memory_order_relaxed));
    add_phase("compiled_graph_refresh_ms", counters.compiled_graph_refresh_ms.load(std::memory_order_relaxed));
    add_phase("cache_eval_ms", counters.cache_eval_ms.load(std::memory_order_relaxed));
    add_phase("dirty_eval_ms", counters.dirty_eval_ms.load(std::memory_order_relaxed));
    add_phase("input_resolve_ms", counters.input_resolve_ms.load(std::memory_order_relaxed));
    add_phase("framebuffer_lifetime_ms", counters.framebuffer_lifetime_ms.load(std::memory_order_relaxed));
    add_phase("node_schedule_ms", counters.node_schedule_ms.load(std::memory_order_relaxed));
    add_phase("node_dispatch_ms", counters.node_dispatch_ms.load(std::memory_order_relaxed));
    add_phase("telemetry_emit_ms", counters.telemetry_emit_ms.load(std::memory_order_relaxed));
    return phases;
}

} // namespace chronon3d::cli::telemetry
