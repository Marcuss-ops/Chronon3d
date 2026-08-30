#pragma once

#include <chronon3d/media/video/packet_assembler.hpp>

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace chronon3d::cli {

using PacketAssembler = chronon3d::media::PacketAssembler;

enum class AudioExecutionPath : std::uint8_t {
    CopyPackets,
    TrimPackets,
    BoundaryReencode,
    FullReencode,
};

/// Selects the compressed-audio path without decoding unchanged samples.
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

/// Canonical packet-level mux boundary shared by native encoding and
/// BitstreamCopy. The assembler does not own the format context; its caller
/// owns container lifetime while all encoded and copied packets converge on
/// these operations.
/* PacketAssembler is the canonical media::PacketAssembler. */
#if 0
class PacketAssembler final {
public:
    PacketAssembler(AVFormatContext* format, AVStream* video_stream,
                    AVStream* audio_stream = nullptr) noexcept
        : format_(format), video_stream_(video_stream), audio_stream_(audio_stream) {}

    PacketAssembler(const PacketAssembler&) = delete;
    PacketAssembler& operator=(const PacketAssembler&) = delete;

    [[nodiscard]] bool submit_video(AVPacket& packet, AVRational source_time_base) const noexcept;
    /// Submit an already encoded packet (e.g. an untouched safe GOP).
    /// Kept distinct from submit_video so the GOP planner can account for
    /// copied versus reencoded work without changing the mux boundary.
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
#endif

} // namespace chronon3d::cli
