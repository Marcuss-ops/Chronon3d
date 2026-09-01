out << "THROUGHPUT & LATENCY\n";
out << "throughput_fps.............. " << fmt::format("{:.1f}", fps) << "\n";
out << "wall_frame_avg_ms........... " << fmt::format("{:.3f}", avg_frame_wall_us / 1000.0) << " ms\n";
out << "wall_frame_p50_ms........... " << fmt::format("{:.2f}", p50) << " ms\n";
out << "wall_frame_p95_ms........... " << fmt::format("{:.2f}", p95) << " ms\n";
out << "wall_frame_p99_ms........... " << fmt::format("{:.2f}", p99) << " ms\n";
out << "render_frame_avg_ms......... " << fmt::format("{:.3f}", static_cast<double>(delta_u64(post_graph_total_us, pre_graph_total_us)) / (frames_d * 1000.0)) << " ms\n";
out << "Total elapsed............... " << fmt::format("{:.1f}", total_elapsed_sec) << " s\n";
out << "CPU utilization............. " << fmt::format("{:.1f}", cpu_percent) << " % (process aggregate)\n";
out << "Encode FPS.................. N/A (benchmark command is not video export)\n\n";

out << "DECISION DISTRIBUTION\n";
out << "ReuseSurface................ " << reuse_frame_times.size() << " frames ("
    << fmt::format("{:.1f}", static_cast<double>(reuse_frame_times.size()) * 100.0 / frames_d)
    << " %, P50: " << fmt::format("{:.2f}", reuse_p50) << " ms)\n";
out << "SparseTiles................. " << sparse_frame_times.size() << " frames ("
    << fmt::format("{:.1f}", static_cast<double>(sparse_frame_times.size()) * 100.0 / frames_d)
    << " %, P50: " << fmt::format("{:.2f}", sparse_p50) << " ms)\n";
out << "FullRgb..................... " << full_rgb_frame_times.size() << " frames ("
    << fmt::format("{:.1f}", static_cast<double>(full_rgb_frame_times.size()) * 100.0 / frames_d)
    << " %, P50: " << fmt::format("{:.2f}", full_p50) << " ms)\n";
out << "SparseYuv................... " << sparse_yuv_frame_times.size() << " frames ("
    << fmt::format("{:.1f}", static_cast<double>(sparse_yuv_frame_times.size()) * 100.0 / frames_d)
    << " %, P50: " << fmt::format("{:.2f}", sparse_yuv_p50) << " ms)\n";
out << "FullYuv..................... " << full_yuv_frame_times.size() << " frames ("
    << fmt::format("{:.1f}", static_cast<double>(full_yuv_frame_times.size()) * 100.0 / frames_d)
    << " %, P50: " << fmt::format("{:.2f}", full_yuv_p50) << " ms)\n";
out << "CopyGop..................... " << copy_gop_frame_times.size() << " frames ("
    << fmt::format("{:.1f}", static_cast<double>(copy_gop_frame_times.size()) * 100.0 / frames_d)
    << " %, P50: " << fmt::format("{:.2f}", copy_gop_p50) << " ms)\n\n";

out << "HARDWARE\n";
if (perf.available == "true") {
    out << "Cycles/frame................ " << perf.cycles_per_frame << "\n";
    out << "Instructions/frame.......... " << perf.instructions_per_frame << "\n";
    out << "IPC......................... " << perf.ipc << "\n";
    out << "Branch miss................. " << perf.branch_miss_pct << "\n";
    out << "LLC miss.................... " << perf.cache_miss_pct << "\n";
} else {
    out << "Cycles/frame................ N/A (install perf)\n";
    out << "Instructions/frame.......... N/A\n";
    out << "IPC......................... N/A\n";
    out << "Branch miss................. N/A\n";
    out << "LLC miss.................... N/A\n";
    out << "Hint........................ Use tools/bench_perf_stat.sh\n";
}
out << "\n";

