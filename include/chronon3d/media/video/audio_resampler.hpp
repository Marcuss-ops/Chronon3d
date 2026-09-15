#pragma once

#include <chronon3d/media/video/packet_assembler.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct AVAudioFifo;
struct AVCodecContext;
struct AVCodecParameters;
struct SwrContext;

namespace chronon3d::media {

/// Transcodes an audio stream to a requested output sample rate.
///
/// Why it exists (clip.render audio contract gap, 2026-09-15): the native
/// A/V mux COPIES the source audio stream, so the published artifact inherits
/// the source's sample rate. A sealed plan that declares 48 kHz therefore
/// produced a 44.1 kHz artifact whenever the source clip was 44.1 kHz, and
/// the fail-closed contract gate rejected every such render with
/// "audio timebase 1/44100 != 1/48000" — the requested
/// {"mode":"transcode"} was inert because no Chronon stage could change a
/// sample rate. This class is that missing stage: decode -> libswresample ->
/// AAC encode, owned by the mux boundary that already owns the audio track.
///
/// Fail-closed contract: `open()` reports WHY the chain could not be built and
/// never degrades to the copy path. A caller that asked for 48 kHz must never
/// silently receive 44.1 kHz; the job fails instead.
class AudioResampler final {
public:
    AudioResampler() = default;
    ~AudioResampler();
    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    /// Build the decode -> resample -> encode chain for `source`, producing
    /// `target_sample_rate` Hz at `target_bitrate` bits/second. A
    /// `target_bitrate` <= 0 selects the canonical assembly-ready audio rate.
    /// Returns false and fills `reason` when the source cannot be decoded or
    /// re-encoded (unknown codec, missing encoder, resampler init failure).
    [[nodiscard]] bool open(const AVCodecParameters* source,
                            int target_sample_rate,
                            int target_bitrate,
                            std::string& reason);

    [[nodiscard]] bool active() const noexcept { return active_; }

    /// Encoder-derived codec parameters for the output stream (owned here).
    [[nodiscard]] AVCodecParameters* output_parameters() const noexcept {
        return output_params_;
    }

    /// Encoder time base, always 1 / target_sample_rate.
    [[nodiscard]] AVRational output_time_base() const noexcept { return time_base_; }

    /// Decode one source packet, resample it and encode the resulting frames.
    /// Produced packets are appended to `out` in write order. The input packet
    /// is not consumed: the caller keeps ownership of its buffer.
    [[nodiscard]] bool push(const AVPacket& input,
                            std::vector<OwnedAVPacket>& out) noexcept;

    /// Drain the decoder and the encoder after the final input packet. Every
    /// remaining packet is appended to `out`; callers must write them before
    /// the container trailer.
    [[nodiscard]] bool flush(std::vector<OwnedAVPacket>& out) noexcept;

private:
    [[nodiscard]] bool drain_decoder(std::vector<OwnedAVPacket>& out) noexcept;
    [[nodiscard]] bool resample_and_encode(const AVFrame* decoded,
                                           std::vector<OwnedAVPacket>& out) noexcept;
    /// Convert `count` samples through the resampler and append them to the
    /// staging fifo, then encode every complete encoder frame available.
    [[nodiscard]] bool resample_into_fifo(const AVFrame* decoded, int count,
                                          std::vector<OwnedAVPacket>& out) noexcept;
    /// Encode whole frames from the fifo. `final` also emits the trailing
    /// partial frame (the encoder pads it).
    [[nodiscard]] bool drain_fifo(bool final,
                                  std::vector<OwnedAVPacket>& out) noexcept;
    /// Encode one frame (or flush with nullptr) and append every emitted packet.
    [[nodiscard]] bool encode_frame(const AVFrame* frame,
                                    std::vector<OwnedAVPacket>& out) noexcept;
    void release() noexcept;

    AVCodecContext* decoder_{nullptr};
    AVCodecContext* encoder_{nullptr};
    SwrContext* swr_{nullptr};
    /// Staging buffer: the native AAC encoder only accepts frames of exactly
    /// encoder_->frame_size samples, while swr_convert emits arbitrary counts.
    AVAudioFifo* fifo_{nullptr};
    int frame_size_{1024};
    AVCodecParameters* output_params_{nullptr};
    AVRational time_base_{1, 1};
    int target_sample_rate_{0};
    std::uint64_t next_pts_{0};
    bool active_{false};
};

} // namespace chronon3d::media
