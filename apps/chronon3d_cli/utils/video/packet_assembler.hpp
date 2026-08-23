#pragma once

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace chronon3d::cli {

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

/// Packet-level video mux boundary used by native encoders.
///
/// The assembler deliberately does not own the format context: the encoder
/// owns codec/container lifetime, while this class owns the packet contract.
/// That makes copied compressed GOP packets and encoded packets converge on
/// the same mux operation in a later phase without adding another exporter.
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

} // namespace chronon3d::cli