out << "MEMORY\n";
out << "Allocations/frame........... " << alloc_per_frame << "\n";
out << "Framebuffer copies/frame.... " << copies_per_frame << "\n";
out << "Full-frame passes........... " << passes_per_frame << "\n";
out << "Peak RSS.................... " << peak_rss_mb << " MB\n";
out << "FB pool current............. " << pool_stats.current_bytes / (1024 * 1024) << " MB\n";
out << "FB pool retained............ " << pool_stats.retained_bytes / (1024 * 1024) << " MB\n";
out << "FB pool available........... " << pool_stats.available_count << "\n";
out << "FB pool allocations......... " << pool_stats.total_allocations << "\n";
out << "FB pool reuses.............. " << pool_stats.total_reuses << "\n";
out << "FB pool clears.............. " << pool_stats.total_clears << "\n";
out << "FB pool evictions........... " << pool_stats.evicted_count << "\n";
out << "FB pool size classes........ " << pool_stats.size_class_count << "\n";
out << "Node cache entries.......... " << node_cache_stats.current_size << "\n";
out << "Node cache weight........... " << node_cache_stats.current_weight / (1024 * 1024) << " MB\n";
out << "Node cache hits............. " << node_cache_stats.hits << "\n";
out << "Node cache misses........... " << node_cache_stats.misses << "\n";
out << "Node cache evictions........ " << node_cache_stats.evictions << "\n\n";

const auto* render_counters = renderer->counters();

out << "INVALIDATION / COST\n";
out << "Dirty union pixels.......... "
    << (render_counters ? render_counters->dirty_union_area_pixels.load() : 0) << "\n";
out << "Dirty pixels................ "
    << (render_counters ? render_counters->dirty_pixels.load() : 0) << "\n";
out << "Dirty rect full fallbacks... "
    << (render_counters ? render_counters->dirty_full_fallbacks.load() : 0) << "\n";
out << "Tile dirty count............. "
    << (render_counters ? render_counters->tile_dirty_count.load() : 0) << "\n";
out << "Tile clean count............. "
    << (render_counters ? render_counters->tile_clean_count.load() : 0) << "\n";
out << "Tile pixels rendered......... "
    << (render_counters ? render_counters->tile_pixels_rendered.load() : 0) << "\n";
out << "Tile pixels skipped.......... "
    << (render_counters ? render_counters->tile_pixels_skipped.load() : 0) << "\n";
out << "Tile full fallbacks.......... "
    << (render_counters ? render_counters->tile_full_fallbacks.load() : 0) << "\n";
out << "Tile regions executed........ "
    << (render_counters ? render_counters->tile_regions_executed.load() : 0) << "\n";
out << "Tile region pixels........... "
    << (render_counters ? render_counters->tile_region_pixels.load() : 0) << "\n";
out << "Tile execution time.......... "
    << (render_counters ? render_counters->tile_execution_wall_ms.load() : 0) << " ms\n";
out << "Nodes skipped................ "
    << (render_counters ? render_counters->nodes_skipped.load() : 0) << "\n";
out << "Graph skipped frames......... "
    << (render_counters ? render_counters->graph_skipped_frames.load() : 0) << "\n";
out << "Dirty evaluation............ "
    << (render_counters ? render_counters->dirty_eval_wall_ms.load() : 0) << " ms\n";
out << "Graph dirty-rect time........ "
    << (render_counters ? render_counters->graph_dirty_rect_wall_ms.load() : 0) << " ms\n";
out << "Full exec EWMA............... "
    << fmt::format("{:.3f}", renderer->dirty_telemetry().full_frame_exec_ms_ewma)
    << " ms (" << renderer->dirty_telemetry().full_frame_cost_samples << " samples)\n";
out << "Tile exec EWMA............... "
    << fmt::format("{:.3f}", renderer->dirty_telemetry().tile_exec_ms_ewma)
    << " ms (" << renderer->dirty_telemetry().tile_cost_samples << " samples)\n";
out << "Tile cost model.............. "
    << (renderer->dirty_telemetry().tile_cost_model_ready() ? "READY" : "WARMING") << "\n\n";

out << "HOT-PATH CONTRACTS\n";
out << "Surface YUV frames........... "
    << (render_counters ? render_counters->native_surface_yuv_frames.load() : 0) << "\n";
out << "Surface YUV bytes............ "
    << (render_counters ? render_counters->native_surface_yuv_bytes.load() : 0) << "\n";
