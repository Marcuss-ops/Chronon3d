#pragma once

#include <chronon3d/core/profiling/counters.hpp>

#include <cstddef>

namespace chronon3d::cli::telemetry {

/// Merges counters from src into dst, handling cumulative (add),
/// snapshot (store), and peak (max) semantics correctly.
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
    dst.graph_resolve_layers_ms.fetch_add(src.graph_resolve_layers_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.graph_dirty_rect_ms.fetch_add(src.graph_dirty_rect_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.graph_build_ms.fetch_add(src.graph_build_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.graph_execute_ms.fetch_add(src.graph_execute_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.graph_total_ms.fetch_add(src.graph_total_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.compiled_graph_refresh_ms.fetch_add(src.compiled_graph_refresh_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.cache_eval_ms.fetch_add(src.cache_eval_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.dirty_eval_ms.fetch_add(src.dirty_eval_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.input_resolve_ms.fetch_add(src.input_resolve_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_lifetime_ms.fetch_add(src.framebuffer_lifetime_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.node_schedule_ms.fetch_add(src.node_schedule_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.node_dispatch_ms.fetch_add(src.node_dispatch_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.telemetry_emit_ms.fetch_add(src.telemetry_emit_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clear_calls.fetch_add(src.clear_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clear_pixels.fetch_add(src.clear_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_copy_pixels.fetch_add(src.clearnode_copy_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.composite_copy_pixels.fetch_add(src.composite_copy_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_bytes_avoided.fetch_add(src.clearnode_bytes_avoided.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_memcpy_bytes.fetch_add(src.clearnode_memcpy_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_memcpy_calls.fetch_add(src.clearnode_memcpy_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_detach_shared_count.fetch_add(src.clearnode_detach_shared_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_partial_clip_copy_count.fetch_add(src.clearnode_partial_clip_copy_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_full_clip_skip_count.fetch_add(src.clearnode_full_clip_skip_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.prev_fb_use_count_sum.fetch_add(src.prev_fb_use_count_sum.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.prev_fb_use_count_samples.fetch_add(src.prev_fb_use_count_samples.load(std::memory_order_relaxed), std::memory_order_relaxed);
    {
        const auto v = src.prev_fb_use_count_peak.load(std::memory_order_relaxed);
        if (v > dst.prev_fb_use_count_peak.load(std::memory_order_relaxed))
            dst.prev_fb_use_count_peak.store(v, std::memory_order_relaxed);
    }
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
    dst.framebuffer_acquire_ms.fetch_add(src.framebuffer_acquire_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_clear_ms.fetch_add(src.framebuffer_clear_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_ms.fetch_add(src.clearnode_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_ms.fetch_add(src.clearnode_restore_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_rect_count.fetch_add(src.clearnode_restore_rect_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_pixels.fetch_add(src.clearnode_restore_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_bytes.fetch_add(src.clearnode_restore_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_full_frame_count.fetch_add(src.clearnode_restore_full_frame_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_dirty_rect_count.fetch_add(src.clearnode_restore_dirty_rect_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.clearnode_restore_noop_count.fetch_add(src.clearnode_restore_noop_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_clear_ms.fetch_add(src.framebuffer_pool_clear_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_enqueue_ms.fetch_add(src.framebuffer_enqueue_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_empty_alloc.fetch_add(src.framebuffer_pool_empty_alloc.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_best_fit_reuse.fetch_add(src.framebuffer_pool_best_fit_reuse.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_exact_hit.fetch_add(src.framebuffer_pool_exact_hit.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_buffer_returned_to_pool_count.fetch_add(src.framebuffer_buffer_returned_to_pool_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_prealloc_created.fetch_add(src.framebuffer_prealloc_created.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_budget_bytes.store(src.framebuffer_pool_budget_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    {
        const auto v = src.framebuffer_pool_retained_bytes.load(std::memory_order_relaxed);
        if (v > dst.framebuffer_pool_retained_bytes.load(std::memory_order_relaxed))
            dst.framebuffer_pool_retained_bytes.store(v, std::memory_order_relaxed);
    }
    dst.framebuffer_pool_evicted_count.fetch_add(src.framebuffer_pool_evicted_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_evicted_bytes.fetch_add(src.framebuffer_pool_evicted_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_pool_pressure_count.fetch_add(src.framebuffer_pool_pressure_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    {
        const auto v = src.framebuffer_pool_size_class_count.load(std::memory_order_relaxed);
        if (v > dst.framebuffer_pool_size_class_count.load(std::memory_order_relaxed))
            dst.framebuffer_pool_size_class_count.store(v, std::memory_order_relaxed);
    }
    dst.unaligned_memory_copies.fetch_add(src.unaligned_memory_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.frame_conversion_copy_ms.fetch_add(src.frame_conversion_copy_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.video_graph_eval_ms.fetch_add(src.video_graph_eval_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.video_conversion_ms.fetch_add(src.video_conversion_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.video_pipe_write_ms.fetch_add(src.video_pipe_write_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.video_ffmpeg_latency_ms.fetch_add(src.video_ffmpeg_latency_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.io_queue_push_blocked_ms.fetch_add(src.io_queue_push_blocked_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.io_queue_pop_wait_ms.fetch_add(src.io_queue_pop_wait_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.io_writer_idle_wait_ms.fetch_add(src.io_writer_idle_wait_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.io_queue_peak_depth.fetch_add(src.io_queue_peak_depth.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.ffmpeg_pipe_write_blocked_ms.fetch_add(src.ffmpeg_pipe_write_blocked_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.converted_frame_cache_hits.fetch_add(src.converted_frame_cache_hits.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.converted_frame_cache_misses.fetch_add(src.converted_frame_cache_misses.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.program_cache_hits.fetch_add(src.program_cache_hits.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.program_cache_misses.fetch_add(src.program_cache_misses.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.program_cache_evictions.fetch_add(src.program_cache_evictions.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.ffmpeg_flush_ms.fetch_add(src.ffmpeg_flush_ms.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_bytes_allocated.fetch_add(src.framebuffer_bytes_allocated.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.framebuffer_bytes_peak.fetch_add(src.framebuffer_bytes_peak.load(std::memory_order_relaxed), std::memory_order_relaxed);
    // Hardware-constant snapshots: just store (not accumulate)
    dst.system_logical_cores.store(src.system_logical_cores.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.system_ram_total_mb.store(src.system_ram_total_mb.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.system_ram_available_min_mb.store(src.system_ram_available_min_mb.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.tbb_arena_max_concurrency.store(src.tbb_arena_max_concurrency.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.tbb_active_workers_peak.store(src.tbb_active_workers_peak.load(std::memory_order_relaxed), std::memory_order_relaxed);

    // Per-run snapshots: use store() with max() for peak values
    {
        const auto v = src.process_cpu_user_ms.load(std::memory_order_relaxed);
        if (v > dst.process_cpu_user_ms.load(std::memory_order_relaxed))
            dst.process_cpu_user_ms.store(v, std::memory_order_relaxed);
    }
    {
        const auto v = src.process_cpu_sys_ms.load(std::memory_order_relaxed);
        if (v > dst.process_cpu_sys_ms.load(std::memory_order_relaxed))
            dst.process_cpu_sys_ms.store(v, std::memory_order_relaxed);
    }
    {
        const auto v = src.process_rss_peak_mb.load(std::memory_order_relaxed);
        if (v > dst.process_rss_peak_mb.load(std::memory_order_relaxed))
            dst.process_rss_peak_mb.store(v, std::memory_order_relaxed);
    }

    // Cumulative counters: use fetch_add (these are per-run deltas)
    dst.parallel_regions_count.fetch_add(src.parallel_regions_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.parallel_regions_skipped_small_level.fetch_add(src.parallel_regions_skipped_small_level.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.used_parallel_clear.fetch_add(src.used_parallel_clear.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.skipped_clear_small.fetch_add(src.skipped_clear_small.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.used_parallel_transform.fetch_add(src.used_parallel_transform.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.skipped_transform_small.fetch_add(src.skipped_transform_small.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.used_parallel_composite.fetch_add(src.used_parallel_composite.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.skipped_composite_small.fetch_add(src.skipped_composite_small.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.skipped_encoder_backpressure.fetch_add(src.skipped_encoder_backpressure.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.dirty_rect_count.fetch_add(src.dirty_rect_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.dirty_pixels.fetch_add(src.dirty_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
    dst.dirty_full_fallbacks.fetch_add(src.dirty_full_fallbacks.load(std::memory_order_relaxed), std::memory_order_relaxed);
    for (std::size_t i = 0; i < dirty_fallback_reason_count(); ++i) {
        dst.dirty_full_fallback_reasons[i].fetch_add(
            src.dirty_full_fallback_reasons[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed
        );
    }
}

} // namespace chronon3d::cli::telemetry
