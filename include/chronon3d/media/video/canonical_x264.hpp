#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// include/chronon3d/media/video/canonical_x264.hpp
//
// The canonical Velox x264 stream identity (avcC / SPS-PPS), measured from the
// certified 1920x1080 24 fps lane. This is a BYTE identity, not a nominal
// profile: a lane that emits it must reproduce exactly these parameter sets
// and fail closed otherwise.
//
// See Chronon3d/docs/tickets/TICKET-CANONICAL-X264-SINGLE-SESSION-20261116.md.
//
// libx264 publishes its parameter sets as Annex-B extradata (SPS/PPS with
// start codes); the MP4 muxer is what turns them into the
// AVCDecoderConfigurationRecord a consumer reads back. `canonical_x264_
// avcc_bytes` rebuilds that record so the digest is over the same bytes
// RenderingGen calls avcC, not over the encoder's staging form.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media::video {

/// Wire/profile name that selects the lane (output_spec.encoder_profile).
inline constexpr std::string_view kCanonicalX264Profile = "canonical_x264";

/// SHA-256 of the 49-byte AVCDecoderConfigurationRecord below.
inline constexpr std::string_view kCanonicalAvcCSHA256 =
    "7201ce80a2b2b32a0c1a89501851099f267c1767d59ff799db744f72032aae5a";

inline constexpr int kCanonicalX264Width = 1920;
inline constexpr int kCanonicalX264Height = 1080;
inline constexpr int kCanonicalX264FPSNum = 24;
inline constexpr int kCanonicalX264FPSDen = 1;
inline constexpr int kCanonicalX264Gop = 48;
inline constexpr int kCanonicalX264MinKeyint = 48;
inline constexpr int kCanonicalX264Crf = 23;
inline constexpr int kCanonicalX264VideoTimescale = 90000;
inline constexpr std::string_view kCanonicalX264Preset = "veryfast";
inline constexpr std::string_view kCanonicalX264Params =
    "scenecut=0:open-gop=0:keyint=48:min-keyint=48";

/// Result of checking one extradata blob against the certified identity.
struct CanonicalX264AvcC {
    bool ok{false};
    std::string sha256;
    std::string reason;
};

/// SHA-256 as lowercase hex; empty when the hash backend is unavailable.
[[nodiscard]] std::string canonical_x264_sha256_hex(
    const std::uint8_t* data, std::size_t size);

/// Rebuild the AVCDecoderConfigurationRecord the MP4 muxer writes for one SPS
/// and one PPS. Accepts extradata that is already an avcC record. Returns
/// false (with `reason`) when the extradata is neither form or lacks a usable
/// SPS/PPS pair.
[[nodiscard]] bool canonical_x264_avcc_bytes(
    const std::uint8_t* extradata, int size,
    std::vector<std::uint8_t>& out, std::string& reason);

/// Rebuild + hash one extradata blob. `ok` is true only when the digest equals
/// kCanonicalAvcCSHA256. This is the byte oracle every canonical lane must
/// pass before it writes an output file.
[[nodiscard]] CanonicalX264AvcC canonical_x264_verify_extradata(
    const std::uint8_t* extradata, int size);

} // namespace chronon3d::media::video
