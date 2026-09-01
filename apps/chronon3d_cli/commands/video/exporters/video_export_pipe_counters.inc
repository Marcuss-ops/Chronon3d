        timings.text.raster_ms = static_cast<double>(
            c->text_rasterization_wall_ms.load(std::memory_order_relaxed));
        timings.text.atlas_upload_ms = static_cast<double>(
            c->glyph_atlas_upload_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.text.draw_ms = static_cast<double>(
            c->text_draw_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.text.atlas_cache_hits = c->gpu_text_atlas_cache_hits.load(std::memory_order_relaxed);
        timings.text.atlas_cache_misses = c->gpu_text_atlas_cache_misses.load(std::memory_order_relaxed);
        timings.text.atlas_key_bytes_hashed = c->gpu_text_atlas_key_bytes_hashed.load(std::memory_order_relaxed);
        timings.text.atlas_repack_count = c->gpu_text_atlas_repack_count.load(std::memory_order_relaxed);
        timings.text.atlas_repack_bytes = c->gpu_text_atlas_repack_bytes.load(std::memory_order_relaxed);
        timings.text.atlas_upload_count = c->gpu_text_atlas_upload_count.load(std::memory_order_relaxed);
        timings.text.atlas_upload_bytes = c->gpu_text_atlas_upload_bytes.load(std::memory_order_relaxed);
        timings.text.instance_upload_count = c->gpu_text_instance_upload_count.load(std::memory_order_relaxed);
        timings.text.instance_upload_bytes = c->gpu_text_instance_upload_bytes.load(std::memory_order_relaxed);
        timings.cache.node_lookup_ms = static_cast<double>(
            c->node_cache_lookup_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cache.node_cache_hits = c->node_cache_hits.load(std::memory_order_relaxed);
        timings.cache.node_cache_misses = c->node_cache_misses.load(std::memory_order_relaxed);
        timings.cache.glyph_cache_hits = c->glyph_cache_hits.load(std::memory_order_relaxed);
        timings.cache.glyph_cache_misses = c->glyph_cache_misses.load(std::memory_order_relaxed);
        timings.cache.gpu_asset_cache_hits = c->gpu_asset_cache_hits.load(std::memory_order_relaxed);
        timings.cache.gpu_asset_cache_misses = c->gpu_asset_cache_misses.load(std::memory_order_relaxed);
        timings.framebuffer_allocations = c->framebuffer_allocations.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_text = c->framebuffer_alloc_text.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_effect = c->framebuffer_alloc_effect.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_glow = c->framebuffer_alloc_glow.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_video = c->framebuffer_alloc_video.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_graph = c->framebuffer_alloc_graph.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_scratch = c->framebuffer_alloc_scratch.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_unknown = c->framebuffer_alloc_unknown.load(std::memory_order_relaxed);
        timings.gpu.video_decode_frames = c->video_decode_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_native_surface_frames = c->video_decode_native_surface_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_hw_transfer_frames = c->video_decode_hw_transfer_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_software_frames = c->video_decode_software_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_native_fallback_frames = c->video_decode_native_fallback_frames.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_hits = c->video_prefetch_hits.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_misses = c->video_prefetch_misses.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_wait_us = c->video_prefetch_wait_us.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_queue_clear_count = c->video_prefetch_queue_clear_count.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_queue_depth_peak = c->video_prefetch_queue_depth_peak.load(std::memory_order_relaxed);
        timings.gpu.video_decode_wall_ms = c->video_decode_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_hw_transfer_wall_ms = c->video_decode_hw_transfer_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_sws_wall_ms = c->video_decode_sws_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_framebuffer_wall_ms = c->video_decode_framebuffer_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.hwframe_transfer_to_cpu_frames = c->hwframe_transfer_to_cpu_frames.load(std::memory_order_relaxed);
        timings.gpu.software_color_convert_frames = c->software_color_convert_frames.load(std::memory_order_relaxed);
        timings.gpu.cpu_full_surface_upload_bytes = c->cpu_full_surface_upload_bytes.load(std::memory_order_relaxed);
        timings.gpu.gpu_readback_bytes = c->gpu_readback_bytes.load(std::memory_order_relaxed);
        timings.gpu.nvenc_frames = c->nvenc_frames.load(std::memory_order_relaxed);
        timings.gpu.software_encode_frames = c->software_encode_frames.load(std::memory_order_relaxed);
        timings.gpu.bitstream_copy_frames = c->bitstream_copy_frames.load(std::memory_order_relaxed);
        timings.gpu.vulkan_frames = c->vulkan_frames.load(std::memory_order_relaxed);
        timings.gpu.cpu_readback_frames = c->cpu_readback_frames.load(std::memory_order_relaxed);
        timings.gpu.decode_submit_ms = c->decode_submit_ms.load(std::memory_order_relaxed);
        timings.gpu.decode_wait_ms = c->decode_wait_ms.load(std::memory_order_relaxed);
        timings.gpu.hwframe_transfer_ms = c->hwframe_transfer_ms.load(std::memory_order_relaxed);
        timings.gpu.swscale_ms = c->swscale_ms.load(std::memory_order_relaxed);
        timings.gpu.cpu_pixel_conversion_ms = c->cpu_pixel_conversion_ms.load(std::memory_order_relaxed);
        timings.gpu.full_surface_upload_ms = c->full_surface_upload_ms.load(std::memory_order_relaxed);
        timings.gpu.video_composite_ms = c->video_composite_ms.load(std::memory_order_relaxed);
        timings.gpu.encode_submit_ms = c->encode_submit_ms.load(std::memory_order_relaxed);
        timings.gpu.encode_wait_ms = c->encode_wait_ms.load(std::memory_order_relaxed);

        timings.cpu_breakdown.timeline_eval_ms = static_cast<double>(c->timeline_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_resolve_layers_ms = static_cast<double>(c->graph_resolve_layers_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_dirty_rect_ms = static_cast<double>(c->graph_dirty_rect_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_build_ms = static_cast<double>(c->graph_build_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_execute_ms = static_cast<double>(c->graph_execute_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.compiled_graph_refresh_ms = static_cast<double>(c->compiled_graph_refresh_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.cache_eval_ms = static_cast<double>(c->cache_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.dirty_eval_ms = static_cast<double>(c->dirty_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.input_resolve_ms = static_cast<double>(c->input_resolve_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.predicted_bbox_ms = static_cast<double>(c->predicted_bbox_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.clone_context_ms = static_cast<double>(c->clone_context_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.state_assign_ms = static_cast<double>(c->state_assign_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.framebuffer_acquire_ms = static_cast<double>(c->framebuffer_acquire_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.framebuffer_clear_ms = static_cast<double>(c->framebuffer_clear_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.framebuffer_lifetime_ms = static_cast<double>(c->framebuffer_lifetime_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_schedule_ms = static_cast<double>(c->node_schedule_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_dispatch_ms = static_cast<double>(c->node_dispatch_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_execute_actual_ms = static_cast<double>(c->node_execute_actual_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_overhead_ms = static_cast<double>(c->node_overhead_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.telemetry_emit_ms = static_cast<double>(c->telemetry_emit_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.text_layout_ms = static_cast<double>(c->text_layout_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.text_rasterization_ms = static_cast<double>(c->text_rasterization_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.text_shaping_ms = static_cast<double>(c->text_shaping_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.text_bidi_ms = static_cast<double>(c->text_bidi_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.glyph_cache_lookup_ms = static_cast<double>(c->glyph_cache_lookup_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.glyph_atlas_upload_ms = static_cast<double>(c->glyph_atlas_upload_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.text_draw_ms = static_cast<double>(c->text_draw_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.clearnode_ms = static_cast<double>(c->clearnode_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.compositenode_blend_ms = static_cast<double>(c->compositenode_blend_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.effect_stack_total_ms = static_cast<double>(c->effect_stack_total_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.graph_executed_frames = c->graph_executed_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.graph_reused_frames = c->graph_reused_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.fast_path_reused_frames = c->fast_path_reused_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.video_source_requested_frames = c->video_source_requested_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.video_source_inactive_frames = c->video_source_inactive_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.video_source_repeated_frames = c->video_source_repeated_frames.load(std::memory_order_relaxed);
    }
    if (session->renderer_ptr()) {
      const auto atlas_stats = session->renderer_ptr()->runtime().gpu_glyph_atlas().stats();
      timings.text.atlas_cache_hits = atlas_stats.hits;
      timings.text.atlas_cache_misses = atlas_stats.misses;
      timings.text.atlas_upload_count = atlas_stats.page_count;
      timings.text.atlas_upload_bytes = atlas_stats.total_glyph_bytes;
    }
    timings.text.atlas_key_bytes_hashed = 0;
    timings.text.atlas_repack_count = 0;
    timings.text.atlas_repack_bytes = 0;
    timings.cache.image_cache_hits = session->prepare_timings.image_cache_hits;
    timings.cache.image_cache_misses = session->prepare_timings.image_cache_misses;
    timings.cache.font_cache_hits = session->prepare_timings.font_cache_hits;
    timings.cache.font_cache_misses = session->prepare_timings.font_cache_misses;

    if (counters) {
        auto* c = counters;
        timings.runtime.video_runtime_created = c->video_runtime_created.load(std::memory_order_relaxed);
        timings.runtime.video_runtime_reused = c->video_runtime_reused.load(std::memory_order_relaxed);
        timings.runtime.cuda_hwdevice_created = c->cuda_hwdevice_created.load(std::memory_order_relaxed);
        timings.runtime.cuda_hwdevice_reused = c->cuda_hwdevice_reused.load(std::memory_order_relaxed);
        timings.runtime.cuda_frames_cache_hit = c->cuda_frames_cache_hit.load(std::memory_order_relaxed);
        timings.runtime.cuda_frames_cache_miss = c->cuda_frames_cache_miss.load(std::memory_order_relaxed);
        timings.runtime.cuda_image_cache_hit = c->cuda_image_cache_hit.load(std::memory_order_relaxed);
        timings.runtime.cuda_image_cache_miss = c->cuda_image_cache_miss.load(std::memory_order_relaxed);
        timings.gpu.software_fallback_nodes = c->gpu_text_fallback_count.load(std::memory_order_relaxed);
    }
    timings.runtime.encoder_open_nvenc_ms = session->startup_breakdown.encoder_open_nvenc_ms;

    if (session->renderer_ptr() && session->renderer_ptr()->runtime().backend_attached()) {
        std::vector<std::pair<std::string, std::uint64_t>> gpu_counters;
      session->renderer_ptr()->runtime().backend().export_gpu_telemetry_counters(gpu_counters);
        for (const auto& [name, value] : gpu_counters) {
            if (name == "gpu_execute_us") {
                timings.gpu.gpu_execute_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "readback_us") {
