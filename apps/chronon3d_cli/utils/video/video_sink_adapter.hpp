#pragma once

// ---------------------------------------------------------------------------
// video_sink_adapter.hpp — Adapter: IVideoEncoder → VideoSink.
//
// Bridges the CLI's IVideoEncoder interface (used by writer thread, telemetry,
// etc.) with the new media::video::VideoSink pipeline created via
// create_video_sink().
//
// Lifecycle:
//   VideoSinkEncoderAdapter(sink_type)
//   → open(FfmpegPipeOptions)
//     → build VideoSinkConfig from FfmpegPipeOptions
//     → create_video_sink(config) → sink->open(config)
//   → write_frame(Framebuffer)
//     → convert Framebuffer to tight-packed VideoFrameView
//     → sink->submit(view)
//   → close()
//     → sink->flush() + sink->close()
//
// Thread-safety: NOT thread-safe.  Accessed only from the writer thread.
// ---------------------------------------------------------------------------

#include "ffmpeg_pipe_encoder.hpp"
#include <commands/video/common/sink_options.hpp>

#include <chronon3d/media/video/video_sink_factory.hpp>
#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/media/frame_conversion/frame_conversion_service.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cli {

/// Adapter that wraps the new VideoSink API in the legacy IVideoEncoder
/// interface so the existing CLI pipeline (writer thread, telemetry,
/// encoder lifecycle) can use create_video_sink() unchanged.
///
/// Used for VideoSinkType::Ffmpeg and VideoSinkType::RawFile.
/// NullRender/NullConvert continue using their existing encoders.
class VideoSinkEncoderAdapter final : public IVideoEncoder {
public:
    explicit VideoSinkEncoderAdapter(VideoSinkType sink_type) noexcept
        : sink_type_(sink_type) {}

    ~VideoSinkEncoderAdapter() noexcept override;

    VideoSinkEncoderAdapter(const VideoSinkEncoderAdapter&) = delete;
    VideoSinkEncoderAdapter& operator=(const VideoSinkEncoderAdapter&) = delete;
    VideoSinkEncoderAdapter(VideoSinkEncoderAdapter&&) noexcept = default;
    VideoSinkEncoderAdapter& operator=(VideoSinkEncoderAdapter&&) noexcept = default;

    // ── IVideoEncoder interface ─────────────────────────────────────────

    bool open(const FfmpegPipeOptions& options) override;
    bool write_frame(const Framebuffer& fb) override;
    bool write_frame_async(const Framebuffer& fb,
                           std::shared_ptr<Framebuffer> owner) override;
    bool close() override;

    void set_counters(chronon3d::RenderCounters* counters) override {
        counters_ = counters;
    }

    [[nodiscard]] uint64_t frames_written() const override {
        return frames_written_;
    }

    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override {
        return last_telemetry_;
    }

    // ── Pipe telemetry (virtual overrides) ──────────────────────────────

    [[nodiscard]] double total_write_blocked_ms() const override {
        return write_blocked_ms_;
    }

    [[nodiscard]] int ffmpeg_pid() const override {
        return ffmpeg_pid_;
    }

private:
    // ── Config mapping helpers ─────────────────────────────────────────

    /// Convert FfmpegPipeOptions → VideoSinkConfig.
    bool build_sink_config(const FfmpegPipeOptions& opts,
                           chronon3d::media::video::VideoSinkConfig& config);

    /// Map codec string to VideoCodec enum.
    static chronon3d::media::video::VideoCodec map_codec(
        const std::string& codec);

    /// Map PipePixelFormat → media::video::PixelFormat.
    static chronon3d::media::video::PixelFormat map_pixel_format(
        PipePixelFormat fmt);

    /// Map output pix fmt string → media::video::PixelFormat.
    static chronon3d::media::video::PixelFormat map_output_pix_fmt(
        const std::string& fmt);

    /// Detect VideoContainer from output path extension.
    static chronon3d::media::video::VideoContainer detect_container(
        const std::string& path);

    // ── Frame conversion ───────────────────────────────────────────────

    /// Convert Framebuffer and submit to sink.
    bool convert_and_submit(const Framebuffer& fb);

    // ── State ──────────────────────────────────────────────────────────

    VideoSinkType sink_type_{VideoSinkType::Ffmpeg};
    std::unique_ptr<chronon3d::media::video::VideoSink> sink_;
    std::vector<uint8_t> staging_buffer_;
    chronon3d::video::FrameConversionService conv_svc_{8};

    uint64_t frames_written_{0};
    double   write_blocked_ms_{0.0};
    int      ffmpeg_pid_{-1};
    EncoderFrameTelemetry last_telemetry_{};
    chronon3d::RenderCounters* counters_{nullptr};

    FfmpegPipeOptions options_{};   // stored for color_transform, pipe_writer, etc.

    // Session contract (cached from open() for fast access)
    int             width_{0};
    int             height_{0};
    PipePixelFormat input_format_{PipePixelFormat::RGBA};
    std::string     codec_;
    std::string     output_pix_fmt_;
};

} // namespace chronon3d::cli
