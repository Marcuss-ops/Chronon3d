#pragma once

#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace chronon3d::media {

enum class AudioExecutionPath : std::uint8_t {
    CopyPackets,
    TrimPackets,
    BoundaryReencode,
    FullReencode,
};

constexpr AudioExecutionPath resolve_audio_execution(
    bool content_changed, bool boundary_trim, bool codec_compatible) noexcept {
    if (!content_changed && !boundary_trim && codec_compatible) {
        return AudioExecutionPath::CopyPackets;
    }
    if (!content_changed && boundary_trim && codec_compatible) {
        return AudioExecutionPath::TrimPackets;
    }
    if (content_changed && boundary_trim && codec_compatible) {
        return AudioExecutionPath::BoundaryReencode;
    }
    return AudioExecutionPath::FullReencode;
}

/// Canonical mux boundary shared by encoded and copied packets.
struct EncodedPacket {
    std::shared_ptr<AVPacket> packet;
    AVRational time_base{1, 1};
    bool keyframe{false};
};

class MuxSession final {
public:
    MuxSession() = default;
    ~MuxSession();
    MuxSession(const MuxSession&) = delete;
    MuxSession& operator=(const MuxSession&) = delete;

    [[nodiscard]] bool open(const std::string& output_path,
                            const AVCodecContext& codec,
                            std::string& reason);
    [[nodiscard]] bool submit(EncodedPacket packet) noexcept;
    [[nodiscard]] bool finalize() noexcept;
    [[nodiscard]] double open_header_ms() const noexcept { return open_header_ms_; }
    [[nodiscard]] double packet_write_ms() const noexcept { return packet_write_ms_; }
    [[nodiscard]] double trailer_ms() const noexcept { return trailer_ms_; }

private:
    AVFormatContext* format_{nullptr};
    AVStream* video_stream_{nullptr};
    double open_header_ms_{0.0};
    double packet_write_ms_{0.0};
    double trailer_ms_{0.0};
};

/// Compatibility packet mux boundary for callers that still own a format
/// context. New encoded paths should use MuxSession.
class PacketAssembler final {
public:
    PacketAssembler(AVFormatContext* format, AVStream* video_stream,
                    AVStream* audio_stream = nullptr) noexcept
        : format_(format), video_stream_(video_stream), audio_stream_(audio_stream) {}

    PacketAssembler(const PacketAssembler&) = delete;
    PacketAssembler& operator=(const PacketAssembler&) = delete;

    [[nodiscard]] bool submit_video(AVPacket& packet, AVRational source_time_base) const noexcept;
    [[nodiscard]] bool submit_copied_video(
        AVPacket& packet, AVRational source_time_base) const noexcept;
    [[nodiscard]] bool submit_audio(AVPacket& packet, AVRational source_time_base) const noexcept;
    [[nodiscard]] bool finalize() const noexcept;

private:
    [[nodiscard]] bool submit(AVPacket& packet, AVRational source_time_base,
                              AVStream* target, bool default_duration) const noexcept;
    AVFormatContext* format_{nullptr};
    AVStream* video_stream_{nullptr};
    AVStream* audio_stream_{nullptr};
};

} // namespace chronon3d::media