out << "Batch YUV frames............. "
    << (render_counters ? render_counters->native_batch_yuv_frames.load() : 0) << "\n";
out << "Batch YUV bytes.............. "
    << (render_counters ? render_counters->native_batch_yuv_bytes.load() : 0) << "\n";
out << "SIMO variant submits......... "
    << (render_counters ? render_counters->simo_variant_submits.load() : 0) << "\n";
out << "Encoder staging copies....... "
    << (render_counters ? render_counters->encoder_staging_copy_bytes.load() : 0) << " bytes\n\n";

out << "FUSED PIXEL PROGRAM\n";
out << "Passes before fusion........ "
    << (render_counters ? render_counters->pixel_fusion_passes_before.load() : 0) << "\n";
out << "Passes after fusion......... "
    << (render_counters ? render_counters->pixel_fusion_passes_after.load() : 0) << "\n";
out << "Bytes saved by fusion....... "
    << (render_counters ? render_counters->pixel_fusion_bytes_saved.load() : 0) << "\n\n";
out << "COLOR PIPELINE RUNTIME\n";
out << "Fused color batches.......... "
    << (render_counters ? render_counters->color_pipeline_batches.load() : 0) << "\n";
out << "Fused color effects.......... "
    << (render_counters ? render_counters->color_pipeline_effects.load() : 0) << "\n\n";

if (renderer->runtime().backend_attached()) {
    const std::uint64_t lb_calls = gpu_delta("layer_batch_calls");
    const std::uint64_t l_inst = gpu_delta("layer_instances");
    const std::uint64_t tb_calls = gpu_delta("text_batch_calls");
    const std::uint64_t glyphs = gpu_delta("glyphs");
    out << "BATCHING & INSTANCING (TIMED-RUN DELTAS)\n";
    out << "layer_batch_calls/frame..... " << fmt::format("{:.1f}", static_cast<double>(lb_calls) / frames_d) << "\n";
    out << "layer_instances/frame....... " << fmt::format("{:.1f}", static_cast<double>(l_inst) / frames_d) << "\n";
    out << "text_batch_calls/frame...... " << fmt::format("{:.1f}", static_cast<double>(tb_calls) / frames_d) << "\n";
    out << "glyphs/frame................ " << fmt::format("{:.1f}", static_cast<double>(glyphs) / frames_d) << "\n\n";

    out << "GPU EXECUTION METRICS (TIMED-RUN DELTAS)\n";
    for (const auto& [k, unused] : gpu_post_ordered) {
        (void)unused;
        out << fmt::format("{:<28} {}\n", k, gpu_delta(k));
    }
    out << "\n";
}

const double timeline_eval_us = static_cast<double>(
    delta_u64(post_timeline_eval_us, pre_timeline_eval_us)) / frames_d;
const double graph_total_us = static_cast<double>(
    delta_u64(post_graph_total_us, pre_graph_total_us)) / frames_d;
const double graph_refresh_us = static_cast<double>(
    delta_u64(post_graph_refresh_us, pre_graph_refresh_us)) / frames_d;
const double text_shape_us = static_cast<double>(
    delta_u64(post_text_shape_ms, pre_text_shape_ms)) * 1000.0 / frames_d;
const double text_layout_us = static_cast<double>(
    delta_u64(post_text_layout_ms, pre_text_layout_ms)) * 1000.0 / frames_d;
const double asset_resolve_us = static_cast<double>(
    delta_u64(post_asset_resolve_us, pre_asset_resolve_us)) / frames_d;

const double frame_slot_wait_us = static_cast<double>(gpu_delta("frame_slot_wait_us")) / frames_d;
const double fence_wait_us = static_cast<double>(gpu_delta("gpu_wait_cpu_us")) / frames_d;
const double queue_submit_cpu_us = static_cast<double>(gpu_delta("gpu_submit_cpu_us")) / frames_d;
const double gpu_timestamp_us = static_cast<double>(gpu_delta("gpu_execute_us")) / frames_d;
const double readback_us = static_cast<double>(gpu_delta("readback_us")) / frames_d;
const double texture_upload_count = static_cast<double>(gpu_delta("gpu_upload_count")) / frames_d;
const double texture_upload_bytes = static_cast<double>(gpu_delta("gpu_upload_bytes")) / frames_d;
const double image_upload_count = static_cast<double>(
    gpu_delta("gpu_upload_image_full_count") + gpu_delta("gpu_upload_image_region_count")) / frames_d;
