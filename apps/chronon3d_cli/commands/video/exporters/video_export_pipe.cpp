#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "../../../utils/process_start.hpp"

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

namespace {

// Job-level cache efficiency metrics written to the `cache` object of the
// sidecar.  Image/font hit-miss come from the prepare barrier (they are
// reset after warmup); node/glyph/gpu hit-miss and node lookup time come
// from the render loop counters.
struct CacheMetrics {
    std::optional<double> node_lookup_ms;
    std::optional<uint64_t> node_cache_hits;
    std::optional<uint64_t> node_cache_misses;
    std::optional<uint64_t> image_cache_hits;
    std::optional<uint64_t> image_cache_misses;
    std::optional<uint64_t> font_cache_hits;
    std::optional<uint64_t> font_cache_misses;
    std::optional<uint64_t> glyph_cache_hits;
    std::optional<uint64_t> glyph_cache_misses;
    std::optional<uint64_t> gpu_asset_cache_hits;
    std::optional<uint64_t> gpu_asset_cache_misses;
};

// Job-level text pipeline timings (cumulative across prepare + render loop).
// font_resolve happens once in the prepare barrier; shaping/bidi/layout are
// prepare-only in steady state; glyph lookup / raster / atlas upload / draw
// accumulate per-frame on the render thread.
struct TextMetrics {
    std::optional<double> font_resolve_ms;
    std::optional<double> shaping_ms;
    std::optional<double> bidi_ms;
    std::optional<double> layout_ms;
    std::optional<double> glyph_cache_lookup_ms;
    std::optional<double> raster_ms;
    std::optional<double> atlas_upload_ms;
    std::optional<double> draw_ms;
};

// Job-level encoder breakdown.  submit_cpu_ms + backpressure_wait_ms are
// the per-frame totals; flush_ms / packet_receive_ms / mux_packet_ms are
// global tails (not 1:1 with a frame, esp. with B-frame reordering);
// device_ms is hardware-encoder-only and stays null on CPU-only encoders.
struct EncoderMetrics {
    std::optional<double> submit_cpu_ms;
    std::optional<double> backpressure_wait_ms;
    std::optional<double> flush_ms;
    std::optional<double> packet_receive_ms;
    std::optional<double> mux_packet_ms;
    std::optional<double> device_ms;
    // Pipe-only: CPU ::write() copy vs poll() back-pressure wait.
    std::optional<double> pipe_write_cpu_ms;
    std::optional<double> pipe_backpressure_wait_ms;
};

// Job-level GPU counters exported by the GPU backend (Vulkan). Fields stay
// nullopt on software backends and emit JSON null — never a misleading 0.
// gpu_execute_ms is GPU-elapsed time from Vulkan timestamp queries;
// gpu_readback_ms is the CPU-side map/memcpy/unmap cost of the surface
// download (the vkCmdCopyImageToBuffer GPU transfer itself is captured in
// gpu_execute_ms / gpu_wait_cpu_ms).
struct GpuMetrics {
    std::optional<double> gpu_execute_ms;
    std::optional<double> gpu_readback_ms;
    std::optional<double> gpu_submit_cpu_ms;
    std::optional<double> gpu_wait_cpu_ms;
    std::optional<uint64_t> gpu_readback_bytes;
    std::optional<uint64_t> gpu_upload_bytes;
    std::optional<uint64_t> gpu_submissions;
    std::optional<uint64_t> passes_executed;
    std::optional<uint64_t> gpu_nodes;
    std::optional<uint64_t> software_fallback_nodes;
    std::optional<uint64_t> software_fallback_us;
    std::optional<uint64_t> fallback_draw_node;
    std::optional<uint64_t> fallback_text_run;
    std::optional<uint64_t> fallback_composite;
    std::optional<uint64_t> fallback_effect;
    std::optional<uint64_t> fallback_blur;
    std::optional<uint64_t> fallback_dof;
};

// Job-level timings written to the `job` object of the frame-timing sidecar.
// Fields not measurable inside the pipe-export path (process startup, plan
// read/parse/validate/compile, graph compile) stay nullopt and emit as JSON
// null — an honest "not measured here" rather than a misleading 0.0.
struct JobTimings {
    std::optional<double> process_wall_ms;
    std::optional<double> job_wall_ms;
    std::optional<double> engine_init_ms;
    std::optional<double> backend_init_ms;
    std::optional<double> plan_read_ms;
    std::optional<double> plan_parse_ms;
    std::optional<double> plan_validate_ms;
    std::optional<double> plan_compile_ms;
    std::optional<double> graph_compile_ms;
    std::optional<double> prepare_ms;
    std::optional<double> render_loop_wall_ms;
    std::optional<double> encoder_finalize_ms;
    std::optional<double> mux_finalize_ms;
    std::optional<double> output_finalize_ms;
    std::optional<double> validation_ms;
    // Output-contract verification split: the ffprobe subprocess and the
    // SHA-256 digest are timed separately so neither cost is hidden behind
    // the combined validation_ms.
    std::optional<double> ffprobe_ms;
    std::optional<double> sha256_ms;
    // Framebuffer allocation events across the render loop (post-warmup),
    // used to derive the per-frame allocation rate in the sidecar.
    std::optional<uint64_t> framebuffer_allocations;
    std::optional<double> image_draw_ms;
    std::optional<uint64_t> image_draw_count;
    CacheMetrics cache;
    TextMetrics text;
    EncoderMetrics encoder;
    GpuMetrics gpu;
    chronon3d::runtime::RenderPreparationTimings prepare;
    // Target output frame rate, used to derive the per-frame budget and the
    // over-budget count in the summary.
    std::optional<int> target_fps;
};

void write_frame_timing_sidecar(
    const std::string& video_path,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& render_frames,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& encoder_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms,
    const JobTimings& timings,
    bool is_native)
{
    if (render_frames.empty()) return;

    std::vector<chronon3d::telemetry::FrameTelemetry> frames = render_frames;
    std::vector<chronon3d::telemetry::FrameTelemetry> enc = encoder_frames;
    std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
        return a.frame_number < b.frame_number;
    });
    std::sort(enc.begin(), enc.end(), [](const auto& a, const auto& b) {
        return a.frame_number < b.frame_number;
    });

    nlohmann::json out{
        {"schema", "chronon3d.frame-timing.v1"},
        {"video", video_path},
        {"wall_time_ms", wall_time_ms},
        {"render_ms", render_ms},
        {"encode_close_ms", encode_ms},
        {"frames_total", frames.size()},
        {"time_source", "steady_clock"},
        {"frame_times_ms", nlohmann::json::array()}
    };

    std::vector<double> durations;
    durations.reserve(frames.size());

    // Per-frame section builders, shared by the frame records and the
    // `first_frame` deep-dive in the summary.  Sections mirror the
    // architectural boundaries; sub-fields that are not measurable on this
    // path emit JSON null, never a misleading 0.0.
    const auto build_render_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"timeline_eval_ms", f.render_breakdown.timeline_eval_ms},
            {"animation_eval_ms", nullptr},
            {"text_ms", f.render_breakdown.text_ms},
            {"graph_prepare_ms", f.render_breakdown.graph_prepare_ms},
            {"graph_execute_ms", f.render_breakdown.graph_execute_ms},
            {"compositing_ms", f.render_breakdown.compositing_ms},
            {"effects_ms", f.render_breakdown.effects_ms},
            {"surface_management_ms", f.render_breakdown.surface_management_ms},
            {"backend_overhead_ms", f.render_breakdown.backend_overhead_ms},
            {"total_ms", f.graph_eval_ms}
        };
    };
    // Pixel conversion decomposed into its architectural boundaries:
    // float→RGBA8 (pixel_format) + RGBA→YUV (color_space) are measured;
    // scale/cpu_copy/gpu_readback/encoder_buffer_copy are not separable
    // on the software path (fused into sws_scale / direct slot write) and
    // emit null, never a misleading 0.0.
    const auto build_conversion_section = [is_native](const chronon3d::telemetry::FrameTelemetry* e) {
        nlohmann::json section{
            {"pixel_format_convert_ms", e ? e->pixel_format_convert_ms : 0.0},
            {"color_space_convert_ms", e ? e->color_space_convert_ms : 0.0},
            {"scale_ms", nullptr},
            {"cpu_copy_ms", nullptr},
            {"gpu_readback_ms", nullptr},
            {"encoder_buffer_copy_ms", nullptr},
            {"convert_ms", nullptr},
            {"copy_ms", nullptr},
            {"total_ms", e ? e->conversion_copy_ms : 0.0}
        };
        if (is_native && e) {
            section["convert_ms"] = e->native_convert_ms;
        }
        return section;
    };
    // Encoder: native exposes submit CPU + EAGAIN back-pressure per frame;
    // pipe exposes the pipe-write split (CPU copy vs poll() back-pressure
    // wait).  Flush / packet receive / mux packet are global (B-frame
    // reordering means they are not 1:1 with a frame), and device time is
    // GPU-only — all null per-frame.
    const auto build_encoder_section = [is_native](const chronon3d::telemetry::FrameTelemetry* e) {
        nlohmann::json section{
            {"submit_cpu_ms", nullptr},
            {"backpressure_wait_ms", nullptr},
            {"pipe_write_cpu_ms", nullptr},
            {"pipe_backpressure_wait_ms", nullptr},
            {"flush_ms", nullptr},
            {"packet_receive_ms", nullptr},
            {"mux_packet_ms", nullptr},
            {"device_ms", nullptr}
        };
        if (e) {
            if (is_native) {
                section["submit_cpu_ms"] = e->encoder_ms;
                section["backpressure_wait_ms"] = e->backpressure_wait_ms;
            } else {
                section["pipe_write_cpu_ms"] = e->pipe_write_cpu_ms;
                section["pipe_backpressure_wait_ms"] = e->pipe_backpressure_wait_ms;
            }
        }
        return section;
    };
    // Image asset pipeline: resolve/io/decode/convert happen once in the
    // prepare barrier and upload is software-unavailable, so only the
    // per-frame draw phase is populated here; the rest emit null.
    const auto build_image_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"resolve_ms", nullptr},
            {"io_ms", nullptr},
            {"decode_ms", nullptr},
            {"convert_ms", nullptr},
            {"upload_ms", nullptr},
            {"draw_ms", f.image_timing.draw_ms},
            {"draw_count", f.image_timing.draw_count}
        };
    };
    // Text pipeline: font_resolve is prepare-only (null here); shaping /
    // bidi / layout are prepare-only in steady state (≈ 0 delta); glyph
    // lookup / raster / atlas upload / draw accumulate each frame.
    const auto build_text_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"font_resolve_ms", nullptr},
            {"shaping_ms", f.text_timing.shaping_ms},
            {"bidi_ms", f.text_timing.bidi_ms},
            {"layout_ms", f.text_timing.layout_ms},
            {"glyph_cache_lookup_ms", f.text_timing.glyph_cache_lookup_ms},
            {"raster_ms", f.text_timing.raster_ms},
            {"atlas_upload_ms", f.text_timing.atlas_upload_ms},
            {"draw_ms", f.text_timing.draw_ms}
        };
    };
    // Per-frame cache: node cache lookup time + whether this frame was a
    // cache hit (fast-path reuse or at least one node cache hit).
    const auto build_cache_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"node_lookup_ms", f.node_lookup_ms},
            {"node_hit", f.cache_hit}
        };
    };
    const auto find_encoder = [&enc](int frame_number) -> const chronon3d::telemetry::FrameTelemetry* {
        const auto it = std::lower_bound(enc.begin(), enc.end(), frame_number,
            [](const chronon3d::telemetry::FrameTelemetry& f, int n) { return f.frame_number < n; });
        return (it != enc.end() && it->frame_number == frame_number) ? &*it : nullptr;
    };

    std::size_t ei = 0;
    for (const auto& frame : frames) {
        while (ei < enc.size() && enc[ei].frame_number < frame.frame_number) ++ei;
        const auto* e = (ei < enc.size() && enc[ei].frame_number == frame.frame_number)
            ? &enc[ei] : nullptr;
        durations.push_back(frame.duration_ms);

        // Wall timeline of this frame, measured with the loop's monotonic
        // clock (wall_start_ms is an offset from render-loop start; never
        // reconstructed from FPS).
        out["frame_times_ms"].push_back({
            {"frame", frame.frame_number},
            {"wall_start_ms", frame.wall_start_ms},
            {"wall_end_ms", frame.wall_start_ms + frame.duration_ms},
            {"wall_duration_ms", frame.duration_ms},
            {"queue_wait_ms", frame.queue_wait_ms},
            {"render", build_render_section(frame)},
            {"conversion", build_conversion_section(e)},
            {"encoder", build_encoder_section(e)},
            {"image", build_image_section(frame)},
            {"text", build_text_section(frame)},
            {"cache", build_cache_section(frame)},
            {"render_ms", frame.graph_eval_ms},
            {"end_to_end_render_thread_ms", frame.duration_ms},
            {"conversion_copy_ms", e ? e->conversion_copy_ms : 0.0},
            {"encoder_ms", e ? e->encoder_ms : 0.0},
            {"pipe_write_ms", e ? e->pipe_write_ms : 0.0},
            {"native_convert_ms", e ? e->native_convert_ms : 0.0},
            {"native_send_ms", e ? e->native_send_ms : 0.0},
            {"native_receive_ms", e ? e->native_receive_ms : 0.0},
            {"native_mux_ms", e ? e->native_mux_ms : 0.0},
            {"node_lookup_ms", frame.node_lookup_ms},
            {"cache_hit", frame.cache_hit},
            {"dirty_area_ratio", frame.dirty_area_ratio}
        });
    }

    // Canonical per-frame timing summary (first/mean/p95/p99 + warmup /
    // steady-state), computed once from the frame records — NOT re-derived
    // inline.  The preset certification harness shares this exact summary.
    const auto stats = chronon3d::telemetry::summarize_frame_timings(frames);
    const std::size_t count = frames.size();

    // Frame budget: end-to-end frame wall duration vs the target frame
    // interval.  A frame over budget is a throughput/stutter signal (cache
    // miss, allocator spike, encoder back-pressure).
    double frame_budget_ms = 0.0;
    uint64_t frames_over_budget = 0;
    if (timings.target_fps && *timings.target_fps > 0) {
        frame_budget_ms = 1000.0 / static_cast<double>(*timings.target_fps);
        for (double value : durations) {
            if (value > frame_budget_ms) ++frames_over_budget;
        }
    }
    const double over_budget_ratio = count > 0
        ? static_cast<double>(frames_over_budget) / static_cast<double>(count) : 0.0;

    // Three distinct throughput views (frames/second):
    //   render_only_fps — pure Chronon pipeline: sum of per-frame graph eval
    //                     (graph + cache + pixel ops), excluding conversion /
    //                     copy / queue wait / encode.
    //   render_loop_fps — the render thread's wall window (first frame → last
    //                     frame rendered), incl. per-frame queue wait.
    //   end_to_end_fps  — the whole job (setup + render + encode + close).
    // realtime_factor is end_to_end_fps / target_fps — how many times faster
    // than realtime the export runs (dashboards love this single number).
    double render_only_ms = 0.0;
    for (const auto& f : frames) render_only_ms += f.graph_eval_ms;
    const double end_to_end_fps = wall_time_ms > 0.0
        ? (1000.0 * static_cast<double>(count) / wall_time_ms) : 0.0;
    const double render_loop_fps = render_ms > 0.0
        ? (1000.0 * static_cast<double>(count) / render_ms) : 0.0;
    const double render_only_fps = render_only_ms > 0.0
        ? (1000.0 * static_cast<double>(count) / render_only_ms) : 0.0;
    const double realtime_factor =
        (timings.target_fps && *timings.target_fps > 0 && end_to_end_fps > 0.0)
        ? (end_to_end_fps / static_cast<double>(*timings.target_fps)) : 0.0;

    // First-frame deep dive: the full frame-0 breakdown so the cold-start
    // cost (lazy font/glyph/asset/surface work, cache misses) is visible
    // instead of being masked by the steady-state average.  Sections mirror
    // the per-frame records; not-measurable fields emit null.
    const auto& first = frames.front();
    const auto* first_e = find_encoder(first.frame_number);
    const nlohmann::json first_frame{
        {"frame", first.frame_number},
        {"wall_duration_ms", first.duration_ms},
        {"queue_wait_ms", first.queue_wait_ms},
        {"render_total_ms", first.graph_eval_ms},
        {"render", build_render_section(first)},
        {"conversion", build_conversion_section(first_e)},
        {"encoder", build_encoder_section(first_e)},
        {"image", build_image_section(first)},
        {"text", build_text_section(first)},
        {"cache", build_cache_section(first)}
    };

    nlohmann::json summary{
        {"first_frame_ms", stats.first_frame_ms},
        {"min_frame_ms", stats.min_frame_ms},
        {"max_frame_ms", stats.max_frame_ms},
        {"mean_frame_ms", stats.mean_frame_ms},
        {"avg_frame_ms", stats.mean_frame_ms},
        {"stddev_frame_ms", stats.stddev_frame_ms},
        {"p50_frame_ms", stats.p50_frame_ms},
        {"p90_frame_ms", stats.p90_frame_ms},
        {"p95_frame_ms", stats.p95_frame_ms},
        {"p99_frame_ms", stats.p99_frame_ms},
        {"warmup_frames", stats.warmup_frames},
        {"warmup_avg_ms", stats.warmup_frames > 0 ? nlohmann::json(stats.warmup_avg_ms) : nlohmann::json(nullptr)},
        {"steady_avg_ms", stats.steady_avg_ms},
        {"steady_p50_ms", stats.steady_p50_ms},
        {"steady_p95_ms", stats.steady_p95_ms},
        {"steady_p99_ms", stats.steady_p99_ms},
        {"render_only_fps", render_only_fps},
        {"render_loop_fps", render_loop_fps},
        {"end_to_end_fps", end_to_end_fps},
        {"measured_fps", end_to_end_fps},
        {"realtime_factor", realtime_factor},
        {"frame_budget_ms", frame_budget_ms},
        {"frames_over_budget", frames_over_budget},
        {"over_budget_ratio", over_budget_ratio}
    };
    summary["first_frame"] = std::move(first_frame);
    if (timings.target_fps) {
        summary["target_fps"] = *timings.target_fps;
    } else {
        summary["target_fps"] = nullptr;
    }
    out["summary"] = std::move(summary);

    auto& job = out["job"];
    const auto put_ms = [&job](const char* key, const std::optional<double>& value) {
        if (value) job[key] = *value; else job[key] = nullptr;
    };
    put_ms("process_wall_ms", timings.process_wall_ms);
    put_ms("job_wall_ms", timings.job_wall_ms);
    put_ms("engine_init_ms", timings.engine_init_ms);
    put_ms("backend_init_ms", timings.backend_init_ms);
    put_ms("plan_read_ms", timings.plan_read_ms);
    put_ms("plan_parse_ms", timings.plan_parse_ms);
    put_ms("plan_validate_ms", timings.plan_validate_ms);
    put_ms("plan_compile_ms", timings.plan_compile_ms);
    put_ms("graph_compile_ms", timings.graph_compile_ms);
    put_ms("prepare_ms", timings.prepare_ms);
    put_ms("render_loop_wall_ms", timings.render_loop_wall_ms);
    put_ms("encoder_finalize_ms", timings.encoder_finalize_ms);
    put_ms("mux_finalize_ms", timings.mux_finalize_ms);
    put_ms("output_finalize_ms", timings.output_finalize_ms);
    put_ms("validation_ms", timings.validation_ms);
    put_ms("ffprobe_ms", timings.ffprobe_ms);
    put_ms("sha256_ms", timings.sha256_ms);

    // Job-level GPU counters: measured only by a GPU backend (Vulkan). The
    // software path emits null — never a misleading 0.
    auto& gpu = job["gpu"];
    const auto put_gpu = [&gpu](const char* key, const std::optional<double>& value) {
        if (value) gpu[key] = *value; else gpu[key] = nullptr;
    };
    const auto put_gpu_u64 = [&gpu](const char* key, const std::optional<uint64_t>& value) {
        if (value) gpu[key] = *value; else gpu[key] = nullptr;
    };
    put_gpu("gpu_execute_ms", timings.gpu.gpu_execute_ms);
    put_gpu("gpu_readback_ms", timings.gpu.gpu_readback_ms);
    put_gpu("gpu_submit_cpu_ms", timings.gpu.gpu_submit_cpu_ms);
    put_gpu("gpu_wait_cpu_ms", timings.gpu.gpu_wait_cpu_ms);
    put_gpu_u64("gpu_readback_bytes", timings.gpu.gpu_readback_bytes);
    put_gpu_u64("gpu_upload_bytes", timings.gpu.gpu_upload_bytes);
    put_gpu_u64("gpu_submissions", timings.gpu.gpu_submissions);
    put_gpu_u64("passes_executed", timings.gpu.passes_executed);
    put_gpu_u64("gpu_nodes", timings.gpu.gpu_nodes);
    put_gpu_u64("software_fallback_nodes", timings.gpu.software_fallback_nodes);
    put_gpu_u64("software_fallback_us", timings.gpu.software_fallback_us);
    put_gpu_u64("fallback_draw_node", timings.gpu.fallback_draw_node);
    put_gpu_u64("fallback_text_run", timings.gpu.fallback_text_run);
    put_gpu_u64("fallback_composite", timings.gpu.fallback_composite);
    put_gpu_u64("fallback_effect", timings.gpu.fallback_effect);
    put_gpu_u64("fallback_blur", timings.gpu.fallback_blur);
    put_gpu_u64("fallback_dof", timings.gpu.fallback_dof);
    std::string effective_backend = "unknown";
    if (timings.gpu.gpu_nodes && *timings.gpu.gpu_nodes > 0) {
        effective_backend = timings.gpu.software_fallback_nodes &&
                *timings.gpu.software_fallback_nodes > 0
            ? "hybrid" : "vulkan";
    } else if (timings.gpu.software_fallback_nodes &&
               *timings.gpu.software_fallback_nodes > 0) {
        effective_backend = "software-fallback";
    }
    gpu["effective_backend"] = effective_backend;

    auto& prepare = job["prepare"];
    const auto put_prepare = [&prepare](const char* key, const std::optional<double>& value) {
        if (value) prepare[key] = *value; else prepare[key] = nullptr;
    };
    put_prepare("asset_preflight_ms", timings.prepare.asset_preflight_ms);
    put_prepare("asset_resolve_ms", timings.prepare.asset_resolve_ms);
    put_prepare("asset_decode_ms", timings.prepare.asset_decode_ms);
    put_prepare("font_resolve_ms", timings.prepare.font_resolve_ms);
    put_prepare("font_load_ms", timings.prepare.font_load_ms);
    put_prepare("text_shape_ms", timings.prepare.text_shape_ms);
    put_prepare("text_layout_ms", timings.prepare.text_layout_ms);
    put_prepare("glyph_raster_ms", timings.prepare.glyph_raster_ms);
    put_prepare("glyph_atlas_upload_ms", timings.prepare.glyph_atlas_upload_ms);
    put_prepare("plan_compile_ms", timings.prepare.plan_compile_ms);
    put_prepare("graph_compile_ms", timings.prepare.graph_compile_ms);
    put_prepare("resource_plan_ms", timings.prepare.resource_plan_ms);
    put_prepare("backend_prepare_ms", timings.prepare.backend_prepare_ms);

    // Job-level image asset pipeline: prepare-barrier phases + render-loop
    // draw totals.  `io_ms` (fused into decode by the stb backend) and
    // `upload_ms`/`upload_count` (GPU-only) stay null on the software path.
    auto& image = job["image"];
    const auto put_image = [&image](const char* key, const std::optional<double>& value) {
        if (value) image[key] = *value; else image[key] = nullptr;
    };
    put_image("resolve_ms", timings.prepare.image_resolve_ms);
    put_image("io_ms", timings.prepare.image_io_ms);
    put_image("decode_ms", timings.prepare.image_decode_ms);
    put_image("convert_ms", timings.prepare.image_convert_ms);
    put_image("upload_ms", timings.prepare.image_upload_ms);
    put_image("draw_ms", timings.image_draw_ms);
    const auto put_image_u64 = [&image](const char* key, const std::optional<uint64_t>& value) {
        if (value) image[key] = *value; else image[key] = nullptr;
    };
    put_image_u64("decode_count", timings.prepare.image_decode_count);
    put_image_u64("upload_count", timings.prepare.image_upload_count);
    put_image_u64("draw_count", timings.image_draw_count);

    // Job-level text pipeline: font_resolve (prepare) + shaping/bidi/layout
    // (prepare-only in steady state) + glyph lookup/raster/atlas upload/draw
    // (render-loop cumulative).
    auto& text = job["text"];
    const auto put_text = [&text](const char* key, const std::optional<double>& value) {
        if (value) text[key] = *value; else text[key] = nullptr;
    };
    put_text("font_resolve_ms", timings.text.font_resolve_ms);
    put_text("shaping_ms", timings.text.shaping_ms);
    put_text("bidi_ms", timings.text.bidi_ms);
    put_text("layout_ms", timings.text.layout_ms);
    put_text("glyph_cache_lookup_ms", timings.text.glyph_cache_lookup_ms);
    put_text("raster_ms", timings.text.raster_ms);
    put_text("atlas_upload_ms", timings.text.atlas_upload_ms);
    put_text("draw_ms", timings.text.draw_ms);

    // Job-level encoder breakdown: per-frame submit/back-pressure totals +
    // global flush/receive/mux tails.  device_ms is hardware-only (null).
    auto& encoder = job["encoder"];
    const auto put_encoder = [&encoder](const char* key, const std::optional<double>& value) {
        if (value) encoder[key] = *value; else encoder[key] = nullptr;
    };
    put_encoder("submit_cpu_ms", timings.encoder.submit_cpu_ms);
    put_encoder("backpressure_wait_ms", timings.encoder.backpressure_wait_ms);
    put_encoder("flush_ms", timings.encoder.flush_ms);
    put_encoder("packet_receive_ms", timings.encoder.packet_receive_ms);
    put_encoder("mux_packet_ms", timings.encoder.mux_packet_ms);
    put_encoder("device_ms", timings.encoder.device_ms);
    put_encoder("pipe_write_cpu_ms", timings.encoder.pipe_write_cpu_ms);
    put_encoder("pipe_backpressure_wait_ms", timings.encoder.pipe_backpressure_wait_ms);

    // Job-level cache efficiency: node lookup time + hit/miss for every cache
    // domain + glyph hit ratio.  Image/font counts come from prepare; node/
    // glyph/gpu counts and lookup time come from the render loop.
    auto& cache = out["cache"];
    if (timings.cache.node_lookup_ms) cache["node_lookup_ms"] = *timings.cache.node_lookup_ms;
    else cache["node_lookup_ms"] = nullptr;
    const auto put_cache_u64 = [&cache](const char* key, const std::optional<uint64_t>& value) {
        if (value) cache[key] = *value; else cache[key] = nullptr;
    };
    put_cache_u64("node_cache_hits", timings.cache.node_cache_hits);
    put_cache_u64("node_cache_misses", timings.cache.node_cache_misses);
    put_cache_u64("image_cache_hits", timings.cache.image_cache_hits);
    put_cache_u64("image_cache_misses", timings.cache.image_cache_misses);
    put_cache_u64("font_cache_hits", timings.cache.font_cache_hits);
    put_cache_u64("font_cache_misses", timings.cache.font_cache_misses);
    put_cache_u64("glyph_cache_hits", timings.cache.glyph_cache_hits);
    put_cache_u64("glyph_cache_misses", timings.cache.glyph_cache_misses);
    put_cache_u64("gpu_asset_cache_hits", timings.cache.gpu_asset_cache_hits);
    put_cache_u64("gpu_asset_cache_misses", timings.cache.gpu_asset_cache_misses);

    const uint64_t glyph_hits = timings.cache.glyph_cache_hits.value_or(uint64_t{0});
    const uint64_t glyph_misses = timings.cache.glyph_cache_misses.value_or(uint64_t{0});
    cache["glyph_hit_ratio"] = (glyph_hits + glyph_misses > 0)
        ? (static_cast<double>(glyph_hits) / static_cast<double>(glyph_hits + glyph_misses))
        : 0.0;

    // Framebuffer allocation rate (the only per-frame allocation event rate
    // the engine measures). Emitted as null when the counter is unavailable
    // rather than inventing a heap-allocator estimate.
    auto& memory = out["memory"];
    if (timings.framebuffer_allocations) {
        memory["framebuffer_allocations"] = *timings.framebuffer_allocations;
        memory["framebuffer_allocations_per_frame"] = count > 0
            ? (static_cast<double>(*timings.framebuffer_allocations) / static_cast<double>(count))
            : 0.0;
    } else {
        memory["framebuffer_allocations"] = nullptr;
        memory["framebuffer_allocations_per_frame"] = nullptr;
    }

    const auto sidecar = std::filesystem::path(video_path).string() + ".timing.json";
    std::ofstream file(sidecar);
    if (!file) {
        spdlog::warn("[video] Could not write frame timing sidecar: {}", sidecar);
        return;
    }
    file << out.dump(2) << '\n';
    spdlog::info("[video] Wrote exact per-frame timing sidecar: {}", sidecar);
}

} // namespace

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
    auto loop_output = run_pipe_export_loop(*session, registry, compiled, render_opts, start, end, opts
);

    // Phase 6 — Encoder close
    auto close_result = close_pipe_encoder(*session);

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
    JobTimings timings;
    timings.job_wall_ms = wall_time_ms;
    timings.process_wall_ms = profiling::duration_ms(process_start_time(), wall_t0);
    timings.prepare_ms = profiling::duration_ms(setup_t0, warmup_t1);
    timings.engine_init_ms = session->engine_init_ms;
    timings.backend_init_ms = session->backend_init_ms;
    timings.render_loop_wall_ms = loop_output.render_ms;
    timings.encoder_finalize_ms = encode_ms;
    if (is_native) timings.mux_finalize_ms = close_result.native_trailer_ms;
    if (is_native) {
        timings.encoder.submit_cpu_ms = close_result.native_send_ms;
        timings.encoder.backpressure_wait_ms = close_result.native_backpressure_ms;
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
    }
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
            } else if (name == "gpu_readback_bytes") {
                timings.gpu.gpu_readback_bytes = value;
            } else if (name == "gpu_upload_bytes") {
                timings.gpu.gpu_upload_bytes = value;
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
            } else if (name == "fallback_text_run") {
                timings.gpu.fallback_text_run = value;
            } else if (name == "fallback_composite") {
                timings.gpu.fallback_composite = value;
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
