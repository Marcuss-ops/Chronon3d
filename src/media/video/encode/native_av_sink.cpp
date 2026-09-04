#include "native_av_sink.hpp"

#include <chronon3d/media/video/packet_assembler.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace chronon3d::media::video {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsed_ms(Clock::time_point start) noexcept {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

[[nodiscard]] std::string ffmpeg_error(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_make_error_string(buffer, sizeof(buffer), code);
    return buffer;
}

[[nodiscard]] AVPixelFormat to_av_pixel_format(PixelFormat format) noexcept {
    switch (format) {
    case PixelFormat::RGBA8:   return AV_PIX_FMT_RGBA;
    case PixelFormat::RGB24:   return AV_PIX_FMT_RGB24;
    case PixelFormat::YUV420P: return AV_PIX_FMT_YUV420P;
    case PixelFormat::NV12:    return AV_PIX_FMT_NV12;
    default:                   return AV_PIX_FMT_NONE;
    }
}

[[nodiscard]] AVCodecID codec_id(VideoCodec codec) noexcept {
    switch (codec) {
    case VideoCodec::H264:
    case VideoCodec::H264Nvenc: return AV_CODEC_ID_H264;
    case VideoCodec::H265:      return AV_CODEC_ID_HEVC;
    case VideoCodec::VP9:       return AV_CODEC_ID_VP9;
    case VideoCodec::AV1:       return AV_CODEC_ID_AV1;
    default:                    return AV_CODEC_ID_NONE;
    }
}

[[nodiscard]] const char* preferred_encoder_name(VideoCodec codec) noexcept {
    switch (codec) {
    case VideoCodec::H264:      return "libx264";
    case VideoCodec::H264Nvenc: return "h264_nvenc";
    case VideoCodec::H265:      return "libx265";
    case VideoCodec::VP9:       return "libvpx-vp9";
    case VideoCodec::AV1:       return "libaom-av1";
    default:                    return nullptr;
    }
}

[[nodiscard]] bool codec_supports_pixel_format(
    const AVCodec* codec, AVPixelFormat format) noexcept {
    if (!codec || format == AV_PIX_FMT_NONE) return false;
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const enum AVPixelFormat* formats = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT,
                                     0, reinterpret_cast<const void**>(&formats),
                                     nullptr) >= 0 && formats) {
        for (const auto* it = formats; *it != AV_PIX_FMT_NONE; ++it) {
            if (*it == format) return true;
        }
        return false;
    }
    // libavcodec 61+ owns supported-config discovery. If a codec cannot
    // expose that list, leave final validation to avcodec_open2 rather than
    // reaching back into AVCodec::pix_fmts from the legacy API.
    return true;
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (!codec->pix_fmts) return true;
    for (const auto* it = codec->pix_fmts; *it != AV_PIX_FMT_NONE; ++it) {
        if (*it == format) {
#pragma GCC diagnostic pop
            return true;
        }
    }
#pragma GCC diagnostic pop
    return false;
#endif
}

[[nodiscard]] AVPixelFormat choose_encoder_pixel_format(
    const AVCodec* codec, PixelFormat requested) noexcept {
    const AVPixelFormat preferred = to_av_pixel_format(requested);
    if (codec_supports_pixel_format(codec, preferred)) return preferred;
    if (codec_supports_pixel_format(codec, AV_PIX_FMT_YUV420P)) {
        return AV_PIX_FMT_YUV420P;
    }
    if (codec_supports_pixel_format(codec, AV_PIX_FMT_NV12)) {
        return AV_PIX_FMT_NV12;
    }
#if LIBAVCODEC_VERSION_MAJOR < 61
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (codec && codec->pix_fmts && codec->pix_fmts[0] != AV_PIX_FMT_NONE) {
        const AVPixelFormat fallback = codec->pix_fmts[0];
#pragma GCC diagnostic pop
        return fallback;
    }
#pragma GCC diagnostic pop
#endif
    return preferred;
}

[[nodiscard]] int tight_stride(PixelFormat format, int width) noexcept {
    switch (format) {
    case PixelFormat::RGBA8: return width * 4;
    case PixelFormat::RGB24: return width * 3;
    case PixelFormat::YUV420P:
    case PixelFormat::NV12: return width;
    default: return 0;
    }
}

} // namespace

