#pragma once

// =============================================================================
// encoder_bitstream_contract.hpp — the single ENCODER-OWNED bitstream contract
// authority for the GOP splice gate (P0-5A).
//
// The compressed-source preflight (inspect_gop_source) compares a SOURCE
// bitstream against a TARGET contract:
//
//   SOURCE bitstream (codec/profile/level/SPS-PPS/format)
//        vs
//   TARGET = the stream the output container will actually carry
//
// When a re-encode participates in that output (hybrid / boundary re-encode)
// the producer of the target is the ENCODER. Deriving the target from the
// source turned the gate into `source vs source`, which always reported
// "compatible" and would have certified a splice a decoder cannot follow.
// The compressed source is only ever the `source` side of the comparison —
// it is never the producer of the target.
//
// Producers, deliberately separated:
//
//   NativeAvEncoder::open()
//     └─ applies ResolvedEncoderConfig ─ avcodec_open2()
//        └─ encoder_bitstream_contract_from_codec()   ← MATERIALIZED
//             profile / level / pixel format / SPS-PPS are read back from the
//             encoder's OWN extradata through libavcodec's decoder, i.e. the
//             same facts a demuxer will report to the playback decoder.
//
//   make_encoder_bitstream_contract()                  ← DECLARED
//     codec, dimensions and colour are configuration facts known before
//     open; profile / level / pixel format / parameter sets are NOT, so they
//     stay unknown and the splice gate fails closed.
//
// The pure-remux lane has no encoder at all; its target is built by
// make_remux_target_contract() in gop_smart_copy.hpp, where "the output stream
// IS the source stream" is the definition rather than an accident.
// =============================================================================

#include "encoder_config_resolution.hpp"
#include "ffmpeg_pipe_encoder.hpp"
#include "gop_smart_copy.hpp"

#include <optional>
#include <string_view>

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// Codec id produced by a resolved FFmpeg encoder name
/// (libx264 / libx264rgb / libx265 / h264_nvenc / hevc_nvenc / av1_nvenc).
/// AV_CODEC_ID_NONE for anything outside that set: an unknown encoder name can
/// never certify a splice.
[[nodiscard]] BitstreamCodecId bitstream_codec_for_encoder_name(
    std::string_view encoder_name) noexcept;

/// Single color-boundary contract. NativeAvEncoder applies exactly these
/// values to the AVCodecContext / AVFrame, and the splice gate compares the
/// source against the same values — one authority, no drift.
struct EncoderColorContract {
    AVColorRange color_range;
    AVColorSpace color_space;
    AVColorPrimaries color_primaries;
    AVColorTransferCharacteristic color_trc;
};

[[nodiscard]] EncoderColorContract encoder_color_contract(
    color::ColorSpace output_space) noexcept;
#endif

/// DECLARED encoder-owned contract, built from exactly the
/// FfmpegPipeOptions + ResolvedEncoderConfig pair NativeAvEncoder::open()
/// applies. It never opens or reads the compressed source.
///
/// profile / level / pixel format / parameter sets are left unknown because
/// they are the encoder's *output* and cannot be derived from configuration
/// strings; the splice gate therefore fails closed until
/// encoder_bitstream_contract_from_codec() supplies the materialized truth.
[[nodiscard]] BitstreamTargetContract make_encoder_bitstream_contract(
    const FfmpegPipeOptions& options,
    const ResolvedEncoderConfig& resolved) noexcept;

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// MATERIALIZED encoder-owned contract.
///
/// Reads the codec facts that a demuxer will hand to the playback decoder from
/// the encoder's OWN bitstream: profile, level and pixel format are parsed by
/// libavcodec's decoder out of the encoder extradata (never guessed from
/// strings), and the parameter sets are the encoder's extradata bytes.
///
/// Call it after avcodec_open2() — that is when the encoder produces its
/// extradata. `encoder_materialized` is true only when every field the splice
/// gate needs is present; otherwise the contract stays fail-closed.
[[nodiscard]] BitstreamTargetContract encoder_bitstream_contract_from_codec(
    const AVCodecContext& codec,
    const FfmpegPipeOptions& options,
    const ResolvedEncoderConfig& resolved) noexcept;

/// MATERIALIZED encoder-owned contract built from the encoder's OWN output
/// packet.
///
/// libx264 and NVENC publish their parameter sets IN-BAND: the first packet
/// carries the SPS/PPS as AV_PKT_DATA_NEW_EXTRADATA and leaves the codec
/// context's extradata empty, so the codec-context probe above legitimately
/// never materializes for them. The packet is therefore where the stream a
/// demuxer will read back actually becomes visible, and it is the instrument
/// the borrowed-mux gate needs (P0-5E).
///
/// Returns std::nullopt while no packet has published the facts yet; callers
/// stay fail-closed in that case.
[[nodiscard]] std::optional<BitstreamTargetContract>
encoder_bitstream_contract_from_packet(const AVPacket& packet,
                                       const AVCodecContext& codec,
                                       const FfmpegPipeOptions& options,
                                       const ResolvedEncoderConfig& resolved) noexcept;
#endif

/// Resolve the encoder configuration this FfmpegPipeOptions pair would apply
/// and build the DECLARED contract from it. Returns std::nullopt when the
/// configuration cannot be resolved — no target, therefore no copy.
[[nodiscard]] std::optional<BitstreamTargetContract>
resolve_encoder_bitstream_contract(const FfmpegPipeOptions& options) noexcept;

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// P0-5E — did the encoder reproduce the stream the borrowed container was
/// declared from?
///
/// On the direct-mux splice lane the container's video stream is declared from
/// the compressed SOURCE (a copied packet is then correct by construction), so
/// the only thing left to prove is that the encoder's own output reproduces
/// that declaration: same codec, profile, level, dimensions, pixel format,
/// parameter sets and colour. `declared` is the source side of the
/// comparison, `encoder_contract` the materialized output of the encoder that
/// will write into the same container.
///
/// This is the same comparison authority the source preflight uses
/// (compare_bitstream_compatibility) — no second table, and the source is
/// never allowed to certify itself: the target is always the encoder's.
[[nodiscard]] bool encoder_reproduces_declared_stream(
    const BitstreamTargetContract& declared,
    const BitstreamTargetContract& encoder_contract) noexcept;
#endif

} // namespace chronon3d::cli
