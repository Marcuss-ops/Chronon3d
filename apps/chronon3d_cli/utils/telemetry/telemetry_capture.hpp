#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <string>
#include <utility>
#include <vector>

namespace chronon3d::cli::telemetry {

/// Snapshots all atomic counters into a CounterTelemetryRecord vector.
/// Also enriches with live framebuffer pool stats.
inline std::vector<chronon3d::telemetry::CounterTelemetryRecord> capture_counters(const chronon3d::RenderCounters& counters) {
    std::vector<chronon3d::telemetry::CounterTelemetryRecord> result = {
        {"pixels_touched", counters.pixels_touched.load(std::memory_order_relaxed)},
        {"producer_surface_pixels", counters.producer_surface_pixels.load(std::memory_order_relaxed)},
        {"producer_canvas_pixels", counters.producer_canvas_pixels.load(std::memory_order_relaxed)},
        {"tight_surface_count", counters.tight_surface_count.load(std::memory_order_relaxed)},
        {"full_canvas_overlay_count", counters.full_canvas_overlay_count.load(std::memory_order_relaxed)},
        {"producer_tight_text_count", counters.producer_tight_text_count.load(std::memory_order_relaxed)},
        {"producer_tight_image_count", counters.producer_tight_image_count.load(std::memory_order_relaxed)},
        {"producer_full_frame_text_count", counters.producer_full_frame_text_count.load(std::memory_order_relaxed)},
        {"producer_full_frame_image_count", counters.producer_full_frame_image_count.load(std::memory_order_relaxed)},
        {"cache_hits", counters.cache_hits.load(std::memory_order_relaxed)},
        {"cache_misses", counters.cache_misses.load(std::memory_order_relaxed)},
        {"node_cache_hits", counters.node_cache_hits.load(std::memory_order_relaxed)},
        {"node_cache_misses", counters.node_cache_misses.load(std::memory_order_relaxed)},
        {"image_cache_hits", counters.image_cache_hits.load(std::memory_order_relaxed)},
        {"image_cache_misses", counters.image_cache_misses.load(std::memory_order_relaxed)},
        {"font_cache_hits", counters.font_cache_hits.load(std::memory_order_relaxed)},
        {"font_cache_misses", counters.font_cache_misses.load(std::memory_order_relaxed)},
        {"glyph_cache_hits", counters.glyph_cache_hits.load(std::memory_order_relaxed)},
        {"glyph_cache_misses", counters.glyph_cache_misses.load(std::memory_order_relaxed)},
        {"gpu_asset_cache_hits", counters.gpu_asset_cache_hits.load(std::memory_order_relaxed)},
        {"gpu_asset_cache_misses", counters.gpu_asset_cache_misses.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_repack_count", counters.gpu_text_atlas_repack_count.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_upload_count", counters.gpu_text_atlas_upload_count.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_repack_bytes", counters.gpu_text_atlas_repack_bytes.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_upload_bytes", counters.gpu_text_atlas_upload_bytes.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_cache_hits", counters.gpu_text_atlas_cache_hits.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_cache_misses", counters.gpu_text_atlas_cache_misses.load(std::memory_order_relaxed)},
        {"gpu_text_atlas_key_bytes_hashed", counters.gpu_text_atlas_key_bytes_hashed.load(std::memory_order_relaxed)},
        {"gpu_text_styled_cache_hits", counters.gpu_text_styled_cache_hits.load(std::memory_order_relaxed)},
        {"gpu_text_styled_cache_misses", counters.gpu_text_styled_cache_misses.load(std::memory_order_relaxed)},
        {"gpu_text_instance_upload_count", counters.gpu_text_instance_upload_count.load(std::memory_order_relaxed)},
        {"gpu_text_instance_upload_bytes", counters.gpu_text_instance_upload_bytes.load(std::memory_order_relaxed)},
        {"interop_ring_wait_count", counters.interop_ring_wait_count.load(std::memory_order_relaxed)},
        {"interop_ring_wait_us", counters.interop_ring_wait_us.load(std::memory_order_relaxed)},
        {"cuda_vulkan_wait_count", counters.cuda_vulkan_wait_count.load(std::memory_order_relaxed)},
        {"cuda_vulkan_wait_submit_us", counters.cuda_vulkan_wait_submit_us.load(std::memory_order_relaxed)},
        {"cuda_vulkan_signal_count", counters.cuda_vulkan_signal_count.load(std::memory_order_relaxed)},
        {"cuda_vulkan_signal_submit_us", counters.cuda_vulkan_signal_submit_us.load(std::memory_order_relaxed)},
        {"cuda_composite_frames", counters.cuda_composite_frames.load(std::memory_order_relaxed)},
        {"cuda_composite_wall_us", counters.cuda_composite_wall_us.load(std::memory_order_relaxed)},
        {"cuda_encode_queue_peak", counters.cuda_encode_queue_peak.load(std::memory_order_relaxed)},
        {"cuda_encode_event_wait_count", counters.cuda_encode_event_wait_count.load(std::memory_order_relaxed)},
        {"cuda_encode_event_wait_us", counters.cuda_encode_event_wait_us.load(std::memory_order_relaxed)},
        {"video_decode_frames", counters.video_decode_frames.load(std::memory_order_relaxed)},
        {"video_decode_hw_frames", counters.video_decode_hw_frames.load(std::memory_order_relaxed)},
        {"video_decode_hw_transfer_wall_ms", counters.video_decode_hw_transfer_wall_ms.load(std::memory_order_relaxed)},
        {"video_decode_wall_ms", counters.video_decode_wall_ms.load(std::memory_order_relaxed)},
        {"video_decode_sws_wall_ms", counters.video_decode_sws_wall_ms.load(std::memory_order_relaxed)},
        {"video_surface_upload_count", counters.video_surface_upload_count.load(std::memory_order_relaxed)},
        {"video_surface_upload_bytes", counters.video_surface_upload_bytes.load(std::memory_order_relaxed)},
        {"video_surface_upload_wall_ms", counters.video_surface_upload_wall_ms.load(std::memory_order_relaxed)},
        {"gpu_native_surface_frames", counters.gpu_native_surface_frames.load(std::memory_order_relaxed)},
        {"gpu_native_encode_frames", counters.gpu_native_encode_frames.load(std::memory_order_relaxed)},
        {"gpu_surface_copy_frames", counters.gpu_surface_copy_frames.load(std::memory_order_relaxed)},
        {"cpu_pixel_readback_count", counters.cpu_pixel_readback_count.load(std::memory_order_relaxed)},
        {"cpu_pixel_readback_bytes", counters.cpu_pixel_readback_bytes.load(std::memory_order_relaxed)},
        {"encoder_staging_copy_bytes", counters.encoder_staging_copy_bytes.load(std::memory_order_relaxed)},
        {"encoder_submit_cpu_ms", counters.encoder_submit_cpu_ms.load(std::memory_order_relaxed)},
        {"encoder_backpressure_wait_ms", counters.encoder_backpressure_wait_ms.load(std::memory_order_relaxed)},
        {"encoder_flush_wall_ms", counters.encoder_flush_wall_ms.load(std::memory_order_relaxed)},
        {"mux_finalize_wall_ms", counters.mux_finalize_wall_ms.load(std::memory_order_relaxed)},
        {"pipe_write_wall_ms", counters.pipe_write_wall_ms.load(std::memory_order_relaxed)},
        {"nodes_executed", counters.nodes_executed.load(std::memory_order_relaxed)},
        {"layers_rendered", counters.layers_rendered.load(std::memory_order_relaxed)},
        {"text_glyphs_rasterized", counters.text_glyphs_rasterized.load(std::memory_order_relaxed)},
        {"text_shaping_calls", counters.text_shaping_calls.load(std::memory_order_relaxed)},
        {"text_shaping_wall_ms", counters.text_shaping_wall_ms.load(std::memory_order_relaxed)},
        {"text_bidi_wall_ms", counters.text_bidi_wall_ms.load(std::memory_order_relaxed)},
        {"images_sampled", counters.images_sampled.load(std::memory_order_relaxed)},
        {"blur_pixels", counters.blur_pixels.load(std::memory_order_relaxed)},
        {"simd_lerp_calls", counters.simd_lerp_calls.load(std::memory_order_relaxed)},
        {"tiles_total", counters.tiles_total.load(std::memory_order_relaxed)},
        {"tiles_hit", counters.tiles_hit.load(std::memory_order_relaxed)},
        {"tiles_miss", counters.tiles_miss.load(std::memory_order_relaxed)},
        {"tiles_partial", counters.tiles_partial.load(std::memory_order_relaxed)},
        {"node_cache_hash_collisions", counters.node_cache_hash_collisions.load(std::memory_order_relaxed)},
        {"graph_resolve_layers_wall_ms", counters.graph_resolve_layers_wall_ms.load(std::memory_order_relaxed)},
        {"graph_dirty_rect_wall_ms", counters.graph_dirty_rect_wall_ms.load(std::memory_order_relaxed)},
        {"graph_build_wall_ms", counters.graph_build_wall_ms.load(std::memory_order_relaxed)},
        {"graph_execute_wall_ms", counters.graph_execute_wall_ms.load(std::memory_order_relaxed)},
        {"graph_total_wall_ms", counters.graph_total_wall_ms.load(std::memory_order_relaxed)},
        {"compiled_graph_refresh_wall_ms", counters.compiled_graph_refresh_wall_ms.load(std::memory_order_relaxed)},
        {"cache_eval_wall_ms", counters.cache_eval_wall_ms.load(std::memory_order_relaxed)},
        {"dirty_eval_wall_ms", counters.dirty_eval_wall_ms.load(std::memory_order_relaxed)},
        {"input_resolve_wall_ms", counters.input_resolve_wall_ms.load(std::memory_order_relaxed)},
        {"framebuffer_lifetime_wall_ms", counters.framebuffer_lifetime_wall_ms.load(std::memory_order_relaxed)},
        {"node_schedule_wall_ms", counters.node_schedule_wall_ms.load(std::memory_order_relaxed)},
        {"node_dispatch_wall_ms", counters.node_dispatch_wall_ms.load(std::memory_order_relaxed)},
        {"node_execute_actual_wall_ms", counters.node_execute_actual_wall_ms.load(std::memory_order_relaxed)},
        {"node_overhead_wall_ms", counters.node_overhead_wall_ms.load(std::memory_order_relaxed)},
        {"level_parallel_count", counters.level_parallel_count.load(std::memory_order_relaxed)},
        {"level_sequential_count", counters.level_sequential_count.load(std::memory_order_relaxed)},
        {"telemetry_emit_wall_ms", counters.telemetry_emit_wall_ms.load(std::memory_order_relaxed)},
        {"predicted_bbox_wall_ms", counters.predicted_bbox_wall_ms.load(std::memory_order_relaxed)},
        {"clone_context_wall_ms", counters.clone_context_wall_ms.load(std::memory_order_relaxed)},
        {"state_assign_wall_ms", counters.state_assign_wall_ms.load(std::memory_order_relaxed)},
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
        {"effect_stack_total_wall_ms", counters.effect_stack_total_wall_ms.load(std::memory_order_relaxed)},
        {"dof_roi_analysis_wall_us", counters.dof_roi_analysis_wall_us.load(std::memory_order_relaxed)},
        {"dof_blur_radius_generation_wall_us", counters.dof_blur_radius_generation_wall_us.load(std::memory_order_relaxed)},
        {"dof_scratch_allocation_wall_us", counters.dof_scratch_allocation_wall_us.load(std::memory_order_relaxed)},
        {"dof_copy_to_hpass_wall_us", counters.dof_copy_to_hpass_wall_us.load(std::memory_order_relaxed)},
        {"dof_horizontal_pass_wall_us", counters.dof_horizontal_pass_wall_us.load(std::memory_order_relaxed)},
        {"dof_hpass_to_output_wall_us", counters.dof_hpass_to_output_wall_us.load(std::memory_order_relaxed)},
        {"dof_vertical_pass_wall_us", counters.dof_vertical_pass_wall_us.load(std::memory_order_relaxed)},
        {"dof_writeback_wall_us", counters.dof_writeback_wall_us.load(std::memory_order_relaxed)},
        {"dof_roi_pixels", counters.dof_roi_pixels.load(std::memory_order_relaxed)},
        {"dof_blur_source_pixels", counters.dof_blur_source_pixels.load(std::memory_order_relaxed)},
        {"dof_max_radius_milli", counters.dof_max_radius_milli.load(std::memory_order_relaxed)},
        {"dof_scratch_bytes", counters.dof_scratch_bytes.load(std::memory_order_relaxed)},
        {"dof_estimated_bytes_read", counters.dof_estimated_bytes_read.load(std::memory_order_relaxed)},
        {"dof_estimated_bytes_written", counters.dof_estimated_bytes_written.load(std::memory_order_relaxed)},
        {"layer_culling_tests", counters.layer_culling_tests.load(std::memory_order_relaxed)},
        {"layers_culled", counters.layers_culled.load(std::memory_order_relaxed)},
        {"layers_visible", counters.layers_visible.load(std::memory_order_relaxed)},
        {"framebuffer_allocations", counters.framebuffer_allocations.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_text", counters.framebuffer_alloc_text.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_effect", counters.framebuffer_alloc_effect.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_glow", counters.framebuffer_alloc_glow.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_video", counters.framebuffer_alloc_video.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_graph", counters.framebuffer_alloc_graph.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_scratch", counters.framebuffer_alloc_scratch.load(std::memory_order_relaxed)},
        {"framebuffer_alloc_unknown", counters.framebuffer_alloc_unknown.load(std::memory_order_relaxed)},
        {"framebuffer_reuses", counters.framebuffer_reuses.load(std::memory_order_relaxed)},
        {"framebuffer_bytes_allocated", counters.framebuffer_bytes_allocated.load(std::memory_order_relaxed)},
        {"framebuffer_bytes_peak", profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed)},
        {"dirty_rect_count", counters.dirty_rect_count.load(std::memory_order_relaxed)},
        {"dirty_pixels", counters.dirty_pixels.load(std::memory_order_relaxed)},
        {"dirty_full_fallbacks", counters.dirty_full_fallbacks.load(std::memory_order_relaxed)},
        {"dirty_full_fallback_predicted_bounds_missing",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::PredictedBoundsMissing)]
                .value.load(std::memory_order_relaxed)},
        {"dirty_full_fallback_composite_missing_input_bounds",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::CompositeMissingInputBounds)]
                .value.load(std::memory_order_relaxed)},
        {"dirty_full_fallback_transform_bounds_unknown",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::TransformBoundsUnknown)]
                .value.load(std::memory_order_relaxed)},
        {"dirty_full_fallback_effect_bounds_unknown",
            counters.dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::EffectBoundsUnknown)]
                .value.load(std::memory_order_relaxed)},
        {"framebuffer_acquire_wall_ms", counters.framebuffer_acquire_wall_ms.load(std::memory_order_relaxed)},
        {"framebuffer_clear_wall_ms", counters.framebuffer_clear_wall_ms.load(std::memory_order_relaxed)},
        {"clearnode_wall_ms", counters.clearnode_wall_ms.load(std::memory_order_relaxed)},
        {"clearnode_restore_wall_ms", counters.clearnode_restore_wall_ms.load(std::memory_order_relaxed)},
        {"clearnode_restore_rect_count", counters.clearnode_restore_rect_count.load(std::memory_order_relaxed)},
        {"clearnode_restore_pixels", counters.clearnode_restore_pixels.load(std::memory_order_relaxed)},
        {"clearnode_restore_bytes", counters.clearnode_restore_bytes.load(std::memory_order_relaxed)},
        {"clearnode_restore_full_frame_count", counters.clearnode_restore_full_frame_count.load(std::memory_order_relaxed)},
        {"clearnode_restore_dirty_rect_count", counters.clearnode_restore_dirty_rect_count.load(std::memory_order_relaxed)},
        {"clearnode_restore_noop_count", counters.clearnode_restore_noop_count.load(std::memory_order_relaxed)},
        {"clearnode_memcpy_wall_ms", counters.clearnode_memcpy_wall_ms.load(std::memory_order_relaxed)},
        {"clearnode_acquire_wall_ms", counters.clearnode_acquire_wall_ms.load(std::memory_order_relaxed)},
        {"clearnode_clear_wall_ms", counters.clearnode_clear_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_blend_wall_ms", counters.compositenode_blend_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_setup_wall_ms", counters.compositenode_setup_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_copy_wall_ms", counters.compositenode_copy_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_row_wall_ms", counters.compositenode_row_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_acquire_wall_ms", counters.compositenode_acquire_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_dispatch_wall_ms", counters.compositenode_dispatch_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_overhead_wall_ms", counters.compositenode_overhead_wall_ms.load(std::memory_order_relaxed)},
        {"compositenode_internal_wall_us", counters.compositenode_internal_wall_us.load(std::memory_order_relaxed)},
        {"framebuffer_pool_clear_wall_ms", counters.framebuffer_pool_clear_wall_ms.load(std::memory_order_relaxed)},
        {"framebuffer_enqueue_wall_ms", counters.framebuffer_enqueue_wall_ms.load(std::memory_order_relaxed)},
        {"framebuffer_pool_empty_alloc", counters.framebuffer_pool_empty_alloc.load(std::memory_order_relaxed)},
        {"framebuffer_pool_best_fit_reuse", counters.framebuffer_pool_best_fit_reuse.load(std::memory_order_relaxed)},
        {"framebuffer_pool_exact_hit", counters.framebuffer_pool_exact_hit.load(std::memory_order_relaxed)},
        {"framebuffer_buffer_returned_to_pool_count", counters.framebuffer_buffer_returned_to_pool_count.load(std::memory_order_relaxed)},
        {"framebuffer_prealloc_created", counters.framebuffer_prealloc_created.load(std::memory_order_relaxed)},
        {"unaligned_memory_copies", counters.unaligned_memory_copies.load(std::memory_order_relaxed)},
        {"frame_conversion_copy_wall_ms", counters.frame_conversion_copy_wall_ms.load(std::memory_order_relaxed)},
        {"video_graph_eval_wall_ms", counters.video_graph_eval_wall_ms.load(std::memory_order_relaxed)},
        {"video_conversion_wall_ms", counters.video_conversion_wall_ms.load(std::memory_order_relaxed)},
        {"video_pipe_write_wall_ms", counters.video_pipe_write_wall_ms.load(std::memory_order_relaxed)},
        {"video_ffmpeg_wait_ms", counters.video_ffmpeg_wait_ms.load(std::memory_order_relaxed)},
        {"io_queue_push_wait_ms", counters.io_queue_push_wait_ms.load(std::memory_order_relaxed)},
        {"io_queue_pop_wait_ms", counters.io_queue_pop_wait_ms.load(std::memory_order_relaxed)},
        {"io_writer_idle_wait_ms", counters.io_writer_idle_wait_ms.load(std::memory_order_relaxed)},
        {"io_queue_peak_depth", counters.io_queue_peak_depth.load(std::memory_order_relaxed)},
        {"ffmpeg_pipe_write_wall_ms", counters.ffmpeg_pipe_write_wall_ms.load(std::memory_order_relaxed)},
        {"converted_frame_cache_hits",  counters.converted_frame_cache_hits.load(std::memory_order_relaxed)},
        {"converted_frame_cache_misses", counters.converted_frame_cache_misses.load(std::memory_order_relaxed)},
        {"program_cache_hits",     counters.program_cache_hits.load(std::memory_order_relaxed)},
        {"program_cache_misses",   counters.program_cache_misses.load(std::memory_order_relaxed)},
        {"program_cache_evictions", counters.program_cache_evictions.load(std::memory_order_relaxed)},
        {"program_cache_capacity",  counters.program_cache_capacity.load(std::memory_order_relaxed)},
        {"program_cache_tune",       counters.program_cache_tune.load(std::memory_order_relaxed)},
        {"ffmpeg_flush_wall_ms", counters.ffmpeg_flush_wall_ms.load(std::memory_order_relaxed)},
        {"encoder_submit_cpu_ms", counters.encoder_submit_cpu_ms.load(std::memory_order_relaxed)},
        {"encoder_backpressure_wait_ms", counters.encoder_backpressure_wait_ms.load(std::memory_order_relaxed)},
        {"encoder_flush_wall_ms", counters.encoder_flush_wall_ms.load(std::memory_order_relaxed)},
        {"mux_finalize_wall_ms", counters.mux_finalize_wall_ms.load(std::memory_order_relaxed)},
        {"pixel_format_convert_wall_ms", counters.pixel_format_convert_wall_ms.load(std::memory_order_relaxed)},
        {"color_space_convert_wall_ms", counters.color_space_convert_wall_ms.load(std::memory_order_relaxed)},
        {"pipe_write_cpu_ms", counters.pipe_write_cpu_ms.load(std::memory_order_relaxed)},
        {"pipe_write_wall_ms", counters.pipe_write_wall_ms.load(std::memory_order_relaxed)},
        {"pipe_write_cpu_wall_us", counters.pipe_write_cpu_wall_us.load(std::memory_order_relaxed)},
        {"pipe_backpressure_wait_wall_us", counters.pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed)},
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
        {"graph_executed_wall_ms_sum", counters.graph_executed_wall_ms_sum.load(std::memory_order_relaxed)},
        {"graph_skipped_wall_ms_sum", counters.graph_skipped_wall_ms_sum.load(std::memory_order_relaxed)},
        {"framebuffer_pool_capacity", 0},
        {"framebuffer_pool_available_count", 0},
        {"framebuffer_pool_current_bytes", 0},
        {"framebuffer_pool_total_allocations", 0},
        {"framebuffer_pool_total_reuses", 0},
        {"framebuffer_pool_budget_bytes", 0},
        {"framebuffer_pool_retained_bytes", 0},
        {"framebuffer_pool_evicted_count", 0},
        {"framebuffer_pool_evicted_bytes", 0},
        {"framebuffer_pool_pressure_count", 0},
        {"framebuffer_pool_size_class_count", 0},
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
            } else if (counter.counter_name == "framebuffer_pool_budget_bytes") {
                counter.counter_value = pool_stats.budget_bytes;
            } else if (counter.counter_name == "framebuffer_pool_retained_bytes") {
                counter.counter_value = pool_stats.retained_bytes;
            } else if (counter.counter_name == "framebuffer_pool_evicted_count") {
                counter.counter_value = pool_stats.evicted_count;
            } else if (counter.counter_name == "framebuffer_pool_evicted_bytes") {
                counter.counter_value = pool_stats.evicted_bytes;
            } else if (counter.counter_name == "framebuffer_pool_pressure_count") {
                counter.counter_value = pool_stats.pressure_count;
            } else if (counter.counter_name == "framebuffer_pool_size_class_count") {
                counter.counter_value = pool_stats.size_class_count;
            }
        }
    }

    return result;
}

