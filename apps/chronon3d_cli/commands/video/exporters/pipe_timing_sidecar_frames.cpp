// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar_frames.cpp — phase 1 of the frame-timing sidecar.
//
// Split out of pipe_timing_sidecar.cpp (pure code move, no schema change):
// shared per-frame JSON section builders + the sorted "frame_times_ms" array.
// ═══════════════════════════════════════════════════════════════════════════

#include "pipe_timing_sidecar_detail.hpp"

#include <algorithm>

namespace chronon3d::cli::pipe_timing_detail {

using chronon3d::telemetry::FrameTelemetry;

double render_total_ms(const FrameTelemetry& f) {
    return f.direct_yuv_decode_ms > 0.0 ? f.direct_yuv_decode_ms : f.graph_eval_ms;
}

nlohmann::json build_render_section(const FrameTelemetry& f) {
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
        {"accounted_cpu_ms", f.render_breakdown.accounted_cpu_ms},
        {"unaccounted_cpu_ms", f.render_breakdown.unaccounted_cpu_ms},
        {"direct_yuv_decode_ms", f.direct_yuv_decode_ms > 0.0 ? nlohmann::json(f.direct_yuv_decode_ms) : nlohmann::json(nullptr)},
        {"total_ms", render_total_ms(f)}
    };
}

nlohmann::json build_conversion_section(bool is_native, const FrameTelemetry* e) {
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
}

nlohmann::json build_encoder_section(bool is_native, const FrameTelemetry* e) {
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
}

nlohmann::json build_image_section(const FrameTelemetry& f) {
    return nlohmann::json{
        {"resolve_ms", nullptr},
        {"io_ms", nullptr},
        {"decode_ms", nullptr},
        {"convert_ms", nullptr},
        {"upload_ms", nullptr},
        {"draw_ms", f.image_timing.draw_ms},
        {"draw_count", f.image_timing.draw_count}
    };
}

nlohmann::json build_text_section(const FrameTelemetry& f) {
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
}

nlohmann::json build_cache_section(const FrameTelemetry& f) {
    return nlohmann::json{
        {"node_lookup_ms", f.node_lookup_ms},
        {"node_hit", f.cache_hit}
    };
}

const FrameTelemetry* find_encoder_frame(
    const std::vector<FrameTelemetry>& enc, int frame_number) {
    const auto it = std::lower_bound(enc.begin(), enc.end(), frame_number,
        [](const FrameTelemetry& f, int n) { return f.frame_number < n; });
    return (it != enc.end() && it->frame_number == frame_number) ? &*it : nullptr;
}

std::vector<double> build_frame_times_section(nlohmann::json& out, SidecarContext& ctx) {
    auto& frames = ctx.frames;
    auto& enc = ctx.enc;

    std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
        return a.frame_number < b.frame_number;
    });
    std::sort(enc.begin(), enc.end(), [](const auto& a, const auto& b) {
        return a.frame_number < b.frame_number;
    });

    out = nlohmann::json{
        {"schema", "chronon3d.frame-timing.v1"},
        {"video", ctx.video_path},
        {"wall_time_ms", ctx.wall_time_ms},
        {"render_ms", ctx.render_ms},
        {"encode_close_ms", ctx.encode_ms},
        {"frames_total", frames.size()},
        {"time_source", "steady_clock"},
        {"frame_times_ms", nlohmann::json::array()}
    };

    std::vector<double> durations;
    durations.reserve(frames.size());

    std::size_t ei = 0;
    for (const auto& frame : frames) {
        while (ei < enc.size() && enc[ei].frame_number < frame.frame_number) ++ei;
        const auto* e = (ei < enc.size() && enc[ei].frame_number == frame.frame_number)
            ? &enc[ei] : nullptr;
        durations.push_back(frame.duration_ms);

        out["frame_times_ms"].push_back({
            {"frame", frame.frame_number},
            {"wall_start_ms", frame.wall_start_ms},
            {"wall_end_ms", frame.wall_start_ms + frame.duration_ms},
            {"wall_duration_ms", frame.duration_ms},
            {"queue_wait_ms", frame.queue_wait_ms},
            {"render", build_render_section(frame)},
            {"conversion", build_conversion_section(ctx.is_native, e)},
            {"encoder", build_encoder_section(ctx.is_native, e)},
            {"image", build_image_section(frame)},
            {"text", build_text_section(frame)},
            {"cache", build_cache_section(frame)},
            {"render_ms", render_total_ms(frame)},
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
            {"dirty_area_ratio", frame.dirty_area_ratio},
            {"fast_path_reused", frame.fast_path_reused},
            {"graph_reused", frame.graph_reused}
        });
    }

    return durations;
}

} // namespace chronon3d::cli::pipe_timing_detail
