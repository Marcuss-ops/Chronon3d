#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

// TelemetryManager::get_os_name()/get_cpu_model()/etc. are static helpers
// available unconditionally (both SqliteTelemetryStore and NullTelemetryStore
// builds expose them) and are needed by `populate_run_host_attribs()` which
// runs regardless of CHRONON3D_ENABLE_SQLITE_TELEMETRY.  Keep the include
// unconditional.  `record_output_run()` below additionally requires the
// SQLite build; its block is still gated by the same #ifdef.
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include "telemetry_capture.hpp"

#include <string>
#include <vector>

namespace chronon3d::cli::telemetry {

/// Populates host-system attributes on a RenderTelemetryRecord.
///
/// Each attribute is filled only when currently empty (idempotent: callers may
/// pre-populate fields from setup-time data).  This helper exists to keep the
/// JSONL-fallback write path and the SqliteTelemetryStore path describing the
/// *same* host context; without it, runs written via the JSONL-only path
/// would silently miss fields that TelemetryManager::record_run() fills
/// internally.  Centralize here so adding a new host attribute (e.g.
/// architecture, NUMA topology, distro version) only needs editing one place
/// and automatically propagates to both stores.
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

/// Records a complete output run to the telemetry database.
/// This overload accepts all event types.
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
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
                              const std::vector<chronon3d::telemetry::NodeTelemetryRecord>& node_events = {},
                              const chronon3d::RenderCounters* counters_src = nullptr,
                              const std::vector<chronon3d::telemetry::FrameTelemetry>& frames = {},
                              const std::vector<chronon3d::telemetry::LayerTelemetryRecord>& layer_events = {},
                              const std::vector<chronon3d::telemetry::CacheTelemetryRecord>& cache_events = {},
                              const std::vector<chronon3d::telemetry::CullingTelemetryRecord>& culling_events = {},
                              const std::vector<chronon3d::telemetry::ImageTelemetryRecord>& image_events = {},
                              const std::vector<chronon3d::telemetry::RenderArtifactRecord>& artifacts = {}) {
    chronon3d::telemetry::TelemetryManager::instance().initialize_default_stores();

    chronon3d::telemetry::RenderTelemetryRecord run;
    run.run_id = chronon3d::telemetry::TelemetryManager::generate_uuid();
    run.composition_id = composition_id;
    run.output_path = output_path;
    run.success = success;
    run.frames_total = frames_total;
    // A successfully validated video has encoded every requested frame.  A
    // few encoder paths do not expose their final counter, so use the
    // validated total instead of publishing a misleading zero.
    run.frames_written = (success && frames_written == 0 && frames_total > 0)
        ? frames_total
        : frames_written;
    run.wall_time_ms = wall_time_ms;
    run.render_ms = render_ms;
    run.encode_ms = encode_ms;
    run.effective_fps = wall_time_ms > 0.0
        ? (static_cast<double>(run.frames_written) * 1000.0 / wall_time_ms)
        : 0.0;
    run.bytes_allocated_peak = chronon3d::telemetry::TelemetryManager::get_peak_memory_usage();
    run.started_at_iso = started_at_iso;
    run.finished_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    // Idempotent w.r.t. TelemetryManager::record_run() (which also fills these
    // fields internally): calling here means any caller of record_output_run
    // sees a fully-populated record on return, even if they never persist at
    // all — preserving the helper's no-drift invariant for future host attribs.
    populate_run_host_attribs(run);

    // Derive chronon render throughput and FFmpeg breakdown from phases
    for (const auto& phase : phases) {
        if (phase.phase_name == "chronon_render_only_ms") {
            run.chronon_render_only_ms = phase.duration_ms;
        } else if (phase.phase_name == "chronon_conversion_copy_ms") {
            run.chronon_conversion_copy_ms = phase.duration_ms;
        } else if (phase.phase_name == "chronon_queue_wait_ms") {
            run.chronon_queue_wait_ms = phase.duration_ms;
        } else if (phase.phase_name == "ffmpeg_flush_close_ms") {
            run.ffmpeg_flush_close_ms = phase.duration_ms;
        } else if (phase.phase_name == "ffmpeg_encode_total_ms") {
            run.ffmpeg_encode_total_ms = phase.duration_ms;
        } else if (phase.phase_name == "e2e_wall_ms") {
            run.e2e_wall_ms = phase.duration_ms;
        } else if (phase.phase_name == "scene_eval_ms") {
            run.phase_scene_eval_ms = phase.duration_ms;
        } else if (phase.phase_name == "gpu_render_ms") {
            run.phase_gpu_render_ms = phase.duration_ms;
        } else if (phase.phase_name == "gpu_readback_ms") {
            run.phase_gpu_readback_ms = phase.duration_ms;
        } else if (phase.phase_name == "encode_ms") {
            run.phase_encode_ms = phase.duration_ms;
        } else if (phase.phase_name == "disk_io_ms") {
            run.phase_disk_io_ms = phase.duration_ms;
        }
    }
    // Compute chronon_render_throughput_ms as sum of the three Chronon phases
    run.chronon_render_throughput_ms = run.chronon_render_only_ms + run.chronon_conversion_copy_ms + run.chronon_queue_wait_ms;

    if (counters_src) {
        populate_run_metrics(run, *counters_src);
    }
    // Derived per-frame allocation rate (never estimated): the framebuffer
    // allocation event count over the frames actually rendered.
    run.framebuffer_allocations_per_frame = run.frames_total > 0
        ? static_cast<double>(run.framebuffer_allocations) / static_cast<double>(run.frames_total)
        : 0.0;

    const auto resolved_counters = counters.empty() && counters_src
        ? capture_counters(*counters_src)
        : counters;

    // Pool configuration is supplied as a resolved counter by the video
    // exporter.  Mirror it onto the run row so the summary does not report a
    // zero budget when the pool was explicitly bounded.
    for (const auto& counter : resolved_counters) {
        if (counter.counter_name == "gpu_submissions")
            run.gpu_submissions = counter.counter_value;
        else if (counter.counter_name == "passes_executed")
            run.passes_executed = counter.counter_value;
        else if (counter.counter_name == "gpu_submit_cpu_us") {
            run.gpu_submit_cpu_us = counter.counter_value;
            run.gpu_submit_cpu_ms = static_cast<double>(counter.counter_value) / 1000.0;
        } else if (counter.counter_name == "gpu_wait_cpu_us") {
            run.gpu_wait_cpu_us = counter.counter_value;
            run.gpu_wait_cpu_ms = static_cast<double>(counter.counter_value) / 1000.0;
        } else if (counter.counter_name == "gpu_execute_us") {
            run.gpu_execute_ms = static_cast<double>(counter.counter_value) / 1000.0;
        } else if (counter.counter_name == "readback_us") {
            run.gpu_readback_ms = static_cast<double>(counter.counter_value) / 1000.0;
        } else if (counter.counter_name == "gpu_upload_bytes") {
            run.gpu_upload_bytes = counter.counter_value;
        } else if (counter.counter_name == "gpu_readback_bytes") {
            run.gpu_readback_bytes = counter.counter_value;
        } else if (counter.counter_name == "physical_surfaces_peak") {
            run.physical_surfaces_peak = counter.counter_value;
        } else if (counter.counter_name == "barrier_count") {
            run.barrier_count = counter.counter_value;
        }
        else if (counter.counter_name == "framebuffer_pool_budget_bytes")
            run.framebuffer_pool_budget_bytes = counter.counter_value;
        else if (counter.counter_name == "framebuffer_pool_retained_bytes")
            run.framebuffer_pool_retained_bytes = counter.counter_value;
        else if (counter.counter_name == "framebuffer_pool_evicted_count")
            run.framebuffer_pool_evicted_count = counter.counter_value;
        else if (counter.counter_name == "framebuffer_pool_evicted_bytes")
            run.framebuffer_pool_evicted_bytes = counter.counter_value;
        else if (counter.counter_name == "framebuffer_pool_pressure_count")
            run.framebuffer_pool_pressure_count = counter.counter_value;
        else if (counter.counter_name == "framebuffer_pool_size_class_count")
            run.framebuffer_pool_size_class_count = counter.counter_value;
    }

    chronon3d::telemetry::TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    snapshot.frames = frames;
    snapshot.phases = phases;
    snapshot.counters = resolved_counters;
    snapshot.node_events = node_events;
    snapshot.layer_events = layer_events;
    snapshot.cache_events = cache_events;
    snapshot.culling_events = culling_events;
    snapshot.image_events = image_events;
    snapshot.artifacts = artifacts;
    chronon3d::telemetry::TelemetryManager::instance().record_run(snapshot);
}

/// Convenience overload with fewer parameters (backward-compatible).
inline void record_output_run(const std::string& composition_id,
                              const std::string& output_path,
                              bool success,
                              int frames_total,
                              int frames_written,
                              double wall_time_ms,
                              double render_ms,
                              double encode_ms,
                              const std::string& started_at_iso,
                              const std::vector<chronon3d::telemetry::PhaseTelemetryRecord>& phases,
                              const std::vector<chronon3d::telemetry::CounterTelemetryRecord>& counters,
                              const chronon3d::RenderCounters* counters_src,
                              const std::vector<chronon3d::telemetry::FrameTelemetry>& frames) {
    record_output_run(composition_id,
                      output_path,
                      success,
                      frames_total,
                      frames_written,
                      wall_time_ms,
                      render_ms,
                      encode_ms,
                      started_at_iso,
                      phases,
                      counters,
                      {},
                      counters_src,
                      frames,
                      {}, {}, {}, {}, {});
}
#endif // CHRONON3D_ENABLE_SQLITE_TELEMETRY

} // namespace chronon3d::cli::telemetry