const double image_upload_bytes = static_cast<double>(gpu_delta("gpu_upload_image_bytes")) / frames_d;

const double accounted_us = timeline_eval_us + graph_total_us;
const double unaccounted_us = avg_frame_wall_us - accounted_us;
const double accounting_check_us = accounted_us + unaccounted_us - avg_frame_wall_us;

out << "SETUP TIMINGS\n";
out << "compile_us.................. " << fmt::format("{:.1f}", compile_us) << " us (one-time)\n";
out << "prepare_us.................. N/A (prepare barrier not separately surfaced)\n\n";
out << "FRAME WALL BREAKDOWN (PER-FRAME AVERAGE; WARM-UP EXCLUDED)\n";
out << "frame_wall_us............... " << fmt::format("{:.1f}", avg_frame_wall_us) << " us\n";
out << "timeline_eval_us............ " << fmt::format("{:.1f}", timeline_eval_us) << " us\n";
out << "timeline_patch_us........... N/A (not separately instrumented)\n";
out << "scene_clone_us.............. N/A (not separately instrumented)\n";
out << "graph_total_us.............. " << fmt::format("{:.1f}", graph_total_us) << " us\n";
out << "graph_refresh_us............ " << fmt::format("{:.1f}", graph_refresh_us) << " us\n";
out << "parameter_patch_us.......... N/A (not separately instrumented)\n";
out << "instance_patch_us........... N/A (not separately instrumented)\n";
out << "frame_slot_acquire_us....... N/A (only wait time is instrumented)\n";
out << "frame_slot_wait_us.......... " << fmt::format("{:.1f}", frame_slot_wait_us) << " us [nested]\n";
out << "fence_wait_us............... " << fmt::format("{:.1f}", fence_wait_us) << " us [nested]\n";
out << "ssbo_write_us............... N/A (not separately instrumented)\n";
out << "descriptor_update_us........ N/A (not separately instrumented)\n";
out << "command_record_us........... N/A (not separately instrumented)\n";
out << "queue_submit_cpu_us......... " << fmt::format("{:.1f}", queue_submit_cpu_us) << " us [nested]\n";
out << "gpu_timestamp_us............ " << fmt::format("{:.1f}", gpu_timestamp_us) << " us [GPU elapsed; nested]\n";
out << "output_wrap_us.............. N/A (not separately instrumented)\n";
out << "readback_us................. " << fmt::format("{:.1f}", readback_us) << " us [nested]\n";
out << "accounted_us................ " << fmt::format("{:.1f}", accounted_us) << " us\n";
out << "unaccounted_us.............. " << fmt::format("{:.1f}", unaccounted_us) << " us\n";
out << "accounting_check_us......... " << fmt::format("{:.3f}", accounting_check_us) << " us (target ~= 0)\n\n";

out << "STEADY-STATE UPLOADS (PER-FRAME AVERAGE)\n";
out << "texture_upload_count/frame. " << fmt::format("{:.3f}", texture_upload_count) << "\n";
out << "texture_upload_bytes/frame. " << fmt::format("{:.1f}", texture_upload_bytes) << "\n";
out << "image_upload_count/frame... " << fmt::format("{:.3f}", image_upload_count) << "\n";
out << "image_upload_bytes/frame... " << fmt::format("{:.1f}", image_upload_bytes) << "\n";
out << "NOTE........................ No gpu_upload_us proxy: upload time is not instrumented.\n\n";
out << "TEXT / ASSET NESTED COSTS\n";
out << "text_shape_us............... " << fmt::format("{:.1f}", text_shape_us) << " us\n";
out << "text_layout_us.............. " << fmt::format("{:.1f}", text_layout_us) << " us\n";
out << "asset_resolve_us............ " << fmt::format("{:.1f}", asset_resolve_us) << " us\n\n";