/// Appends backend-exported GPU counters (submit/wait/readback/upload)
/// to the counter list and mirrors the values onto the canonical run record.
/// Software backends leave the list untouched (their export is the default
/// no-op), so a software run reports zero for GPU-only metrics, as intended.
inline void capture_backend_gpu_counters(
    const chronon3d::graph::RenderBackend& backend,
    std::vector<chronon3d::telemetry::CounterTelemetryRecord>& counters,
    chronon3d::telemetry::RenderTelemetryRecord& run) {
    std::vector<std::pair<std::string, std::uint64_t>> gpu;
    backend.export_gpu_telemetry_counters(gpu);
    for (const auto& [name, value] : gpu) {
        counters.push_back({name, value});
        if (name == "gpu_submissions") {
            run.gpu_submissions = value;
        } else if (name == "passes_executed") {
            run.passes_executed = value;
        } else if (name == "gpu_submit_cpu_us") {
            run.gpu_submit_cpu_us = value;
            run.gpu_submit_cpu_ms = static_cast<double>(value) / 1000.0;
        } else if (name == "gpu_wait_cpu_us") {
            run.gpu_wait_cpu_us = value;
            run.gpu_wait_cpu_ms = static_cast<double>(value) / 1000.0;
        } else if (name == "gpu_execute_us") {
            run.gpu_execute_ms = static_cast<double>(value) / 1000.0;
        } else if (name == "readback_us") {
            run.gpu_readback_ms = static_cast<double>(value) / 1000.0;
        } else if (name == "gpu_upload_bytes") {
            run.gpu_upload_bytes = value;
        } else if (name == "gpu_readback_bytes") {
            run.gpu_readback_bytes = value;
        } else if (name == "physical_surfaces_peak") {
            run.physical_surfaces_peak = value;
        }
    }
}

