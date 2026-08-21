#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "pipe_timing_sidecar.hpp"
#include "../../../utils/process_start.hpp"
#include "../../../utils/telemetry/nvml_sampler.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/frame_timing_summary.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>


namespace chronon3d::cli {

PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget)
{
    const auto wall_t0 = profiling::now();

    // Phase 1 — Setup
    const auto setup_t0 = profiling::now();
    auto session =setup_pipe_export_session(registry, compiled, settings, opts, start, end, cpu_budget)
;
    if (!session || !session->encoder || !session->renderer) {
        return PipeExportResult{};
    }

    if (opts.sink.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    // Phase 2-4 — Warmup
    RenderSettings render_opts = settings;
    runtime::RenderPreparationTimings warmup_prepare_timings;
    const auto warmup_result =warmup_pipe_renderer(*session->renderer, compiled, opts,
                                                   &warmup_prepare_timings)
;
    if (!warmup_result) {
        spdlog::error("[video] export aborted: render preparation failed: {}",
                      warmup_result.error().message);
        return PipeExportResult{};
    }
    session->prepare_timings.accumulate(warmup_prepare_timings);
    warmup_pipe_pool(*session);
    const auto warmup_t1 = profiling::now();
    session->sys_metrics.sample_cpu_start();

    // Phase 5 — Render loop + writer join
    telemetry::NvmlSampler nvml_sampler;
    nvml_sampler.start(std::chrono::milliseconds(250));

    auto loop_output = run_pipe_export_loop(*session, registry, compiled, render_opts, start, end, opts);

    // Phase 6 — Encoder close
    auto close_result = close_pipe_encoder(*session);
    nvml_sampler.stop();
    const auto nvml_stats = nvml_sampler.stats();

    const auto wall_t1 = profiling::now();
    const double wall_time_ms = profiling::duration_ms(wall_t0, wall_t1);
    const double encode_ms = profiling::duration_ms(loop_output.render_end, wall_t1);
    const double writer_encode_ms = static_cast<double>(
        session->writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;

    spdlog::info("[video] render_ms={:.2f} encode_ms={:.2f} writer_encode_ms={:.2f} wall_ms={:.2f}",
                 loop_output.render_ms, encode_ms, writer_encode_ms, wall_time_ms);

    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", loop_output.loop_result.queue_wait_ms);

    // Phase 8 — Result (validation + atomic output finalize)
    // Runs before telemetry so the render artifact record can persist the
    // verified SHA-256 digest + published path instead of an empty placeholder.
    auto result = make_pipe_export_result(*session, loop_output.loop_result, close_result,
                                          loop_output.render_ms, encode_ms, wall_time_ms);

    // Phase 7 — Telemetry (SQLite + counters)
    record_pipe_telemetry(composition_id, *session, loop_output.loop_result,
                          close_result, loop_output.telemetry_frames,
                          wall_time_ms, loop_output.render_ms, encode_ms, result);

    // Phase 9 — Frame-timing sidecar (job timings now include validation + finalize)
    const bool is_native = (session->opts.encoder.encoder_backend == "native");
    pipe_timing::JobTimings timings;
    timings.job_wall_ms = wall_time_ms;
    timings.process_wall_ms = profiling::duration_ms(process_start_time(), wall_t0);
    timings.prepare_ms = profiling::duration_ms(setup_t0, warmup_t1);
    timings.engine_init_ms = session->engine_init_ms;
    timings.backend_init_ms = session->backend_init_ms;
    timings.render_loop_wall_ms = loop_output.render_ms;
    timings.encoder_finalize_ms = encode_ms;
    if (nvml_stats.sample_count > 0) {
        timings.hardware.gpu_utilization_avg = nvml_stats.gpu_utilization_avg;
        timings.hardware.gpu_utilization_peak = nvml_stats.gpu_utilization_peak;
        timings.hardware.nvdec_utilization_avg = nvml_stats.nvdec_utilization_avg;
        timings.hardware.nvdec_utilization_peak = nvml_stats.nvdec_utilization_peak;
        timings.hardware.nvenc_utilization_avg = nvml_stats.nvenc_utilization_avg;
        timings.hardware.nvenc_utilization_peak = nvml_stats.nvenc_utilization_peak;
        timings.hardware.memory_utilization_avg = nvml_stats.memory_utilization_avg;
        timings.hardware.vram_used_peak_mb = nvml_stats.vram_used_peak_mb;
        timings.hardware.vram_total_mb = nvml_stats.vram_total_mb;
    }
    if (is_native) timings.mux_finalize_ms = close_result.native_trailer_ms;
    if (is_native) {
        timings.encoder.submit_cpu_ms = close_result.native_send_ms;
        timings.encoder.backpressure_wait_ms = close_result.native_backpressure_ms;
        timings.encoder.cuda_pending_peak = close_result.native_cuda_pending_peak;
        timings.encoder.cuda_backpressure_wait_count =
            close_result.native_cuda_backpressure_wait_count;
        timings.encoder.flush_ms = close_result.native_flush_ms;
        timings.encoder.packet_receive_ms = close_result.native_receive_ms;
        timings.encoder.mux_packet_ms = close_result.native_mux_ms;
        // device_ms stays null — CPU-only encoder, never estimated.
    } else {
        // Pipe path: separate the CPU ::write() copy from the poll()
        // back-pressure wait.  flush/receive/mux happen inside the external
        // FFmpeg process (not measurable) and submit_cpu_ms stays null.
        if (session->renderer->counters()) {
            auto* c = session->renderer->counters();
            timings.encoder.pipe_write_cpu_ms = static_cast<double>(
                c->pipe_write_cpu_wall_us.load(std::memory_order_relaxed)) / 1000.0;
            timings.encoder.pipe_backpressure_wait_ms = static_cast<double>(
                c->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        }
    }
    timings.output_finalize_ms = result.output_finalize_ms;
    timings.validation_ms = result.validation_ms;
    timings.ffprobe_ms = result.ffprobe_ms;
    timings.sha256_ms = result.sha256_ms;
    timings.target_fps = session->opts.output.fps;
    timings.prepare = session->prepare_timings;
    if (session->renderer->counters()) {
        const auto* c = session->renderer->counters();
        timings.gpu.gpu_native_surface_frames = c->gpu_native_surface_frames.load(std::memory_order_relaxed);
        timings.gpu.gpu_native_encode_frames = c->gpu_native_encode_frames.load(std::memory_order_relaxed);
        timings.gpu.gpu_surface_copy_frames = c->gpu_surface_copy_frames.load(std::memory_order_relaxed);
        timings.gpu.cpu_pixel_readback_count = c->cpu_pixel_readback_count.load(std::memory_order_relaxed);
        timings.gpu.cpu_pixel_readback_bytes = c->cpu_pixel_readback_bytes.load(std::memory_order_relaxed);
        timings.gpu.video_pipe_fallback_frames = c->video_pipe_fallback_frames.load(std::memory_order_relaxed);
        timings.gpu.video_native_fallback_frames = c->video_native_fallback_frames.load(std::memory_order_relaxed);
        timings.gpu.gpu_surface_create_failures = c->gpu_surface_create_failures.load(std::memory_order_relaxed);
        timings.gpu.gpu_encode_failures = c->gpu_encode_failures.load(std::memory_order_relaxed);
        timings.gpu.interop_ring_wait_count = c->interop_ring_wait_count.load(std::memory_order_relaxed);
        timings.gpu.interop_ring_wait_us = c->interop_ring_wait_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_wait_count = c->cuda_vulkan_wait_count.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_wait_submit_us = c->cuda_vulkan_wait_submit_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_signal_count = c->cuda_vulkan_signal_count.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_signal_submit_us = c->cuda_vulkan_signal_submit_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_composite_frames = c->cuda_composite_frames.load(std::memory_order_relaxed);
        timings.gpu.cuda_composite_wall_us = c->cuda_composite_wall_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_encode_queue_peak = c->cuda_encode_queue_peak.load(std::memory_order_relaxed);
        timings.gpu.cuda_encode_event_wait_count = c->cuda_encode_event_wait_count.load(std::memory_order_relaxed);
        timings.gpu.cuda_encode_event_wait_us = c->cuda_encode_event_wait_us.load(std::memory_order_relaxed);
        timings.gpu.encoder_staging_copy_bytes = c->encoder_staging_copy_bytes.load(std::memory_order_relaxed);
    }
    if (session->renderer->counters()) {
        auto* c = session->renderer->counters();
        timings.image_draw_ms = static_cast<double>(
            c->image_draw_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.image_draw_count =
            c->image_draw_count.load(std::memory_order_relaxed);
        timings.text.font_resolve_ms = static_cast<double>(
            c->font_resolve_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.text.shaping_ms = static_cast<double>(
            c->text_shaping_wall_ms.load(std::memory_order_relaxed));
        timings.text.bidi_ms = static_cast<double>(
            c->text_bidi_wall_ms.load(std::memory_order_relaxed));
        timings.text.layout_ms = static_cast<double>(
            c->text_layout_wall_ms.load(std::memory_order_relaxed));
        timings.text.glyph_cache_lookup_ms = static_cast<double>(
            c->glyph_cache_lookup_wall_us.load(std::memory_order_relaxed)) / 1000.0;
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
        timings.framebuffer_allocations =
            c->framebuffer_allocations.load(std::memory_order_relaxed);
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
        timings.gpu.video_decode_wall_ms = c->video_decode_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_hw_transfer_wall_ms = c->video_decode_hw_transfer_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_sws_wall_ms = c->video_decode_sws_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_framebuffer_wall_ms = c->video_decode_framebuffer_wall_ms.load(std::memory_order_relaxed);

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
    const auto atlas_stats = session->renderer->runtime().gpu_text_atlas_cache().stats();
    timings.text.atlas_cache_hits = atlas_stats.cache_hits;
    timings.text.atlas_cache_misses = atlas_stats.cache_misses;
    timings.text.atlas_key_bytes_hashed = atlas_stats.key_bytes_hashed;
    timings.text.atlas_repack_count = atlas_stats.repack_count;
    timings.text.atlas_repack_bytes = atlas_stats.repack_bytes;
    timings.text.atlas_upload_count = atlas_stats.asset_upload_count;
    timings.text.atlas_upload_bytes = atlas_stats.asset_upload_bytes;
    timings.cache.image_cache_hits = session->prepare_timings.image_cache_hits;
    timings.cache.image_cache_misses = session->prepare_timings.image_cache_misses;
    timings.cache.font_cache_hits = session->prepare_timings.font_cache_hits;
    timings.cache.font_cache_misses = session->prepare_timings.font_cache_misses;

    // GPU backend counters (Vulkan) flow into the sidecar's job.gpu object so
    // gpu_execute / gpu_readback are measured next to the encoder phases in a
    // single artifact.  Software backends export nothing → fields stay null.
    if (session->renderer->runtime().backend_attached()) {
        std::vector<std::pair<std::string, std::uint64_t>> gpu_counters;
        session->renderer->runtime().backend().export_gpu_telemetry_counters(gpu_counters);
        for (const auto& [name, value] : gpu_counters) {
            if (name == "gpu_execute_us") {
                timings.gpu.gpu_execute_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "readback_us") {
                timings.gpu.gpu_readback_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "gpu_submit_cpu_us") {
                timings.gpu.gpu_submit_cpu_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "gpu_wait_cpu_us") {
                timings.gpu.gpu_wait_cpu_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "standalone_wait_count") {
                timings.gpu.standalone_wait_count = value;
            } else if (name == "standalone_wait_us") {
                timings.gpu.standalone_wait_us = value;
            } else if (name == "frame_batch_drain_wait_count") {
                timings.gpu.frame_batch_drain_wait_count = value;
            } else if (name == "frame_batch_drain_wait_us") {
                timings.gpu.frame_batch_drain_wait_us = value;
            } else if (name == "frame_slot_wait_count") {
                timings.gpu.frame_slot_wait_count = value;
            } else if (name == "frame_slot_wait_us") {
                timings.gpu.frame_slot_wait_us = value;
            } else if (name == "gpu_readback_bytes") {
                timings.gpu.gpu_readback_bytes = value;
            } else if (name == "gpu_upload_bytes") {
                timings.gpu.gpu_upload_bytes = value;
            } else if (name == "gpu_upload_full_surface_bytes") {
                timings.gpu.gpu_upload_full_surface_bytes = value;
            } else if (name == "gpu_upload_region_bytes") {
                timings.gpu.gpu_upload_region_bytes = value;
            } else if (name.rfind("gpu_upload_", 0) == 0) {
                timings.gpu.upload_breakdown[name] = value;
            } else if (name == "gpu_submissions") {
                timings.gpu.gpu_submissions = value;
            } else if (name == "passes_executed") {
                timings.gpu.passes_executed = value;
            } else if (name == "gpu_nodes") {
                timings.gpu.gpu_nodes = value;
            } else if (name == "software_fallback_nodes") {
                timings.gpu.software_fallback_nodes = value;
            } else if (name == "software_fallback_us") {
                timings.gpu.software_fallback_us = value;
            } else if (name == "fallback_draw_node") {
                timings.gpu.fallback_draw_node = value;
            } else if (name == "fallback_draw_image") {
                timings.gpu.fallback_draw_image = value;
            } else if (name == "fallback_draw_other") {
                timings.gpu.fallback_draw_other = value;
            } else if (name == "fallback_text_run") {
                timings.gpu.fallback_text_run = value;
            } else if (name == "fallback_composite") {
                timings.gpu.fallback_composite = value;
            } else if (name == "fallback_composite_dimensions") {
                timings.gpu.fallback_composite_dimensions = value;
            } else if (name == "fallback_composite_mode") {
                timings.gpu.fallback_composite_mode = value;
            } else if (name == "fallback_effect") {
                timings.gpu.fallback_effect = value;
            } else if (name == "fallback_blur") {
                timings.gpu.fallback_blur = value;
            } else if (name == "fallback_dof") {
                timings.gpu.fallback_dof = value;
            }
        }
    }

    write_frame_timing_sidecar(session->original_output_path,
                               loop_output.telemetry_frames,
                               session->frame_encoder_telemetry,
                               wall_time_ms, loop_output.render_ms, encode_ms,
                               timings, is_native);

    return result;
}

} // namespace chronon3d::cli
