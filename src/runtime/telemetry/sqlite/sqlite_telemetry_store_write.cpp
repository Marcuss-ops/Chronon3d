#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    // Named-column INSERT: column order matches telemetry_schema.sql exactly (103 columns)
    const char* sql =
        "INSERT OR REPLACE INTO render_runs ("
        "run_id, composition_id, output_path, success, error_code, error_message, "
        "frames_total, frames_written, wall_time_ms, render_ms, encode_ms, "
        "effective_fps, pixels_touched, cache_hits, cache_misses, nodes_executed, "
        "layers_rendered, text_glyphs_rasterized, images_sampled, blur_pixels, "
        "simd_lerp_calls, tiles_total, tiles_hit, tiles_miss, tiles_partial, "
        "bytes_allocated_peak, node_cache_hash_collisions, "
        "clear_skipped_calls, clear_skipped_pixels, clear_calls, clear_pixels, composite_calls, composite_pixels, "
        "transform_calls, transform_pixels, effect_stack_calls, effect_pixels, "
        "layer_culling_tests, layers_culled, layers_visible, "
        "framebuffer_allocations, framebuffer_reuses, framebuffer_bytes_allocated, "
        "framebuffer_bytes_peak, "
        "dirty_rect_count, dirty_pixels, dirty_union_area_pixels, dirty_full_fallbacks, "
        "bypass_not_cacheable_count, "
        "dirty_full_fallback_predicted_bounds_missing, "
        "dirty_full_fallback_composite_missing_input_bounds, "
        "dirty_full_fallback_transform_bounds_unknown, "
        "dirty_full_fallback_effect_bounds_unknown, "
        "framebuffer_acquire_ms, framebuffer_clear_ms, clearnode_ms, "
        "framebuffer_pool_clear_ms, framebuffer_enqueue_ms, "
        "framebuffer_pool_miss_count_size_mismatch, framebuffer_pool_miss_count_empty, "
        "framebuffer_pool_hits, framebuffer_buffer_returned_to_pool_count, "
        "unaligned_memory_copies, frame_conversion_copy_ms, "
        "video_graph_eval_ms, video_conversion_ms, video_pipe_write_ms, video_ffmpeg_latency_ms, "
        "io_queue_push_blocked_ms, io_queue_pop_wait_ms, io_queue_peak_depth, ffmpeg_pipe_write_blocked_ms, converted_frame_cache_hits, ffmpeg_flush_ms, "
        "io_queue_peak_bytes, setup_graph_parsing_ms, setup_asset_io_load_ms, setup_pool_preallocation_ms, image_decode_ms, "
        "process_context_switches_voluntary, process_context_switches_involuntary, os_page_faults_major, os_page_faults_minor, "
        "ffmpeg_cpu_user_pct, ffmpeg_cpu_sys_pct, llc_references, llc_misses, "
        "chronon_render_only_ms, chronon_conversion_copy_ms, chronon_queue_wait_ms, "
        "chronon_render_throughput_ms, ffmpeg_encode_total_ms, ffmpeg_flush_close_ms, "
        "e2e_wall_ms, "
        "started_at_iso, finished_at_iso, git_commit_short, build_type, "
        "compiler_info, os, cpu_model, cores"
        ") VALUES ("
        "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, "
        "?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, "
        "?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, "
        "?31, ?32, ?33, ?34, ?35, ?36, ?37, ?38, ?39, ?40, "
        "?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49, ?50, "
        "?51, ?52, ?53, ?54, ?55, ?56, ?57, ?58, ?59, ?60, "
        "?61, ?62, ?63, ?64, ?65, ?66, ?67, ?68, ?69, ?70, "
        "?71, ?72, ?73, ?74, ?75, ?76, ?77, ?78, ?79, ?80, "
        "?81, ?82, ?83, ?84, ?85, ?86, ?87, ?88, ?89, "
        "?90, ?91, ?92, ?93, ?94, ?95, ?96, ?97, ?98, ?99, "
        "?100, ?101, ?102, ?103"
        ");";

    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    return bind_all(stmt,
        run.run_id,
        run.composition_id,
        run.output_path,
        run.success,
        run.error_code,
        run.error_message,
        run.frames_total,
        run.frames_written,
        run.wall_time_ms,
        run.render_ms,
        run.encode_ms,
        run.effective_fps,
        run.pixels_touched,
        run.cache_hits,
        run.cache_misses,
        run.nodes_executed,
        run.layers_rendered,
        run.text_glyphs_rasterized,
        run.images_sampled,
        run.blur_pixels,
        run.simd_lerp_calls,
        run.tiles_total,
        run.tiles_hit,
        run.tiles_miss,
        run.tiles_partial,
        run.bytes_allocated_peak,
        run.node_cache_hash_collisions,
        run.clear_skipped_calls,
        run.clear_skipped_pixels,
        run.clear_calls,
        run.clear_pixels,
        run.composite_calls,
        run.composite_pixels,
        run.transform_calls,
        run.transform_pixels,
        run.effect_stack_calls,
        run.effect_pixels,
        run.layer_culling_tests,
        run.layers_culled,
        run.layers_visible,
        run.framebuffer_allocations,
        run.framebuffer_reuses,
        run.framebuffer_bytes_allocated,
        run.framebuffer_bytes_peak,
        run.dirty_rect_count,
        run.dirty_pixels,
        run.dirty_union_area_pixels,
        run.dirty_full_fallbacks,
        run.bypass_not_cacheable_count,
        run.dirty_full_fallback_predicted_bounds_missing,
        run.dirty_full_fallback_composite_missing_input_bounds,
        run.dirty_full_fallback_transform_bounds_unknown,
        run.dirty_full_fallback_effect_bounds_unknown,
        run.framebuffer_acquire_ms,
        run.framebuffer_clear_ms,
        run.clearnode_ms,
        run.framebuffer_pool_clear_ms,
        run.framebuffer_enqueue_ms,
        run.framebuffer_pool_miss_count_size_mismatch,
        run.framebuffer_pool_miss_count_empty,
        run.framebuffer_pool_hits,
        run.framebuffer_buffer_returned_to_pool_count,
        run.unaligned_memory_copies,
        run.frame_conversion_copy_ms,
        run.video_graph_eval_ms,
        run.video_conversion_ms,
        run.video_pipe_write_ms,
        run.video_ffmpeg_latency_ms,
        run.io_queue_push_blocked_ms,
        run.io_queue_pop_wait_ms,
        run.io_queue_peak_depth,
        run.ffmpeg_pipe_write_blocked_ms,
        run.converted_frame_cache_hits,
        run.ffmpeg_flush_ms,
        run.io_queue_peak_bytes,
        run.setup_graph_parsing_ms,
        run.setup_asset_io_load_ms,
        run.setup_pool_preallocation_ms,
        run.image_decode_ms,
        run.process_context_switches_voluntary,
        run.process_context_switches_involuntary,
        run.os_page_faults_major,
        run.os_page_faults_minor,
        run.ffmpeg_cpu_user_pct,
        run.ffmpeg_cpu_sys_pct,
        run.llc_references,
        run.llc_misses,
        run.chronon_render_only_ms,
        run.chronon_conversion_copy_ms,
        run.chronon_queue_wait_ms,
        run.chronon_render_throughput_ms,
        run.ffmpeg_encode_total_ms,
        run.ffmpeg_flush_close_ms,
        run.e2e_wall_ms,
        run.started_at_iso,
        run.finished_at_iso,
        run.git_commit_short,
        run.build_type,
        run.compiler_info,
        run.os,
        run.cpu_model,
        run.cores
    ) && stmt.step_done();
}

} // namespace chronon3d::telemetry
#endif