NativeAvSink::~NativeAvSink() noexcept {
    (void)close();
}

bool NativeAvSink::fail(VideoSinkError error, std::string message) noexcept {
    error_ = error;
    error_message_ = std::move(message);
    state_ = VideoSinkState::Failed;
    return false;
}

void NativeAvSink::reset_stats() noexcept {
    stats_ = {};
}

VideoSink::Diagnostics NativeAvSink::diagnostics() const noexcept {
    return Diagnostics{.child_pid = -1, .blocked_write_ms = 0.0, .backend = "native-av"};
}

bool NativeAvSink::open(const VideoSinkConfig& config) {
    if (state_ == VideoSinkState::Open || state_ == VideoSinkState::Flushing) {
        return fail(VideoSinkError::InvalidConfig, "native-av sink is already open");
    }
    release_ffmpeg();
    state_ = VideoSinkState::Created;
    error_ = VideoSinkError::None;
    error_message_.clear();
    reset_stats();
    encoder_flushed_ = false;

    const ValidationResult validation = validate_video_sink_config(config);
    if (!validation) {
        return fail(VideoSinkError::InvalidConfig, validation.error_message);
    }
    if (!config.output.overwrite && std::filesystem::exists(config.output.output_path)) {
        return fail(VideoSinkError::FileExists,
                    "output exists and overwrite is disabled: " +
                    config.output.output_path.string());
    }

    config_ = config;
    const VideoCodec resolved_codec =
        resolve_auto_codec(config.encoder.codec, config.output.container);
    if (resolved_codec == VideoCodec::Uncompressed) {
        return fail(VideoSinkError::InvalidConfig,
                    "uncompressed output belongs to RawVideoSink");
    }

    const char* preferred_name = preferred_encoder_name(resolved_codec);
    const AVCodec* encoder = preferred_name
        ? avcodec_find_encoder_by_name(preferred_name)
        : nullptr;
    if (!encoder && resolved_codec != VideoCodec::H264Nvenc) {
        encoder = avcodec_find_encoder(codec_id(resolved_codec));
    }
    if (!encoder) {
        return fail(VideoSinkError::EncoderFailed,
                    std::string("libavcodec encoder unavailable: ") +
                    (preferred_name ? preferred_name : "unknown"));
    }

    codec_ = avcodec_alloc_context3(encoder);
    if (!codec_) {
        return fail(VideoSinkError::EncoderFailed,
                    "avcodec_alloc_context3 failed");
    }

    codec_->width = config.stream.width;
    codec_->height = config.stream.height;
    codec_->time_base = AVRational{
        config.stream.frame_rate_den, config.stream.frame_rate_num};
    codec_->framerate = AVRational{
        config.stream.frame_rate_num, config.stream.frame_rate_den};
    codec_->gop_size = std::max(1, config.stream.frame_rate_num /
                                  std::max(1, config.stream.frame_rate_den));
    codec_->max_b_frames = 0;
    codec_->pix_fmt = choose_encoder_pixel_format(
        encoder, config.encoder.encoded_pixel_format);
    if (codec_->pix_fmt == AV_PIX_FMT_NONE) {
        return fail(VideoSinkError::InvalidConfig,
                    "no supported encoder pixel format for requested output");
    }

    if (config.encoder.rate_control_mode == RateControlMode::Bitrate) {
        codec_->bit_rate = config.encoder.bitrate;
    }

    AVDictionary* codec_options = nullptr;
    if (!config.encoder.preset.empty()) {
        av_dict_set(&codec_options, "preset", config.encoder.preset.c_str(), 0);
    }
    if (config.encoder.tune && !config.encoder.tune->empty()) {
        av_dict_set(&codec_options, "tune", config.encoder.tune->c_str(), 0);
    }
    if (config.encoder.rate_control_mode == RateControlMode::Crf &&
        config.encoder.crf >= 0) {
        const std::string value = std::to_string(config.encoder.crf);
        av_dict_set(&codec_options,
                    resolved_codec == VideoCodec::H264Nvenc ? "cq" : "crf",
                    value.c_str(), 0);
    } else if (config.encoder.rate_control_mode == RateControlMode::ConstantQp &&
               config.encoder.qp >= 0) {
        const std::string value = std::to_string(config.encoder.qp);
        av_dict_set(&codec_options, "qp", value.c_str(), 0);
    }

    const int open_result = avcodec_open2(codec_, encoder, &codec_options);
    if (open_result < 0) {
        av_dict_free(&codec_options);
        return fail(VideoSinkError::EncoderFailed,
                    "avcodec_open2 failed: " + ffmpeg_error(open_result));
    }
    if (codec_options != nullptr) {
        const AVDictionaryEntry* unknown = av_dict_get(codec_options, "", nullptr,
                                                       AV_DICT_IGNORE_SUFFIX);
        const std::string name = unknown ? unknown->key : "unknown";
        av_dict_free(&codec_options);
        return fail(VideoSinkError::InvalidConfig,
                    "unsupported encoder option for selected codec: " + name);
    }
    av_dict_free(&codec_options);

    mux_ = std::make_unique<chronon3d::media::MuxSession>();
    std::string mux_reason;
    if (!mux_->open(chronon3d::media::MuxOpenConfig{
            .output_path = config.output.output_path.string(),
            .video_codec = codec_}, mux_reason)) {
        return fail(VideoSinkError::WriteFailed,
                    "libavformat mux open failed: " + mux_reason);
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        return fail(VideoSinkError::EncoderFailed,
                    "av_frame_alloc or av_packet_alloc failed");
    }
    frame_->format = codec_->pix_fmt;
    frame_->width = codec_->width;
    frame_->height = codec_->height;
    const int buffer_result = av_frame_get_buffer(frame_, 32);
    if (buffer_result < 0) {
        return fail(VideoSinkError::EncoderFailed,
                    "av_frame_get_buffer failed: " + ffmpeg_error(buffer_result));
    }

    state_ = VideoSinkState::Open;
    return true;
}

