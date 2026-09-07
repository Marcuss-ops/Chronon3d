#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

// TelemetryManager::get_os_name()/get_cpu_model()/etc. are static helpers
// available unconditionally and are needed by `populate_run_host_attribs()`
// and `populate_run_metrics()`, which run in every build.
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include "telemetry_capture.hpp"

#include <string>
#include <vector>

namespace chronon3d::cli::telemetry {

/// Populates host-system attributes on a RenderTelemetryRecord.
///
/// Each attribute is filled only when currently empty (idempotent: callers may
/// pre-populate fields from setup-time data).  Keeping the population here
/// (rather than relying solely on TelemetryManager::record_run() back-fill)
/// lets setup-time callers describe host context exactly once.  Centralize
/// here so adding a new host attribute (e.g. architecture, NUMA topology,
/// distro version) only needs editing one place.
///
/// `bytes_allocated_peak` is set unconditionally because it is a process-wide
/// gauge (not a missing-field situation): the value is taken at finalize time
/// which is when the render finished, so it is the canonical snapshot.
inline void populate_run_host_attribs(chronon3d::telemetry::RenderTelemetryRecord& run) {
    if (run.os.empty())               run.os               = chronon3d::telemetry::TelemetryManager::get_os_name();
    if (run.cpu_model.empty())        run.cpu_model        = chronon3d::telemetry::TelemetryManager::get_cpu_model();
    if (run.cores == 0)               run.cores            = chronon3d::telemetry::TelemetryManager::get_logical_cores();
    if (run.compiler_info.empty())    run.compiler_info    = chronon3d::telemetry::TelemetryManager::get_compiler_info();
    if (run.build_type.empty())       run.build_type       = chronon3d::telemetry::TelemetryManager::get_build_type();
    if (run.git_commit_short.empty()) run.git_commit_short = chronon3d::telemetry::TelemetryManager::get_git_commit();
    run.bytes_allocated_peak = chronon3d::telemetry::TelemetryManager::get_peak_memory_usage();
}

/// Populates a RenderTelemetryRecord from atomic RenderCounters.
inline void populate_run_metrics(chronon3d::telemetry::RenderTelemetryRecord& run, const chronon3d::RenderCounters& counters) {
    run.pixels_touched = counters.pixels_touched.load(std::memory_order_relaxed);
    run.cache_hits = counters.cache_hits.load(std::memory_order_relaxed);
    run.cache_misses = counters.cache_misses.load(std::memory_order_relaxed);
    run.nodes_executed = counters.nodes_executed.load(std::memory_order_relaxed);
    run.layers_rendered = counters.layers_rendered.load(std::memory_order_relaxed);
    run.text_glyphs_rasterized = counters.text_glyphs_rasterized.load(std::memory_order_relaxed);
    run.images_sampled = counters.images_sampled.load(std::memory_order_relaxed);
    run.blur_pixels = counters.blur_pixels.load(std::memory_order_relaxed);
    run.simd_lerp_calls = counters.simd_lerp_calls.load(std::memory_order_relaxed);
    run.logical_resource_count = counters.logical_resource_count.load(std::memory_order_relaxed);
    run.physical_resource_slot_count = counters.physical_resource_slot_count.load(std::memory_order_relaxed);
    run.logical_resource_bytes = counters.logical_resource_bytes.load(std::memory_order_relaxed);
    run.physical_resource_bytes = counters.physical_resource_bytes.load(std::memory_order_relaxed);
    run.alias_saved_bytes = counters.alias_saved_bytes.load(std::memory_order_relaxed);
    run.alias_reuse_count = counters.alias_reuse_count.load(std::memory_order_relaxed);
    run.new_resource_slot_count = counters.new_resource_slot_count.load(std::memory_order_relaxed);
    run.arena_peak_bytes = counters.arena_peak_bytes.load(std::memory_order_relaxed);
    run.node_cache_hash_collisions = counters.node_cache_hash_collisions.load(std::memory_order_relaxed);
    run.clear_skipped_calls = counters.clear_skipped_calls.load(std::memory_order_relaxed);
    run.clear_skipped_pixels = counters.clear_skipped_pixels.load(std::memory_order_relaxed);
    run.clear_calls = counters.clear_calls.load(std::memory_order_relaxed);
    run.clear_pixels = counters.clear_pixels.load(std::memory_order_relaxed);
    run.clearnode_copy_pixels = counters.clearnode_copy_pixels.load(std::memory_order_relaxed);
    run.composite_copy_pixels = counters.composite_copy_pixels.load(std::memory_order_relaxed);
    run.clearnode_bytes_avoided = counters.clearnode_bytes_avoided.load(std::memory_order_relaxed);
    run.clearnode_memcpy_bytes = counters.clearnode_memcpy_bytes.load(std::memory_order_relaxed);
    run.clearnode_memcpy_calls = counters.clearnode_memcpy_calls.load(std::memory_order_relaxed);
    run.clearnode_detach_shared_count = counters.clearnode_detach_shared_count.load(std::memory_order_relaxed);
    run.clearnode_partial_clip_copy_count = counters.clearnode_partial_clip_copy_count.load(std::memory_order_relaxed);
    run.clearnode_full_clip_skip_count = counters.clearnode_full_clip_skip_count.load(std::memory_order_relaxed);
    run.prev_fb_use_count_sum = counters.prev_fb_use_count_sum.load(std::memory_order_relaxed);
    run.prev_fb_use_count_samples = counters.prev_fb_use_count_samples.load(std::memory_order_relaxed);
    run.prev_fb_use_count_peak = counters.prev_fb_use_count_peak.load(std::memory_order_relaxed);
    run.composite_calls = counters.composite_calls.load(std::memory_order_relaxed);
    run.composite_pixels = counters.composite_pixels.load(std::memory_order_relaxed);
    run.transform_calls = counters.transform_calls.load(std::memory_order_relaxed);
    run.transform_pixels = counters.transform_pixels.load(std::memory_order_relaxed);
    run.effect_stack_calls = counters.effect_stack_calls.load(std::memory_order_relaxed);
    run.effect_pixels = counters.effect_pixels.load(std::memory_order_relaxed);
    run.layer_culling_tests = counters.layer_culling_tests.load(std::memory_order_relaxed);
    run.layers_culled = counters.layers_culled.load(std::memory_order_relaxed);
    run.layers_visible = counters.layers_visible.load(std::memory_order_relaxed);
    run.framebuffer_allocations = counters.framebuffer_allocations.load(std::memory_order_relaxed);
    run.framebuffer_reuses = counters.framebuffer_reuses.load(std::memory_order_relaxed);
    run.framebuffer_bytes_allocated = counters.framebuffer_bytes_allocated.load(std::memory_order_relaxed);
    run.framebuffer_bytes_peak = profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed);
    run.dirty_rect_count = counters.dirty_rect_count.load(std::memory_order_relaxed);
    run.dirty_pixels = counters.dirty_pixels.load(std::memory_order_relaxed);
    run.dirty_union_area_pixels = counters.dirty_union_area_pixels.load(std::memory_order_relaxed);
    run.dirty_full_fallbacks = counters.dirty_full_fallbacks.load(std::memory_order_relaxed);
    run.dirty_full_fallback_predicted_bounds_missing =
        counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::PredictedBoundsMissing)]
            .value.load(std::memory_order_relaxed);
    run.dirty_full_fallback_composite_missing_input_bounds =
        counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::CompositeMissingInputBounds)]
            .value.load(std::memory_order_relaxed);
    run.dirty_full_fallback_transform_bounds_unknown =
        counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::TransformBoundsUnknown)]
            .value.load(std::memory_order_relaxed);
    run.dirty_full_fallback_effect_bounds_unknown =
        counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::EffectBoundsUnknown)]
            .value.load(std::memory_order_relaxed);
    run.framebuffer_acquire_wall_ms = counters.framebuffer_acquire_wall_ms.load(std::memory_order_relaxed);
    run.framebuffer_clear_wall_ms = counters.framebuffer_clear_wall_ms.load(std::memory_order_relaxed);
    run.clearnode_wall_ms = counters.clearnode_wall_ms.load(std::memory_order_relaxed);
    run.clearnode_restore_wall_ms = counters.clearnode_restore_wall_ms.load(std::memory_order_relaxed);
    run.clearnode_restore_rect_count = counters.clearnode_restore_rect_count.load(std::memory_order_relaxed);
    run.clearnode_restore_pixels = counters.clearnode_restore_pixels.load(std::memory_order_relaxed);
    run.clearnode_restore_bytes = counters.clearnode_restore_bytes.load(std::memory_order_relaxed);
    run.clearnode_restore_full_frame_count = counters.clearnode_restore_full_frame_count.load(std::memory_order_relaxed);
    run.clearnode_restore_dirty_rect_count = counters.clearnode_restore_dirty_rect_count.load(std::memory_order_relaxed);
    run.clearnode_restore_noop_count = counters.clearnode_restore_noop_count.load(std::memory_order_relaxed);
    run.framebuffer_pool_clear_wall_ms = counters.framebuffer_pool_clear_wall_ms.load(std::memory_order_relaxed);
    run.framebuffer_enqueue_wall_ms = counters.framebuffer_enqueue_wall_ms.load(std::memory_order_relaxed);
    run.framebuffer_pool_empty_alloc = counters.framebuffer_pool_empty_alloc.load(std::memory_order_relaxed);
    run.framebuffer_pool_best_fit_reuse = counters.framebuffer_pool_best_fit_reuse.load(std::memory_order_relaxed);
    run.framebuffer_pool_exact_hit = counters.framebuffer_pool_exact_hit.load(std::memory_order_relaxed);
    run.framebuffer_buffer_returned_to_pool_count = counters.framebuffer_buffer_returned_to_pool_count.load(std::memory_order_relaxed);
    run.framebuffer_pool_budget_bytes = counters.framebuffer_pool_budget_bytes.load(std::memory_order_relaxed);
    run.framebuffer_pool_retained_bytes = counters.framebuffer_pool_retained_bytes.load(std::memory_order_relaxed);
    run.framebuffer_pool_evicted_count = counters.framebuffer_pool_evicted_count.load(std::memory_order_relaxed);
    run.framebuffer_pool_evicted_bytes = counters.framebuffer_pool_evicted_bytes.load(std::memory_order_relaxed);
    run.framebuffer_pool_pressure_count = counters.framebuffer_pool_pressure_count.load(std::memory_order_relaxed);
    run.framebuffer_pool_size_class_count = counters.framebuffer_pool_size_class_count.load(std::memory_order_relaxed);
    run.unaligned_memory_copies = counters.unaligned_memory_copies.load(std::memory_order_relaxed);
    run.frame_conversion_copy_wall_ms = counters.frame_conversion_copy_wall_ms.load(std::memory_order_relaxed);
    run.video_graph_eval_wall_ms = counters.video_graph_eval_wall_ms.load(std::memory_order_relaxed);
    run.video_conversion_wall_ms = counters.video_conversion_wall_ms.load(std::memory_order_relaxed);
    run.video_pipe_write_wall_ms = counters.video_pipe_write_wall_ms.load(std::memory_order_relaxed);
    run.video_ffmpeg_wait_ms = counters.video_ffmpeg_wait_ms.load(std::memory_order_relaxed);
    run.io_queue_push_wait_ms = counters.io_queue_push_wait_ms.load(std::memory_order_relaxed);
    run.io_queue_pop_wait_ms = counters.io_queue_pop_wait_ms.load(std::memory_order_relaxed);
    run.io_writer_idle_wait_ms = counters.io_writer_idle_wait_ms.load(std::memory_order_relaxed);
    run.io_queue_peak_depth = counters.io_queue_peak_depth.load(std::memory_order_relaxed);
    run.ffmpeg_pipe_write_wall_ms = counters.ffmpeg_pipe_write_wall_ms.load(std::memory_order_relaxed);
    run.converted_frame_cache_hits = counters.converted_frame_cache_hits.load(std::memory_order_relaxed);
    run.program_cache_hits = counters.program_cache_hits.load(std::memory_order_relaxed);
    run.program_cache_misses = counters.program_cache_misses.load(std::memory_order_relaxed);
    run.program_cache_evictions = counters.program_cache_evictions.load(std::memory_order_relaxed);
    run.ffmpeg_flush_wall_ms = counters.ffmpeg_flush_wall_ms.load(std::memory_order_relaxed);

    run.chronon_conversion_copy_ms = counters.frame_conversion_copy_wall_ms.load(std::memory_order_relaxed);

    // Graph Executor Phase Timings
    run.compiled_graph_refresh_wall_ms = counters.compiled_graph_refresh_wall_ms.load(std::memory_order_relaxed);
    run.cache_eval_wall_ms = counters.cache_eval_wall_ms.load(std::memory_order_relaxed);
    run.dirty_eval_wall_ms = counters.dirty_eval_wall_ms.load(std::memory_order_relaxed);
    run.input_resolve_wall_ms = counters.input_resolve_wall_ms.load(std::memory_order_relaxed);
    run.predicted_bbox_wall_ms = counters.predicted_bbox_wall_ms.load(std::memory_order_relaxed);
    run.clone_context_wall_ms = counters.clone_context_wall_ms.load(std::memory_order_relaxed);
    run.state_assign_wall_ms = counters.state_assign_wall_ms.load(std::memory_order_relaxed);
    run.framebuffer_lifetime_wall_ms = counters.framebuffer_lifetime_wall_ms.load(std::memory_order_relaxed);
    run.node_schedule_wall_ms = counters.node_schedule_wall_ms.load(std::memory_order_relaxed);
    run.node_dispatch_wall_ms = counters.node_dispatch_wall_ms.load(std::memory_order_relaxed);
    run.node_execute_actual_wall_ms = counters.node_execute_actual_wall_ms.load(std::memory_order_relaxed);
    run.node_overhead_wall_ms = counters.node_overhead_wall_ms.load(std::memory_order_relaxed);
    run.telemetry_emit_wall_ms = counters.telemetry_emit_wall_ms.load(std::memory_order_relaxed);

    // Setup Deep Dive (cold start diagnostics)
    run.setup_graph_parsing_wall_ms = counters.setup_graph_parsing_wall_ms.load(std::memory_order_relaxed);
    run.setup_asset_io_load_wall_ms = counters.setup_asset_io_load_wall_ms.load(std::memory_order_relaxed);
    run.setup_pool_preallocation_wall_ms = counters.setup_pool_preallocation_wall_ms.load(std::memory_order_relaxed);
    run.image_decode_wall_ms = counters.image_decode_wall_ms.load(std::memory_order_relaxed);

    // OS & Process Diagnostics
    run.process_context_switches_voluntary = counters.process_context_switches_voluntary.load(std::memory_order_relaxed);
    run.process_context_switches_involuntary = counters.process_context_switches_involuntary.load(std::memory_order_relaxed);
    run.os_page_faults_major = counters.os_page_faults_major.load(std::memory_order_relaxed);
    run.os_page_faults_minor = counters.os_page_faults_minor.load(std::memory_order_relaxed);
    run.ffmpeg_cpu_user_pct = counters.ffmpeg_cpu_user_pct.load(std::memory_order_relaxed);
    run.ffmpeg_cpu_sys_pct = counters.ffmpeg_cpu_sys_pct.load(std::memory_order_relaxed);
    run.llc_references = counters.llc_references.load(std::memory_order_relaxed);
    run.llc_misses = counters.llc_misses.load(std::memory_order_relaxed);
}

} // namespace chronon3d::cli::telemetry
