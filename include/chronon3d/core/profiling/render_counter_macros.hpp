#pragma once

// ============================================================================
// render_counter_macros.hpp — Domain-grouped counter X-macros
// ============================================================================
//
// Houses the X(name)-emission macros that define each counter field of
// RenderCounters / RenderCountersRaw.  Split out of counters.hpp (FASE 21)
// so that the umbrella entry (counters.hpp) can be navigated quickly while
// the macro vocabulary lives in its own dedicated header.
//
// Each CHRONON_COUNTERS_<DOMAIN>(X) macro emits X(name) for every counter
// of a given domain.  Consumers needing only a subset include this header
// and use the per-domain macro directly; consumers needing the full set
// include the combined umbrella CHRONON_RENDER_COUNTERS(X).
//
// SYSTEM + SETUP are intentionally EXCLUDED from the umbrella: they are
// written by a separate sampler (low-frequency, OS/syscall driven) and
// must not contribute to per-frame hot-loop counter writes.
// ============================================================================

namespace chronon3d {

#define CHRONON_COUNTERS_CORE(X) \
    X(pixels_touched) \
    X(nodes_executed) \
    X(layers_rendered) \
    X(images_sampled) \
    X(blur_pixels) \
    X(simd_lerp_calls) \
    X(logical_resource_count) \
    X(physical_resource_slot_count) \
    X(logical_resource_bytes) \
    X(physical_resource_bytes) \
    X(alias_saved_bytes) \
    X(alias_reuse_count) \
    X(new_resource_slot_count) \
    X(arena_peak_bytes)

#define CHRONON_COUNTERS_CACHE(X) \
    X(cache_hits) \
    X(cache_misses) \
    X(node_cache_hash_collisions) \
    X(node_cache_hits) \
    X(node_cache_misses) \
    X(node_cache_lookup_wall_us) \
    X(image_cache_hits) \
    X(image_cache_misses) \
    X(font_cache_hits) \
    X(font_cache_misses) \
    X(glyph_cache_hits) \
    X(glyph_cache_misses) \
    X(gpu_asset_cache_hits) \
    X(gpu_asset_cache_misses) \
    X(glow_cache_hits) \
    X(glow_cache_misses) \
    X(converted_frame_cache_hits) \
    X(converted_frame_cache_misses) \
    X(program_cache_hits) \
    X(program_cache_misses) \
    X(program_cache_evictions)

#define CHRONON_COUNTERS_IMAGE(X) \
    X(image_resolve_wall_us) \
    X(image_decode_wall_us) \
    X(image_convert_wall_us) \
    X(image_upload_wall_us) \
    X(image_draw_wall_us) \
    X(image_decode_count) \
    X(image_upload_count) \
    X(image_draw_count)

#define CHRONON_COUNTERS_TEXT(X) \
    X(text_glyphs_rasterized) \
    X(text_cache_hits) \
    X(text_cache_misses) \
    X(text_layout_wall_ms) \
    X(text_rasterization_wall_ms) \
    X(text_shaping_calls) \
    X(text_shaping_wall_ms) \
    X(text_bidi_wall_ms) \
    X(font_resolve_wall_us) \
    X(glyph_cache_lookup_wall_us) \
    X(glyph_atlas_upload_wall_us) \
    X(text_draw_wall_us) \
    X(text_shadow_cache_hits) \
    X(text_shadow_cache_misses) \
    X(text_glow_cache_hits) \
    X(text_glow_cache_misses) \
    X(glyph_atlas_hits) \
    X(glyph_atlas_misses) \
    X(glyph_atlas_stored) \
    X(text_bbox_contract_violations)  /**< TICKET-TEXT-CLIP-PREDICTED-BBOX: incremented when predicted_bbox() returns empty or non-finite */

#define CHRONON_COUNTERS_TILE(X) \
    X(tiles_total) \
    X(tiles_hit) \
    X(tiles_miss) \
    X(tiles_partial) \
    X(tile_dirty_count) \
    X(tile_clean_count) \
    X(tile_pixels_rendered) \
    X(tile_pixels_skipped) \
    X(tile_full_fallbacks) \
    X(tile_regions_executed) \
    X(tile_region_pixels) \
    X(tile_execution_wall_ms)

#define CHRONON_COUNTERS_COMPOSITE(X) \
    X(clear_skipped_calls) \
    X(clear_skipped_pixels) \
    X(clear_calls) \
    X(clear_pixels) \
    X(clearnode_copy_pixels) \
    X(composite_copy_pixels) \
    X(clearnode_bytes_avoided) \
    X(clearnode_memcpy_bytes) \
    X(clearnode_memcpy_calls) \
    X(clearnode_detach_shared_count) \
    X(clearnode_partial_clip_copy_count) \
    X(clearnode_full_clip_skip_count) \
    X(prev_fb_use_count_sum) \
    X(prev_fb_use_count_samples) \
    X(prev_fb_use_count_peak) \
    X(composite_calls) \
    X(composite_pixels) \
    X(clearnode_wall_ms) \
    X(clearnode_memcpy_wall_ms) \
    X(clearnode_restore_wall_ms) \
    X(clearnode_acquire_wall_ms) \
    X(clearnode_clear_wall_ms) \
    X(clearnode_restore_rect_count) \
    X(clearnode_restore_pixels) \
    X(clearnode_restore_bytes) \
    X(clearnode_restore_full_frame_count) \
    X(clearnode_restore_dirty_rect_count) \
    X(clearnode_restore_noop_count) \
    X(compositenode_blend_wall_ms) \
    X(compositenode_setup_wall_ms) \
    X(compositenode_copy_wall_ms) \
    X(compositenode_row_wall_ms) \
    X(compositenode_dispatch_wall_ms) \
    X(compositenode_acquire_wall_ms) \
    X(compositenode_overhead_wall_ms) \
    X(compositenode_internal_wall_us)

#define CHRONON_COUNTERS_TRANSFORM(X) \
    X(transform_calls) \
    X(transform_pixels) \
    X(projected_winding_flips)

#define CHRONON_COUNTERS_EFFECTS(X) \
    X(effect_stack_calls) \
    X(effect_pixels) \
    X(effect_stack_total_wall_ms) \
    X(dof_roi_analysis_wall_us) \
    X(dof_blur_radius_generation_wall_us) \
    X(dof_scratch_allocation_wall_us) \
    X(dof_copy_to_hpass_wall_us) \
    X(dof_horizontal_pass_wall_us) \
    X(dof_hpass_to_output_wall_us) \
    X(dof_vertical_pass_wall_us) \
    X(dof_writeback_wall_us) \
    X(dof_roi_pixels) \
    X(dof_blur_source_pixels) \
    X(dof_max_radius_milli) \
    X(dof_scratch_bytes) \
    X(dof_estimated_bytes_read) \
    X(dof_estimated_bytes_written) \
    X(effect_focus_in_ladder_precompute_wall_ms) \
    X(effect_focus_in_ladder_crossfade_wall_ms)

#define CHRONON_COUNTERS_FRAMEBUFFER(X) \
    X(framebuffer_allocations) \
    X(framebuffer_reuses) \
    X(framebuffer_bytes_allocated) \
    X(framebuffer_bytes_peak) \
    X(framebuffer_pool_clear_wall_ms) \
    X(framebuffer_enqueue_wall_ms) \
    X(framebuffer_pool_empty_alloc) \
    X(framebuffer_pool_best_fit_reuse) \
    X(framebuffer_pool_exact_hit) \
    X(framebuffer_buffer_returned_to_pool_count) \
    X(framebuffer_prealloc_created) \
    X(framebuffer_copy_wall_ms) \
    X(framebuffer_copy_parallel_calls) \
    X(unaligned_memory_copies) \
    X(framebuffer_pool_budget_bytes) \
    X(framebuffer_pool_retained_bytes) \
    X(framebuffer_pool_evicted_count) \
    X(framebuffer_pool_evicted_bytes) \
    X(framebuffer_pool_pressure_count) \
    X(framebuffer_pool_size_class_count)

#define CHRONON_COUNTERS_DIRTY(X) \
    X(dirty_rect_count) \
    X(dirty_pixels) \
    X(dirty_union_area_pixels) \
    X(dirty_full_fallbacks) \
    X(scroll_opt_copy_wall_ms)

#define CHRONON_COUNTERS_LAYER(X) \
    X(layer_culling_tests) \
    X(layers_culled) \
    X(layers_visible)

#define CHRONON_COUNTERS_GRAPH(X) \
    X(graph_cache_hits) \
    X(graph_cache_misses) \
    X(graph_resolve_layers_wall_ms) \
    X(graph_dirty_rect_wall_ms) \
    X(graph_build_wall_ms) \
    X(graph_execute_wall_ms) \
    X(graph_total_wall_ms) \
    X(timeline_eval_wall_ms) \
    X(pixel_fusion_passes_before) \
    X(pixel_fusion_passes_after) \
    X(pixel_fusion_bytes_saved) \
    X(full_frame_passes) \
    X(full_frame_copies) \
    X(compiled_graph_refresh_wall_ms) \
    X(cache_eval_wall_ms) \
    X(dirty_eval_wall_ms) \
    X(input_resolve_wall_ms) \
    X(framebuffer_lifetime_wall_ms) \
    X(node_schedule_wall_ms) \
    X(node_dispatch_wall_ms) \
    X(node_execute_actual_wall_ms) \
    X(node_overhead_wall_ms) \
    X(level_parallel_count) \
    X(level_sequential_count) \
    X(telemetry_emit_wall_ms) \
    X(predicted_bbox_wall_ms) \
    X(clone_context_wall_ms) \
    X(state_assign_wall_ms) \
    X(bypass_not_cacheable_count) \
    X(nodes_skipped) \
    X(framebuffer_acquire_wall_ms) \
    X(framebuffer_clear_wall_ms) \
    X(graph_executed_frames) \
    X(graph_skipped_frames) \
    X(graph_executed_wall_ms_sum) \
    X(graph_skipped_wall_ms_sum)

#define CHRONON_COUNTERS_VIDEO(X) \
    X(frame_conversion_copy_wall_ms) \
    X(video_graph_eval_wall_ms) \
    X(video_conversion_wall_ms) \
    X(video_pipe_write_wall_ms) \
    X(video_ffmpeg_wait_ms) \
    X(io_queue_push_wait_ms) \
    X(io_queue_pop_wait_ms) \
    X(io_writer_idle_wait_ms) \
    X(io_queue_peak_depth) \
    X(io_queue_peak_bytes) \
    X(ffmpeg_pipe_write_wall_ms) \
    X(ffmpeg_flush_wall_ms) \
    X(native_av_convert_wall_ms) \
    X(native_av_receive_packet_wall_ms) \
    X(native_av_mux_write_wall_ms) \
    X(native_av_convert_skipped_wall_ms) \
    X(video_sink_type_id) \
    X(video_frames_submitted) \
    X(video_frames_converted) \
    X(video_frames_written_counter) \
    X(video_frames_dropped) \
    X(video_convert_only_wall_ms) \
    X(video_pipe_write_only_wall_ms) \
    X(video_writer_wait_ms) \
    X(conversion_bytes_written) \
    X(encoder_staging_copy_bytes) \
    X(encoder_slots_allocated) /* gauge: current pool capacity */ \
    X(encoder_slot_reuses) /* gauge: cumulative pool reuse events */ \
    X(frame_conversion_wall_ms) \
    X(frame_submit_wall_ms) \
    X(color_pipeline_batches) \
    X(color_pipeline_effects) \
    X(encoder_submit_cpu_ms) \
    X(encoder_backpressure_wait_ms) \
    X(encoder_flush_wall_ms) \
    X(mux_finalize_wall_ms) \
    X(pixel_format_convert_wall_ms) \
    X(color_space_convert_wall_ms) \
    X(pipe_write_cpu_ms) \
    X(pipe_write_wall_ms) \
    X(pipe_write_cpu_wall_us) \
    X(pipe_backpressure_wait_wall_us)

// ── Combined umbrella (backward-compatible) ─────────────────────────────────

#define CHRONON_RENDER_COUNTERS(X) \
    CHRONON_COUNTERS_CORE(X) \
    CHRONON_COUNTERS_CACHE(X) \
    CHRONON_COUNTERS_IMAGE(X) \
    CHRONON_COUNTERS_TEXT(X) \
    CHRONON_COUNTERS_TILE(X) \
    CHRONON_COUNTERS_COMPOSITE(X) \
    CHRONON_COUNTERS_TRANSFORM(X) \
    CHRONON_COUNTERS_EFFECTS(X) \
    CHRONON_COUNTERS_FRAMEBUFFER(X) \
    CHRONON_COUNTERS_DIRTY(X) \
    CHRONON_COUNTERS_LAYER(X) \
    CHRONON_COUNTERS_GRAPH(X) \
    CHRONON_COUNTERS_VIDEO(X)

#define CHRONON_RENDER_COUNTERS_SYSTEM(X) \
    X(process_context_switches_voluntary) \
    X(process_context_switches_involuntary) \
    X(os_page_faults_major) \
    X(os_page_faults_minor) \
    X(ffmpeg_cpu_user_pct) \
    X(ffmpeg_cpu_sys_pct) \
    X(llc_references) \
    X(llc_misses) \
    X(system_logical_cores) \
    X(system_ram_total_mb) \
    X(system_ram_available_min_mb) \
    X(process_cpu_user_ms) \
    X(process_cpu_sys_ms) \
    X(process_rss_peak_mb) \
    X(tbb_arena_max_concurrency) \
    X(tbb_active_workers_peak) \
    X(tbb_active_workers_avg_sum) \
    X(tbb_active_workers_avg_count) \
    X(parallel_regions_count) \
    X(parallel_regions_skipped_small_level) \
    X(parallel_idle_worker_entry_sum) \
    X(parallel_idle_worker_samples) \
    X(sequential_level_execute_wall_ms) \
    X(used_parallel_clear) \
    X(skipped_clear_small) \
    X(used_parallel_transform) \
    X(skipped_transform_small) \
    X(used_parallel_composite) \
    X(skipped_composite_small) \
    X(skipped_encoder_backpressure)

#define CHRONON_RENDER_COUNTERS_SETUP(X) \
    X(setup_graph_parsing_wall_ms) \
    X(setup_asset_io_load_wall_ms) \
    X(setup_pool_preallocation_wall_ms) \
    X(image_decode_wall_ms)

} // namespace chronon3d
