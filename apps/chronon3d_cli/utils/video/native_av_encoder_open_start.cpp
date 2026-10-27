#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"
#include "ffmpeg_profile_constants.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/keyframe_cadence.hpp>
#include <fmt/format.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mathematics.h>
#include <libavcodec/version.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#include <libavutil/hwcontext_cuda.h>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#endif
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

bool NativeAvEncoder::wire_container(const std::string& filename) {
    if (!options_.output_segments.empty()) {
        if (options_.borrowed_mux || !options_.audio_source_path.empty()) {
            spdlog::error("[native_av] segmented output does not support borrowed mux or audio");
            return false;
        }
        AVCodecParameters* params = avcodec_parameters_alloc();
        if (!params || avcodec_parameters_from_context(params, codec_) < 0) {
            avcodec_parameters_free(&params);
            spdlog::error("[native_av] failed to snapshot encoder parameters for segmented output");
            return false;
        }
        segmented_mux_ = std::make_unique<chronon3d::media::MuxOutputSegmentWriter>();
        std::string reason;
        // P4 batch superjob: the per-item segments are cut at forced-IDR
        // boundaries, and the session's output_path doubles as the whole-
        // timeline MIRROR (the batch's fused clip) so the "batch consumed as
        // one clip" contract keeps its MP4 while every item stays its own
        // artifact. One codec, one EOF, N+1 muxes finalized together.
        const bool opened = segmented_mux_->open(
            params, codec_->time_base, options_.output_segments,
            options_.fragmented_mp4, reason, options_.output_path);
        avcodec_parameters_free(&params);
        if (!opened) {
            spdlog::error("[native_av] segmented mux open failed: {}", reason);
            segmented_mux_.reset();
            return false;
        }
        mux_ = nullptr;
        return true;
    }
    std::optional<chronon3d::media::AudioStreamConfig> audio_config;
    if (!options_.audio_source_path.empty()) {
        if (avformat_open_input(&audio_input_, options_.audio_source_path.c_str(),
                                nullptr, nullptr) < 0 ||
            avformat_find_stream_info(audio_input_, nullptr) < 0) {
            spdlog::error("[native_av] audio source open failed: {}", options_.audio_source_path);
            return false;
        }
        audio_input_stream_ = av_find_best_stream(
            audio_input_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audio_input_stream_ < 0) {
            spdlog::error("[native_av] audio source has no audio stream: {}", options_.audio_source_path);
            return false;
        }
        audio_config = chronon3d::media::AudioStreamConfig{
            .params = audio_input_->streams[audio_input_stream_]->codecpar,
            .time_base = audio_input_->streams[audio_input_stream_]->time_base,
            .target_sample_rate = options_.audio_target_sample_rate};
    }

    // ── Container ownership (P0-5C) ──────────────────────────────────────
    // Borrowed: the caller owns the container (its video stream is already
    // declared from the contract the splice gate certified) and owns audio and
    // the trailer too, so nothing is opened or finalized here.
    // Owned: this encoder opens the container and finalizes it in close().
    if (options_.borrowed_mux) {
        if (codec_ == nullptr) return false;
        borrows_mux_ = true;
        mux_ = options_.borrowed_mux;
        // P0-5E — placement is the container's business: the owner of the
        // borrowed session sets the scheduled origin on it
        // (MuxSession::set_video_origin) and this session submits with
        // MuxVideoPlacement::ScheduledOrigin, so its packets land on the tick
        // the schedule reserved for the work unit it just rendered.
        spdlog::info(
            "[native_av] borrowing the caller-owned mux session (close() will "
            "only flush the encoder; the caller owns audio + trailer); "
            "scheduled origin tick={} (container time base)",
            options_.borrowed_mux->video_origin());
    } else {
        owned_mux_ = std::make_unique<chronon3d::media::MuxSession>();
        std::string mux_reason;
        if (!owned_mux_->open(chronon3d::media::MuxOpenConfig{
                .output_path = filename,
                .video_codec = codec_,
                .audio = audio_config,
                .fragmented_mp4 = options_.fragmented_mp4}, mux_reason)) {
            spdlog::error("[native_av] mux session open failed: {}", mux_reason);
            return false;
        }
        mux_ = owned_mux_.get();
    }
    return true;
}

bool NativeAvEncoder::finalize_open(const std::string& filename) {
    const auto& resolved = *resolved_encoder_config_;

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        spdlog::error("[native_av] av_frame_alloc or av_packet_alloc failed");
        return false;
    }

    frame_->format = codec_->pix_fmt;
    frame_->width = codec_->width;
    frame_->height = codec_->height;
    frame_->color_range = codec_->color_range;
    frame_->colorspace = codec_->colorspace;
    frame_->color_trc = codec_->color_trc;
    frame_->color_primaries = codec_->color_primaries;
    if (!gpu_nvenc_ && av_frame_get_buffer(frame_, 32) < 0) {
        spdlog::error("[native_av] av_frame_get_buffer failed");
        return false;
    }

    spdlog::info(
        "[native_av] Opened native encoder: {}x{} @ {}fps, codec={}, rc={}, preset={}, output='{}'",
        options_.width, options_.height,
        static_cast<double>(options_.fps_num) / options_.fps_den,
        resolved.encoder_name(), resolved.rate_control_label(),
        resolved.preset_label(), filename);
    open_complete_ = true;
    return true;
}

} // namespace chronon3d::cli
