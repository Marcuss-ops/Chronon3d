#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace chronon3d::media {

struct MuxAvioHashWriter;

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

/// Describes an audio stream to add to a MuxSession. The caller supplies
/// codec parameters (typically copied from the source or the TTS output)
/// and the source time_base so rescale_ts works correctly.
struct AudioStreamConfig {
    AVCodecParameters* params{nullptr};  ///< borrowed, not owned
    AVRational time_base{1, 1};
};

/// Complete mux opening contract. All streams are declared before the
/// container header is written; audio is optional but cannot be added later.
struct MuxOpenConfig {
    std::string output_path;
    const AVCodecContext* video_codec{nullptr};  ///< borrowed, not owned
    std::optional<AudioStreamConfig> audio{};
};

struct SegmentAssemblyRequest {
    std::vector<std::string> input_paths;
    std::string output_path;
};

struct SegmentAssemblyResult {
    bool success{false};
    std::string reason;
    /// Lowercase SHA-256 digest of the exact final muxed file bytes.
    std::string output_checksum;
    /// True only when seek/backpatch forced a final-file checksum reread.
    bool checksum_used_reread{false};
};

/// Assemble compatible encoded segments through Chronon's canonical mux path.
SegmentAssemblyResult assemble_segments(const SegmentAssemblyRequest& request);

class MuxSession final {
public:
    MuxSession() = default;
    ~MuxSession();
    MuxSession(const MuxSession&) = delete;
    MuxSession& operator=(const MuxSession&) = delete;

    /// Open the mux session with a video stream derived from the encoder
    /// context. After this call the session is ready to accept video
    /// packets. Optional audio is declared in the same config before the
    /// container header is written.
    [[nodiscard]] bool open(const MuxOpenConfig& config,
                            std::string& reason);

    /// All stream declarations, including optional audio, are registered
    /// before the header is written.

    /// Write the mux header through the checksum-aware AVIO boundary. Called
    /// automatically by open(); retained for internal/test compatibility.
    [[nodiscard]] bool write_header(const std::string& output_path,
                                     std::string& reason);

    /// Submit a video packet. This is the canonical sink for all encoded
    /// video: NVENC output, copied bitstream packets, and any future
    /// encoder path. The MuxSession owns the timestamp authority — callers
    /// never rescale PTS themselves.
    [[nodiscard]] bool submit_video(EncodedPacket packet) noexcept;

    /// Submit an audio packet. Same contract as submit_video: the MuxSession
    /// rescales the packet from the declared time_base into the audio
    /// stream's time_base and interleaves it with video via
    /// av_interleaved_write_frame.
    [[nodiscard]] bool submit_audio(EncodedPacket packet) noexcept;

    /// Legacy alias for submit_video. Retained for callers that were
    /// written before the A/V split; new code should call submit_video.
    [[nodiscard]] bool submit(EncodedPacket packet) noexcept {
        return submit_video(std::move(packet));
    }

    [[nodiscard]] bool finalize() noexcept;
    [[nodiscard]] double open_header_ms() const noexcept { return open_header_ms_; }
    [[nodiscard]] double packet_write_ms() const noexcept { return packet_write_ms_; }
    [[nodiscard]] double audio_write_ms() const noexcept { return audio_write_ms_; }
    [[nodiscard]] double trailer_ms() const noexcept { return trailer_ms_; }
    [[nodiscard]] bool has_audio() const noexcept { return audio_stream_ != nullptr; }
    [[nodiscard]] std::string_view output_checksum() const noexcept { return output_checksum_; }
    [[nodiscard]] bool checksum_used_reread() const noexcept { return checksum_used_reread_; }

private:
    AVFormatContext* format_{nullptr};
    AVStream* video_stream_{nullptr};
    AVStream* audio_stream_{nullptr};
    std::unique_ptr<MuxAvioHashWriter, void(*)(MuxAvioHashWriter*)> writer_{
        nullptr, [](MuxAvioHashWriter* p) { delete_writer(p); }};
    static void delete_writer(MuxAvioHashWriter* writer) noexcept;
    std::string output_path_;
    std::string output_checksum_;
    bool checksum_used_reread_{false};
    bool finalize_attempted_{false};
    bool finalized_{false};
    double open_header_ms_{0.0};
    double packet_write_ms_{0.0};
    double audio_write_ms_{0.0};
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
