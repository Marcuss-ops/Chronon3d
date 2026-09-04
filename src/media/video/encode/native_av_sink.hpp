#pragma once

#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_sink.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace chronon3d::media {
class MuxSession;
}

namespace chronon3d::media::video {

/// Canonical in-process compressed-video sink.
///
/// Standard codec mechanics belong to libavcodec; container mechanics belong
/// to MuxSession/libavformat. Chronon owns only configuration mapping,
/// lifecycle, frame validation/conversion and error propagation.
class NativeAvSink final : public VideoSink {
public:
    NativeAvSink() = default;
    ~NativeAvSink() noexcept override;

    NativeAvSink(const NativeAvSink&) = delete;
    NativeAvSink& operator=(const NativeAvSink&) = delete;

    bool open(const VideoSinkConfig& config) override;
    bool submit(const VideoFrameView& frame) override;
    bool submit_planar(const PlanarVideoFrameView& frame) override;
    bool submit_biplanar(const BiplanarVideoFrameView& frame) override;
    bool flush() override;
    bool close() noexcept override;

    [[nodiscard]] VideoSinkState state() const noexcept override { return state_; }
    [[nodiscard]] std::string_view name() const noexcept override { return "native-av"; }
    [[nodiscard]] std::uint64_t frames_submitted() const noexcept override {
        return stats_.frames_submitted;
    }
    [[nodiscard]] Stats stats() const noexcept override { return stats_; }
    void reset_stats() noexcept override;
    [[nodiscard]] Diagnostics diagnostics() const noexcept override;
    [[nodiscard]] VideoSinkError last_error() const noexcept override { return error_; }
    [[nodiscard]] std::string last_error_message() const noexcept override {
        return error_message_;
    }

private:
    bool submit_planes(const std::uint8_t* const data[4], const int linesize[4],
                       int source_av_format, int width, int height,
                       std::int64_t pts_hint);
    bool drain_packets(bool flushing);
    bool fail(VideoSinkError error, std::string message) noexcept;
    void release_ffmpeg() noexcept;

    VideoSinkConfig config_{};
    VideoSinkState state_{VideoSinkState::Created};
    VideoSinkError error_{VideoSinkError::None};
    std::string error_message_;
    AVCodecContext* codec_{nullptr};
    AVFrame* frame_{nullptr};
    AVPacket* packet_{nullptr};
    SwsContext* sws_{nullptr};
    std::unique_ptr<media::MuxSession> mux_;
    Stats stats_{};
    bool encoder_flushed_{false};
};

} // namespace chronon3d::media::video