bool NativeAvSink::submit(const VideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || encoder_flushed_) {
        return fail(VideoSinkError::NotOpen, "submit requires an open, unflushed sink");
    }
    if (!frame.data || frame.width != config_.stream.width ||
        frame.height != config_.stream.height ||
        frame.pixel_format != config_.stream.submitted_format) {
        return fail(VideoSinkError::InvalidFrame,
                    "packed frame does not match the configured stream contract");
    }
    if (!validate_packed_stride(frame.pixel_format, frame.width, frame.stride_bytes)) {
        return fail(VideoSinkError::InvalidStride, "invalid packed frame stride");
    }

    if (frame.pixel_format == PixelFormat::YUV420P) {
        const auto* base = static_cast<const std::uint8_t*>(frame.data);
        const int y_stride = frame.width;
        const int uv_stride = frame.width / 2;
        const std::size_t y_bytes = static_cast<std::size_t>(frame.width) * frame.height;
        const std::size_t u_bytes = static_cast<std::size_t>(frame.width / 2) *
                                    static_cast<std::size_t>(frame.height / 2);
        const std::uint8_t* data[4]{base, base + y_bytes, base + y_bytes + u_bytes, nullptr};
        const int linesize[4]{y_stride, uv_stride, uv_stride, 0};
        return submit_planes(data, linesize, AV_PIX_FMT_YUV420P,
                             frame.width, frame.height, frame.pts);
    }
    if (frame.pixel_format == PixelFormat::NV12) {
        const auto* base = static_cast<const std::uint8_t*>(frame.data);
        const std::size_t y_bytes = static_cast<std::size_t>(frame.width) * frame.height;
        const std::uint8_t* data[4]{base, base + y_bytes, nullptr, nullptr};
        const int linesize[4]{frame.width, frame.width, 0, 0};
        return submit_planes(data, linesize, AV_PIX_FMT_NV12,
                             frame.width, frame.height, frame.pts);
    }

    const auto* base = static_cast<const std::uint8_t*>(frame.data);
    const int stride = frame.stride_bytes > 0
        ? static_cast<int>(frame.stride_bytes)
        : tight_stride(frame.pixel_format, frame.width);
    const std::uint8_t* data[4]{base, nullptr, nullptr, nullptr};
    const int linesize[4]{stride, 0, 0, 0};
    return submit_planes(data, linesize,
                         static_cast<int>(to_av_pixel_format(frame.pixel_format)),
                         frame.width, frame.height, frame.pts);
}