/// Captures non-zero graph phase timings from counters as PhaseTelemetryRecords.
inline std::vector<chronon3d::telemetry::PhaseTelemetryRecord> capture_graph_phase_records(const chronon3d::RenderCounters& counters) {
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;
    const auto add_phase = [&](const char* name, uint64_t value) {
        if (value > 0) {
            phases.push_back({name, static_cast<double>(value)});
        }
    };

    add_phase("graph_resolve_layers_wall_ms", counters.graph_resolve_layers_wall_ms.load(std::memory_order_relaxed));
    add_phase("graph_dirty_rect_wall_ms", counters.graph_dirty_rect_wall_ms.load(std::memory_order_relaxed));
    add_phase("graph_build_wall_ms", counters.graph_build_wall_ms.load(std::memory_order_relaxed));
    add_phase("graph_execute_wall_ms", counters.graph_execute_wall_ms.load(std::memory_order_relaxed));
    add_phase("graph_total_wall_ms", counters.graph_total_wall_ms.load(std::memory_order_relaxed));
    add_phase("compiled_graph_refresh_wall_ms", counters.compiled_graph_refresh_wall_ms.load(std::memory_order_relaxed));
    add_phase("cache_eval_wall_ms", counters.cache_eval_wall_ms.load(std::memory_order_relaxed));
    add_phase("dirty_eval_wall_ms", counters.dirty_eval_wall_ms.load(std::memory_order_relaxed));
    add_phase("input_resolve_wall_ms", counters.input_resolve_wall_ms.load(std::memory_order_relaxed));
    add_phase("framebuffer_lifetime_wall_ms", counters.framebuffer_lifetime_wall_ms.load(std::memory_order_relaxed));
    add_phase("node_schedule_wall_ms", counters.node_schedule_wall_ms.load(std::memory_order_relaxed));
    add_phase("node_dispatch_wall_ms", counters.node_dispatch_wall_ms.load(std::memory_order_relaxed));
    add_phase("telemetry_emit_wall_ms", counters.telemetry_emit_wall_ms.load(std::memory_order_relaxed));
    const auto add_us_phase = [&](const char* name, uint64_t value) {
        if (value > 0) phases.push_back({name, static_cast<double>(value) / 1000.0});
    };
    add_us_phase("dof_roi_analysis_ms", counters.dof_roi_analysis_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_blur_radius_generation_ms", counters.dof_blur_radius_generation_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_scratch_allocation_ms", counters.dof_scratch_allocation_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_copy_to_hpass_ms", counters.dof_copy_to_hpass_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_horizontal_pass_ms", counters.dof_horizontal_pass_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_hpass_to_output_ms", counters.dof_hpass_to_output_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_vertical_pass_ms", counters.dof_vertical_pass_wall_us.load(std::memory_order_relaxed));
    add_us_phase("dof_writeback_ms", counters.dof_writeback_wall_us.load(std::memory_order_relaxed));
    return phases;
}

} // namespace chronon3d::cli::telemetry
