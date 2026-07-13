#include "native_av_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

// Convenience: steady clock helpers
namespace {
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
}

namespace chronon3d::cli {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Map our options to the codec name that avcodec_find_encoder_by_name expects.
static const char* resolve_encoder_name(const FfmpegPipeOptions& opt) {
    if (opt.codec == "libx264rgb")
        return "libx264rgb";
    return "libx264";
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

bool NativeAvEncoder::open(const FfmpegPipeOptions& options) {
    if (fmt_ || codec_) {
        spdlog::error("[native_av] Encoder already open");
        return false;
    }

    if (options.width <= 0 || options.height <= 0 ||
        options.fps <= 0 || options.output_path.empty()) {
        spdlog::error("[native_av] Invalid encoder options (w={}, h={}, fps={}, path='{}')",
                      options.width, options.height, options.fps, options.output_path);
        return false;
    }

    options_ = options;
    frames_written_ = 0;

    // Reset conversion cache so a stale digest from a prior export won't
    // produce a false hit if the same object is reopened.
    last_converted_digest_       = 0;
    last_converted_width_        = 0;
    last_converted_height_       = 0;
    last_converted_pix_fmt_      = -1;
    last_converted_apply_gamma_  = false;
    last_converted_color_matrix_ = -1;
    cache_hits_   = 0;
    cache_misses_ = 0;

    // Reset telemetry accumulators
    native_convert_ms_         = 0.0;
    native_send_frame_ms_      = 0.0;
    native_receive_packet_ms_  = 0.0;
    native_mux_write_ms_       = 0.0;
    native_trailer_ms_         = 0.0;

    // 1. Allocate format context (MP4 muxer)
    const std::string filename = options_.output_path;
    avformat_alloc_output_context2(&fmt_, nullptr, nullptr, filename.c_str());
    if (!fmt_) {
        spdlog::error("[native_av] avformat_alloc_output_context2 failed for '{}'", filename);
        return false;
    }

    // 2. Find the encoder
    const AVCodec* encoder = avcodec_find_encoder_by_name(resolve_encoder_name(options_));
    if (!encoder) {
        spdlog::error("[native_av] avcodec_find_encoder_by_name('{}') failed", resolve_encoder_name(options_));
        return false;
    }

    // 3. Allocate codec context
    codec_ = avcodec_alloc_context3(encoder);
    if (!codec_) {
        spdlog::error("[native_av] avcodec_alloc_context3 failed");
        return false;
    }

    codec_->width     = options_.width;
    codec_->height    = options_.height;
    codec_->time_base = AVRational{1, options_.fps};
    codec_->framerate = AVRational{options_.fps, 1};
    // GOP size = 1 second (fps frames) for reliable decoder compatibility.
    // With fps*2, the very subtle tracking_breathing animation (4% scale over
    // 120 frames) produces P-frames with 99.7% skip on the "ultrafast" preset
    // — some H.264 decoders can't decode those near-empty P-frames, resulting
    // in visible frames only at keyframe intervals.  fps*2 worked for most
    // content but broke for near-static scenes with tiny per-frame changes.
    codec_->gop_size  = options_.fps;  // was fps * 2
    codec_->max_b_frames = 0;              // no B-frames for lowest latency

    // Pixel format: always YUV420P for H.264 (the converter handles it)
    codec_->pix_fmt = AV_PIX_FMT_YUV420P;

    // Set encoder options (preset, crf, tune, threads)
    if (!options_.preset.empty()) {
        av_opt_set(codec_->priv_data, "preset", options_.preset.c_str(), 0);
    }
    {
        char crf_str[16];
        snprintf(crf_str, sizeof(crf_str), "%d", options_.crf);
        av_opt_set(codec_->priv_data, "crf", crf_str, 0);
    }
    // Apply the unified CPU budget to x264.  encode_threads == 0 keeps the
    // legacy "auto" behaviour for backwards compatibility.
    const std::string codec_name = resolve_encoder_name(options_);
    if (codec_name == "libx264" || codec_name == "libx264rgb") {
        if (options_.encode_threads > 0) {
            char threads_str[16];
            snprintf(threads_str, sizeof(threads_str), "%d", options_.encode_threads);
            av_opt_set(codec_->priv_data, "threads", threads_str, 0);
        } else {
            av_opt_set(codec_->priv_data, "threads", "auto", 0);
        }
        av_opt_set(codec_->priv_data, "thread_type", "frame", 0);
    }
    // tune: default empty for batch export (faster), use "zerolatency" only for streaming
    const std::string tune = options_.tune.empty() ? "" : options_.tune;
    if (!tune.empty()) {
        av_opt_set(codec_->priv_data, "tune", tune.c_str(), 0);
    }

    // Global header if the container format requires it
    if (fmt_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // 4. Open the codec
    if (avcodec_open2(codec_, encoder, nullptr) < 0) {
        spdlog::error("[native_av] avcodec_open2 failed");
        return false;
    }

    // 5. Create stream
    stream_ = avformat_new_stream(fmt_, nullptr);
    if (!stream_) {
        spdlog::error("[native_av] avformat_new_stream failed");
        return false;
    }
    stream_->time_base = codec_->time_base;

    if (avcodec_parameters_from_context(stream_->codecpar, codec_) < 0) {
        spdlog::error("[native_av] avcodec_parameters_from_context failed");
        return false;
    }

    // 6. Open output file
    if (!(fmt_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
            spdlog::error("[native_av] avio_open failed for '{}'", filename);
            return false;
        }
    }

    // 7. Write header (no +faststart for benchmarks — adds write-seek overhead)
    AVDictionary* mux_opts = nullptr;
    // No +faststart — we want clean close() timing for benchmarks.
    // faststart can be added in a follow-up when the user needs streaming MP4.
    if (avformat_write_header(fmt_, &mux_opts) < 0) {
        spdlog::error("[native_av] avformat_write_header failed");
        return false;
    }

    // 8. Allocate frame + packet
    frame_  = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        spdlog::error("[native_av] av_frame_alloc or av_packet_alloc failed");
        return false;
    }

    frame_->format = codec_->pix_fmt;
    frame_->width  = codec_->width;
    frame_->height = codec_->height;

    if (av_frame_get_buffer(frame_, 32) < 0) {
        spdlog::error("[native_av] av_frame_get_buffer failed");
        return false;
    }

    spdlog::info("[native_av] Opened native encoder: {}x{} @ {}fps, codec={}, preset={}, crf={}, output='{}'",
                 options_.width, options_.height, options_.fps,
                 resolve_encoder_name(options_), options_.preset, options_.crf, filename);
    return true;
}

// ---------------------------------------------------------------------------
// close — drain encoder, write trailer, clean up
// ---------------------------------------------------------------------------

bool NativeAvEncoder::close() {
    if (!codec_) {
        // Already closed or never opened — not an error.
        return true;
    }

    const auto t_trailer0 = Clock::now();

    // 1. Drain encoder: send NULL to flush internal buffers
    avcodec_send_frame(codec_, nullptr);

    // 2. Receive and write remaining packets (timing tracked inside drain_packets)
    drain_packets();

    // 3. Write trailer (finalizes the MP4 file, writes moov atom, etc.)
    if (fmt_) {
        av_write_trailer(fmt_);
    }

    native_trailer_ms_ += elapsed_ms(t_trailer0);

    // 5. Close the IO
    if (fmt_ && !(fmt_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_->pb);
    }

    // 5. Free resources
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&codec_);
    avformat_free_context(fmt_);

    fmt_    = nullptr;
    codec_  = nullptr;
    stream_ = nullptr;
    frame_  = nullptr;
    packet_ = nullptr;

    spdlog::info("[native_av] Closed native encoder — {} frames written, YUV cache: {} hits / {} misses",
                 frames_written_, cache_hits_, cache_misses_);
    return true;
}

NativeAvEncoder::~NativeAvEncoder() {
    if (codec_) {
        close();
    }
}

// ---------------------------------------------------------------------------
// drain_packets — common helper used during write and close
// ---------------------------------------------------------------------------

bool NativeAvEncoder::drain_packets() {
    if (!codec_ || !fmt_ || !stream_ || !packet_) {
        return false;
    }

    for (;;) {
        const auto t_recv0 = Clock::now();
        int ret = avcodec_receive_packet(codec_, packet_);
        const double recv_ms = elapsed_ms(t_recv0);
        native_receive_packet_ms_ += recv_ms;

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more packets available right now (or stream fully drained)
            return true;
        }
        if (ret < 0) {
            spdlog::error("[native_av] avcodec_receive_packet error: {}", ret);
            return false;
        }

        // Rescale timestamps to stream time base and mux
        av_packet_rescale_ts(packet_, codec_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;

        const auto t_mux0 = Clock::now();
        ret = av_interleaved_write_frame(fmt_, packet_);
        const double mux_ms = elapsed_ms(t_mux0);
        native_mux_write_ms_ += mux_ms;

        av_packet_unref(packet_);

        if (ret < 0) {
            spdlog::error("[native_av] av_interleaved_write_frame error: {}", ret);
            return false;
        }

        if (profiling::g_current_counters) {
            profiling::g_current_counters->native_av_receive_packet_ms.fetch_add(
                static_cast<uint64_t>(recv_ms), std::memory_order_relaxed);
            profiling::g_current_counters->native_av_mux_write_ms.fetch_add(
                static_cast<uint64_t>(mux_ms), std::memory_order_relaxed);
        }
    }
}

} // namespace chronon3d::cli
