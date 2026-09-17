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
class AudioResampler;

/// FFmpeg AVPacket ownership.  Encoded packets handed to MuxSession are
/// consumed synchronously inside submit_video/submit_audio (the mux never
/// retains the pointer past the call), so a move-only owner matches the
/// real protocol: no shared_ptr control block, no per-packet ref-count
/// traffic, and producers can av_packet_move_ref() straight out of their
/// receive buffer instead of clone+ref.
struct AVPacketDeleter {
    void operator()(AVPacket* packet) const noexcept {
        if (packet) av_packet_free(&packet);
    }
};
using OwnedAVPacket = std::unique_ptr<AVPacket, AVPacketDeleter>;

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

/// Canonical mux boundary shared by encoded and copied packets.  Move-only:
/// submit_video/submit_audio take the packet by value and never retain it,
/// so the producer transfers ownership exactly once (av_packet_move_ref or
/// av_packet_ref into a fresh OwnedAVPacket).
struct EncodedPacket {
    OwnedAVPacket packet;
    AVRational time_base{1, 1};
    bool keyframe{false};
};

/// P0-5E — where a BORROWED producer's packets belong in the container.
///
/// A container declared from the compressed source (open_source_declared_mux)
/// carries copied GOPs at their own source ticks and freshly encoded GOPs at
/// the tick the schedule reserved for the work unit that produced them. Which
/// of the two a packet is cannot be read off the packet: it is the producer's
/// placement that decides, and the MuxSession stays the one timestamp
/// authority that applies it.
///
enum class MuxVideoPlacement : std::uint8_t {
    /// The packet's own timestamps, rescaled into the stream time base. Copied
    /// source packets and ordinary single-owner exports.
    AsSubmitted,
    /// Rescaled and then shifted by the container's scheduled origin
    /// (set_video_origin): what a borrowed encoder's packets need so the work
    /// unit it just rendered lands where the schedule put it instead of
    /// restarting the container at zero.
    ScheduledOrigin,
};

/// Describes an audio stream to add to a MuxSession. The caller supplies
/// codec parameters (typically copied from the source or the TTS output)
/// and the source time_base so rescale_ts works correctly.
struct AudioStreamConfig {
    AVCodecParameters* params{nullptr};  ///< borrowed, not owned
    AVRational time_base{1, 1};
    /// Optional target sample rate for the OUTPUT audio stream.
    ///
    /// 0 (the default) keeps the historical zero-cost copy path: the source
    /// stream is muxed verbatim, so the artifact inherits the source's rate
    /// and time_base. A positive value that differs from the source rate makes
    /// MuxSession transcode the audio (decode -> libswresample -> AAC encode)
    /// so the published artifact satisfies the declared rate instead of
    /// inheriting it. The requested rate is part of the output contract, so a
    /// request that cannot be honoured FAILS the render rather than degrading
    /// silently to the source rate.
    int target_sample_rate{0};
    /// Optional target bitrate (bits/second) for the resampled OUTPUT audio.
    /// 0 selects the canonical assembly-ready audio profile rate
    /// (128 kbit/s) — the value the output contract's audio block declares.
    /// Ignored on the copy path, where the source bitrate is preserved.
    int target_bitrate{0};
};

/// Complete mux opening contract. All streams are declared before the
/// container header is written; audio is optional but cannot be added later.
struct MuxOpenConfig {
    std::string output_path;
    const AVCodecContext* video_codec{nullptr};  ///< borrowed, not owned
    std::optional<AudioStreamConfig> audio{};
    const AVCodecParameters* video_params{nullptr}; ///< borrowed, not owned
    AVRational video_time_base{0, 1};
    /// Request the SHA-256 of the final file bytes.
    ///
    /// DEFAULT OFF, because it is a cost the caller must be able to justify:
    /// a seekable container backpatches metadata (MP4 patches the `mdat` size
    /// near the START of the file after the payload is written), so the
    /// append-only hash is invalidated and the checksum can only be produced by
    /// re-reading the whole file — an extra O(file) pass per artifact.
    ///
    /// Who may justify it: a caller whose ONLY digest is this one
    /// (`assemble_segments`, whose result carries the concatenated artifact's
    /// checksum). Who must NOT: the exporter/container lane, whose published
    /// artifact digest is produced by the output-contract verification
    /// (`media::video::verify_output_contract` -> PipeExportResult::sha256), a
    /// mandatory pass over the same file. That lane used to pay for a digest
    /// nothing read and then pay twice more: measured on a real render,
    /// `full_file_reread_count=1` and `final_reread_bytes` equal to the whole
    /// artifact size. One owner per digest.
    bool checksum{false};
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

/// Exact output/hash accounting for one mux session. Seekable containers may
/// backpatch metadata, invalidating an append-only hash; these counters make
/// that cost explicit instead of treating every final reread as a bug.
struct MuxHashMetrics {
    std::uint64_t output_write_bytes{0};
    std::uint64_t output_seek_count{0};
    std::uint64_t output_overwrite_bytes{0};
    std::uint64_t incremental_hash_invalidations{0};
    std::uint64_t final_reread_bytes{0};
};

/// Assemble compatible encoded segments through Chronon's canonical mux path.
SegmentAssemblyResult assemble_segments(const SegmentAssemblyRequest& request);

class MuxSession final {
public:
    // Declared, not defaulted inline: MuxSession owns a unique_ptr to the
    // forward-declared AudioResampler, so both the constructor's exception
    // cleanup and the destructor are defined in the translation unit where
    // AudioResampler is complete (packet_assembler.cpp).
    MuxSession();
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

