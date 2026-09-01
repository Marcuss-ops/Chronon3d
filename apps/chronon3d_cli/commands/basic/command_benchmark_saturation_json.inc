if (!report_json.empty()) {
    const auto counter = [render_counters](auto member) -> std::uint64_t {
        return render_counters ? (render_counters->*member).load(std::memory_order_relaxed) : 0;
    };
    nlohmann::json report{
        {"schema_version", 2},
        {"scene", scene},
        {"duration_sec", duration_sec},
        {"frames", total_frames},
        {"motion_blur", {
            {"mode", motion_blur_mode},
            {"samples", motion_blur_samples},
            {"shutter_angle_deg", 180.0},
            {"shutter_phase_deg", -90.0}
        }},
        {"fps", fps},
        {"throughput_fps", fps},
        {"average_frame_ms", avg_frame_wall_us / 1000.0},
        {"wall_frame_avg_ms", avg_frame_wall_us / 1000.0},
        {"wall_frame_p50_ms", p50},
        {"wall_frame_p95_ms", p95},
        {"wall_frame_p99_ms", p99},
        {"render_frame_avg_ms", graph_total_us / 1000.0},
        {"p50_ms", p50},
        {"p95_ms", p95},
        {"p99_ms", p99},
        {"total_elapsed_sec", total_elapsed_sec},
        {"decisions", {
            {"ReuseSurface", {
                {"count", reuse_frame_times.size()},
                {"ratio", static_cast<double>(reuse_frame_times.size()) / frames_d},
                {"p50_ms", reuse_p50}
            }},
            {"SparseTiles", {
                {"count", sparse_frame_times.size()},
                {"ratio", static_cast<double>(sparse_frame_times.size()) / frames_d},
                {"p50_ms", sparse_p50}
            }},
            {"FullRgb", {
                {"count", full_rgb_frame_times.size()},
                {"ratio", static_cast<double>(full_rgb_frame_times.size()) / frames_d},
                {"p50_ms", full_p50}
            }},
            {"SparseYuv", {
                {"count", sparse_yuv_frame_times.size()},
                {"ratio", static_cast<double>(sparse_yuv_frame_times.size()) / frames_d},
                {"p50_ms", sparse_yuv_p50}
            }},
            {"FullYuv", {
                {"count", full_yuv_frame_times.size()},
                {"ratio", static_cast<double>(full_yuv_frame_times.size()) / frames_d},
                {"p50_ms", full_yuv_p50}
            }},
            {"CopyGop", {
                {"count", copy_gop_frame_times.size()},
                {"ratio", static_cast<double>(copy_gop_frame_times.size()) / frames_d},
                {"p50_ms", copy_gop_p50}
            }}
        }},
        {"utilization", {
            {"cpu_percent", cpu_percent},
            {"gpu_percent", nullptr},
            {"gpu_percent_status", "NOT_SAMPLED_BY_PROCESS"}
        }},
        {"encode_fps", nullptr},
        {"encode_fps_status", "NOT_VIDEO_EXPORT"},
        {"setup", {
            {"compile_us", compile_us},
            {"prepare_us", nullptr},
            {"prepare_status", "NOT_SEPARATELY_INSTRUMENTED"}
        }},
        {"frame_wall_breakdown", {
            {"frame_wall_us", avg_frame_wall_us},
            {"timeline_eval_us", timeline_eval_us},
            {"timeline_patch_us", nullptr},
            {"scene_clone_us", nullptr},
            {"graph_total_us", graph_total_us},
            {"graph_refresh_us", graph_refresh_us},
            {"parameter_patch_us", nullptr},
            {"instance_patch_us", nullptr},
            {"frame_slot_acquire_us", nullptr},
            {"frame_slot_wait_us", frame_slot_wait_us},
            {"fence_wait_us", fence_wait_us},
            {"ssbo_write_us", nullptr},
            {"descriptor_update_us", nullptr},
            {"command_record_us", nullptr},
            {"queue_submit_cpu_us", queue_submit_cpu_us},
            {"gpu_timestamp_us", gpu_timestamp_us},
            {"output_wrap_us", nullptr},
            {"readback_us", readback_us},
            {"accounted_us", accounted_us},
            {"unaccounted_us", unaccounted_us},
            {"accounting_check_us", accounting_check_us},
            {"accounting_contract", "accounted_us + unaccounted_us == frame_wall_us"}
        }},
        {"uploads", {
            {"texture_upload_count_per_frame", texture_upload_count},
            {"texture_upload_bytes_per_frame", texture_upload_bytes},
            {"image_upload_count_per_frame", image_upload_count},
            {"image_upload_bytes_per_frame", image_upload_bytes},
            {"upload_time_us", nullptr},
            {"upload_time_status", "NOT_INSTRUMENTED"}
        }},
        {"gpu", {
            {"vk_submissions_per_frame", gpu_delta("gpu_submissions") / frames_d},
            {"passes_executed_per_frame", gpu_delta("passes_executed") / frames_d},
            {"gpu_upload_count_per_frame", gpu_delta("gpu_upload_count") / frames_d},
            {"gpu_upload_bytes_per_frame", gpu_delta("gpu_upload_bytes") / frames_d},
            {"gpu_upload_full_surface_bytes_per_frame", gpu_delta("gpu_upload_full_surface_bytes") / frames_d},
            {"gpu_upload_region_bytes_per_frame", gpu_delta("gpu_upload_region_bytes") / frames_d},
            {"gpu_readback_bytes_per_frame", gpu_delta("gpu_readback_bytes") / frames_d},
            {"gpu_submit_cpu_us_per_frame", gpu_delta("gpu_submit_cpu_us") / frames_d},
            {"gpu_wait_cpu_us_per_frame", gpu_delta("gpu_wait_cpu_us") / frames_d},
            {"frame_slot_wait_us_per_frame", gpu_delta("frame_slot_wait_us") / frames_d},
            {"standalone_wait_us_per_frame", gpu_delta("standalone_wait_us") / frames_d},
            {"frame_batch_drain_wait_us_per_frame", gpu_delta("frame_batch_drain_wait_us") / frames_d},
            {"readback_us_per_frame", gpu_delta("readback_us") / frames_d},
            {"gpu_execute_us_per_frame", gpu_delta("gpu_execute_us") / frames_d},
            {"cpu_gpu_sync_us_per_frame", gpu_delta("cpu_gpu_sync_us") / frames_d},
            {"vk_cmd_dispatch_per_frame", gpu_delta("vkCmdDispatch") / frames_d},
            {"vk_cmd_draw_per_frame", gpu_delta("vkCmdDraw") / frames_d},
            {"barriers_per_frame", gpu_delta("barriers") / frames_d},
            {"descriptor_allocations_per_frame", gpu_delta("descriptor_allocations") / frames_d},
            {"physical_surfaces_peak", gpu_absolute("physical_surfaces_peak")},
            {"vma_allocation_bytes_after", gpu_absolute("vma_allocation_bytes")},
            {"vma_block_bytes_after", gpu_absolute("vma_block_bytes")},
            {"vma_allocation_count_after", gpu_absolute("vma_allocation_count")},
            {"vma_block_count_after", gpu_absolute("vma_block_count")},
            {"vma_budget_bytes_after", gpu_absolute("vma_budget_bytes")},
            {"vma_usage_bytes_after", gpu_absolute("vma_usage_bytes")}
        }},
        {"nested_costs", {
            {"text_shape_us", text_shape_us},
            {"text_layout_us", text_layout_us},
            {"asset_resolve_us", asset_resolve_us}
        }},
        {"memory", {
            {"peak_rss_mb", peak_rss_mb},
            {"framebuffer_allocations_per_frame", alloc_per_frame},
            {"framebuffer_copies_per_frame", copies_per_frame},
            {"full_frame_passes_per_frame", passes_per_frame},
            {"pool_current_mb", pool_stats.current_bytes / (1024 * 1024)},
            {"pool_retained_mb", pool_stats.retained_bytes / (1024 * 1024)},
            {"pool_allocations", pool_stats.total_allocations},
            {"pool_reuses", pool_stats.total_reuses},
            {"pool_clears", pool_stats.total_clears},
            {"pool_evictions", pool_stats.evicted_count}
        }},
        {"hot_path", {
            {"native_surface_yuv_frames", counter(&RenderCounters::native_surface_yuv_frames)},
            {"native_surface_yuv_bytes", counter(&RenderCounters::native_surface_yuv_bytes)},
            {"native_batch_yuv_frames", counter(&RenderCounters::native_batch_yuv_frames)},
            {"native_batch_yuv_bytes", counter(&RenderCounters::native_batch_yuv_bytes)},
            {"simo_variant_submits", counter(&RenderCounters::simo_variant_submits)},
            {"encoder_staging_copy_bytes", counter(&RenderCounters::encoder_staging_copy_bytes)}
        }},
        {"cache", {
            {"entries", node_cache_stats.current_size},
            {"weight_bytes", node_cache_stats.current_weight},
            {"hits", node_cache_stats.hits},
            {"misses", node_cache_stats.misses},
            {"evictions", node_cache_stats.evictions}
        }},
        {"invalidation", {
            {"dirty_union_pixels", counter(&RenderCounters::dirty_union_area_pixels)},
            {"dirty_pixels", counter(&RenderCounters::dirty_pixels)},
            {"dirty_full_fallbacks", counter(&RenderCounters::dirty_full_fallbacks)},
            {"tile_dirty_count", counter(&RenderCounters::tile_dirty_count)},
            {"tile_clean_count", counter(&RenderCounters::tile_clean_count)},
            {"tile_pixels_rendered", counter(&RenderCounters::tile_pixels_rendered)},
            {"tile_pixels_skipped", counter(&RenderCounters::tile_pixels_skipped)},
            {"tile_full_fallbacks", counter(&RenderCounters::tile_full_fallbacks)},
            {"tile_regions_executed", counter(&RenderCounters::tile_regions_executed)},
            {"tile_region_pixels", counter(&RenderCounters::tile_region_pixels)},
            {"tile_execution_wall_ms", counter(&RenderCounters::tile_execution_wall_ms)},
            {"nodes_skipped", counter(&RenderCounters::nodes_skipped)},
            {"dirty_eval_wall_ms", counter(&RenderCounters::dirty_eval_wall_ms)},
            {"graph_dirty_rect_wall_ms", counter(&RenderCounters::graph_dirty_rect_wall_ms)}
        }},
        {"cost_model", {
            {"full_frame_exec_ms_ewma", renderer->dirty_telemetry().full_frame_exec_ms_ewma},
            {"tile_exec_ms_ewma", renderer->dirty_telemetry().tile_exec_ms_ewma},
            {"full_frame_samples", renderer->dirty_telemetry().full_frame_cost_samples},
            {"tile_samples", renderer->dirty_telemetry().tile_cost_samples},
            {"state", renderer->dirty_telemetry().tile_cost_model_ready() ? "READY" : "WARMING"}
        }},
        {"fusion", {
            {"passes_before", counter(&RenderCounters::pixel_fusion_passes_before)},
            {"passes_after", counter(&RenderCounters::pixel_fusion_passes_after)},
            {"bytes_saved", counter(&RenderCounters::pixel_fusion_bytes_saved)},
            {"color_pipeline_batches", counter(&RenderCounters::color_pipeline_batches)},
            {"color_pipeline_effects", counter(&RenderCounters::color_pipeline_effects)}
        }},
        {"determinism", {
            {"status", "NOT_RUN"},
            {"sequential_hash", nullptr},
            {"random_access_hash", nullptr}
        }}
    };
    std::ofstream json_out(report_json);
    if (!json_out) {
        spdlog::error("Cannot write benchmark JSON report: {}", report_json);
        return 1;
    }
    json_out << report.dump(2) << '\n';
    spdlog::info("Benchmark JSON report written to {}", report_json);
}