bool NativeAvSink::submit_planar(const PlanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || encoder_flushed_) {
        return fail(VideoSinkError::NotOpen, "submit_planar requires an open, unflushed sink");
    }
    if (!frame.y_data || !frame.u_data || !frame.v_data ||
        frame.width != config_.stream.width || frame.height != config_.stream.height) {
        return fail(VideoSinkError::InvalidFrame,
                    "planar frame does not match the configured stream contract");
    }
    if (!validate_planar_frame(frame.width, frame.height,
                               frame.y_stride, frame.u_stride, frame.v_stride)) {
        return fail(VideoSinkError::InvalidStride, "invalid YUV420P plane stride");
    }
    const std::uint8_t* data[4]{
        static_cast<const std::uint8_t*>(frame.y_data),
        static_cast<const std::uint8_t*>(frame.u_data),
        static_cast<const std::uint8_t*>(frame.v_data), nullptr};
    const int linesize[4]{
        static_cast<int>(frame.y_stride ? frame.y_stride : frame.width),
        static_cast<int>(frame.u_stride ? frame.u_stride : frame.width / 2),
        static_cast<int>(frame.v_stride ? frame.v_stride : frame.width / 2), 0};
    return submit_planes(data, linesize, AV_PIX_FMT_YUV420P,
                         frame.width, frame.height, frame.pts);
}

bool NativeAvSink::submit_biplanar(const BiplanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || encoder_flushed_) {
        return fail(VideoSinkError::NotOpen, "submit_biplanar requires an open, unflushed sink");
    }
    if (!frame.y_data || !frame.uv_data ||
        frame.width != config_.stream.width || frame.height != config_.stream.height) {
        return fail(VideoSinkError::InvalidFrame,
                    "biplanar frame does not match the configured stream contract");
    }
    if (!validate_biplanar_frame(frame.width, frame.height,
                                 frame.y_stride, frame.uv_stride)) {
        return fail(VideoSinkError::InvalidStride, "invalid NV12 plane stride");
    }
    const std::uint8_t* data[4]{
        static_cast<const std::uint8_t*>(frame.y_data),
        static_cast<const std::uint8_t*>(frame.uv_data), nullptr, nullptr};
    const int linesize[4]{
        static_cast<int>(frame.y_stride ? frame.y_stride : frame.width),
        static_cast<int>(frame.uv_stride ? frame.uv_stride : frame.width), 0, 0};
    return submit_planes(data, linesize, AV_PIX_FMT_NV12,
                         frame.width, frame.height, frame.pts);
}

bool NativeAvSink::submit_planes(const std::uint8_t* const data[4],
                                 const int linesize[4], int source_av_format,
                                 int width, int height, std::int64_t pts_hint) {
    const auto submit_start = Clock::now();
    const int writable_result = av_frame_make_writable(frame_);
    if (writable_result < 0) {
        return fail(VideoSinkError::EncoderFailed,
                    "av_frame_make_writable failed: " + ffmpeg_error(writable_result));
    }

    sws_ = sws_getCachedContext(
        sws_, width, height, static_cast<AVPixelFormat>(source_av_format),
        codec_->width, codec_->height, codec_->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) {
        return fail(VideoSinkError::EncoderFailed,
                    "sws_getCachedContext failed");
    }
    const int rows = sws_scale(sws_, data, linesize, 0, height,
                               frame_->data, frame_->linesize);
    if (rows != height) {
        return fail(VideoSinkError::EncoderFailed,
                    "sws_scale produced an incomplete frame");
    }

    // Preserve the caller's media timeline. NativeAvSink must not silently
    // replace source PTS with submission order.
    frame_->pts = pts_hint;

    int send_result = avcodec_send_frame(codec_, frame_);
    if (send_result == AVERROR(EAGAIN)) {
        // libavcodec is applying backpressure: receive all currently
        // available packets, then retry the exact same frame once. The
        // send/receive contract guarantees that both sides cannot remain at
        // EAGAIN without progress.
        if (!drain_packets(false)) return false;
        send_result = avcodec_send_frame(codec_, frame_);
        if (send_result == AVERROR(EAGAIN)) {
            return fail(VideoSinkError::EncoderFailed,
                        "libavcodec made no progress after send-frame EAGAIN");
        }
    }
    if (send_result < 0) {
        return fail(VideoSinkError::EncoderFailed,
                    "avcodec_send_frame failed: " + ffmpeg_error(send_result));
    }
    if (!drain_packets(false)) return false;

    ++stats_.frames_submitted;
    ++stats_.submit_count;
    stats_.encoder_staging_copy_bytes += frame_buffer_size(
        config_.encoder.encoded_pixel_format, width, height);
    stats_.total_submit_ms += elapsed_ms(submit_start);
    return true;
}