    /// Declare the container tick a BORROWED producer's output starts at. The
    /// owner of the session sets it before handing the session to the producer
    /// of one work unit; packets submitted with MuxVideoPlacement::
    /// ScheduledOrigin are shifted by exactly this many stream ticks.
    void set_video_origin(std::int64_t origin_ticks) noexcept { video_origin_ = origin_ticks; }
    [[nodiscard]] std::int64_t video_origin() const noexcept { return video_origin_; }

    /// Submit a video packet. This is the canonical sink for all encoded
    /// video: NVENC output, copied bitstream packets, and any future
    /// encoder path. The MuxSession owns the timestamp authority — callers
    /// never rescale PTS themselves.
    [[nodiscard]] bool submit_video(
        EncodedPacket packet,
        MuxVideoPlacement placement = MuxVideoPlacement::AsSubmitted) noexcept;
    /// Synchronous borrowed-packet path. The packet is consumed before this
    /// call returns, so the caller can reuse it without cloning it into a
    /// shared_ptr control block.
    [[nodiscard]] bool submit_video(
        AVPacket& packet, AVRational source_time_base,
        MuxVideoPlacement placement = MuxVideoPlacement::AsSubmitted) noexcept;

    /// Submit an audio packet. Same contract as submit_video: the MuxSession
    /// rescales the packet from the declared time_base into the audio
    /// stream's time_base and interleaves it with video via
    /// av_interleaved_write_frame.
    [[nodiscard]] bool submit_audio(EncodedPacket packet) noexcept;

    /// Legacy alias for submit_video. Retained for callers that were
    /// written before the A/V split; new code should call submit_video.
    [[nodiscard]] bool submit(
        EncodedPacket packet,
        MuxVideoPlacement placement = MuxVideoPlacement::AsSubmitted) noexcept {
        return submit_video(std::move(packet), placement);
    }

    [[nodiscard]] bool finalize() noexcept;
    [[nodiscard]] double open_header_ms() const noexcept { return open_header_ms_; }
    [[nodiscard]] double packet_write_ms() const noexcept { return packet_write_ms_; }
    [[nodiscard]] double audio_write_ms() const noexcept { return audio_write_ms_; }
    [[nodiscard]] double trailer_ms() const noexcept { return trailer_ms_; }
    [[nodiscard]] bool has_audio() const noexcept { return audio_stream_ != nullptr; }
    /// Empty unless MuxOpenConfig::checksum was requested: there is no digest
    /// to publish when the caller did not ask for one (the exporter lane's
    /// digest is owned by the output-contract verification).
    [[nodiscard]] std::string_view output_checksum() const noexcept { return output_checksum_; }
    /// True only when a requested checksum had to re-read the final file.
    [[nodiscard]] bool checksum_requested() const noexcept { return checksum_enabled_; }
    [[nodiscard]] bool checksum_used_reread() const noexcept { return checksum_used_reread_; }
    [[nodiscard]] MuxHashMetrics hash_metrics() const noexcept;

private:
    /// Writes one already-encoded audio packet to the mux. Shared by the copy
    /// path and the resampler output so both honour the same interleaving and
    /// timestamp rules.
    [[nodiscard]] bool write_audio_packet(AVPacket& packet,
                                          AVRational source_time_base) noexcept;

    /// Shifts a rescaled packet onto the schedule's origin (P0-5E).
    [[nodiscard]] bool place_video_packet(AVPacket& packet,
                                          MuxVideoPlacement placement) const noexcept;

    AVFormatContext* format_{nullptr};
    AVStream* video_stream_{nullptr};
    AVStream* audio_stream_{nullptr};
    /// Non-null only when the caller declared a target sample rate that
    /// differs from the source. Owns the decode/resample/encode chain and is
    /// drained before the container trailer is written.
    std::unique_ptr<AudioResampler> audio_resampler_;
    std::unique_ptr<MuxAvioHashWriter, void(*)(MuxAvioHashWriter*)> writer_{
        nullptr, [](MuxAvioHashWriter* p) { delete_writer(p); }};
    static void delete_writer(MuxAvioHashWriter* writer) noexcept;
    std::string output_path_;
    std::string output_checksum_;
    /// P0-5E — container tick a BORROWED producer's output starts at.
    std::int64_t video_origin_{0};
    bool checksum_enabled_{false};
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