bool NativeAvSink::drain_packets(bool flushing) {
    for (;;) {
        const int receive_result = avcodec_receive_packet(codec_, packet_);
        if (receive_result == AVERROR(EAGAIN)) {
            if (!flushing) return true;
            return fail(VideoSinkError::EncoderFailed,
                        "libavcodec requested more input after flush was accepted");
        }
        if (receive_result == AVERROR_EOF) return true;
        if (receive_result < 0) {
            return fail(VideoSinkError::EncoderFailed,
                        "avcodec_receive_packet failed: " + ffmpeg_error(receive_result));
        }

        AVPacket* cloned = av_packet_clone(packet_);
        av_packet_unref(packet_);
        if (!cloned) {
            return fail(VideoSinkError::EncoderFailed,
                        "av_packet_clone failed");
        }
        std::shared_ptr<AVPacket> owned(
            cloned, [](AVPacket* packet) { av_packet_free(&packet); });
        const bool keyframe = (owned->flags & AV_PKT_FLAG_KEY) != 0;
        if (!mux_ || !mux_->submit_video(chronon3d::media::EncodedPacket{
                .packet = std::move(owned),
                .time_base = codec_->time_base,
                .keyframe = keyframe})) {
            return fail(VideoSinkError::WriteFailed,
                        "libavformat failed to mux an encoded packet");
        }
    }
}

bool NativeAvSink::flush() {
    if (state_ != VideoSinkState::Open) {
        if (state_ == VideoSinkState::Failed) return false;
        return fail(VideoSinkError::NotOpen, "flush requires an open sink");
    }
    if (encoder_flushed_) return true;

    const auto flush_start = Clock::now();
    state_ = VideoSinkState::Flushing;

    int send_result = avcodec_send_frame(codec_, nullptr);
    if (send_result == AVERROR(EAGAIN)) {
        // A delayed encoder can still have output queued when drain mode is
        // requested. Consume it first and retry the null frame; do not turn
        // normal encoder backpressure into a sink failure.
        if (!drain_packets(false)) return false;
        send_result = avcodec_send_frame(codec_, nullptr);
        if (send_result == AVERROR(EAGAIN)) {
            return fail(VideoSinkError::EncoderFailed,
                        "libavcodec made no progress while entering flush mode");
        }
    }
    if (send_result < 0 && send_result != AVERROR_EOF) {
        return fail(VideoSinkError::EncoderFailed,
                    "encoder flush submit failed: " + ffmpeg_error(send_result));
    }
    if (send_result != AVERROR_EOF && !drain_packets(true)) return false;

    encoder_flushed_ = true;
    stats_.total_flush_ms += elapsed_ms(flush_start);
    state_ = VideoSinkState::Open;
    return true;
}

bool NativeAvSink::close() noexcept {
    if (state_ == VideoSinkState::Closed) return true;
    if (state_ == VideoSinkState::Created) {
        release_ffmpeg();
        state_ = VideoSinkState::Closed;
        return true;
    }

    bool ok = state_ != VideoSinkState::Failed;
    if (ok && !encoder_flushed_) ok = flush();
    if (ok && mux_) {
        if (!mux_->finalize()) {
            error_ = VideoSinkError::WriteFailed;
            error_message_ = "libavformat trailer/finalization failed";
            ok = false;
        }
    }

    const auto output_path = config_.output.output_path;
    release_ffmpeg();
    if (ok) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(output_path, ec);
        if (!ec) stats_.bytes_written = static_cast<std::uint64_t>(size);
        state_ = VideoSinkState::Closed;
        return true;
    }
    state_ = VideoSinkState::Failed;
    return false;
}

void NativeAvSink::release_ffmpeg() noexcept {
    mux_.reset();
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (packet_) av_packet_free(&packet_);
    if (frame_) av_frame_free(&frame_);
    if (codec_) avcodec_free_context(&codec_);
}

} // namespace chronon3d::media::video