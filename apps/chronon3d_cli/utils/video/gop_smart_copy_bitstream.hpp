#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// gop_smart_copy_bitstream.hpp — internal byte-level bitstream helpers.
//
// Split out of gop_smart_copy.cpp (P2 structure pass): the NAL / Annex-B /
// configuration-record readers were one anonymous namespace used by both the
// bitstream-format functions and the source-analysis functions.  They now live
// in their own TU; this header is the internal seam so the source TU can still
// certify packet random access without duplicating the readers.
//
// Internal-only: lives under apps/chronon3d_cli/utils/video/, not in include/.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

namespace chronon3d::cli::detail {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

/// Length of the Annex-B start code at `offset` (3 or 4 bytes), or 0 when
/// there is none. The one start-code reader the packet-format helpers share.
std::size_t annex_b_start_code_length_at(const std::uint8_t* data,
                                         std::size_t size,
                                         std::size_t offset) noexcept;

/// True for the video parameter sets of `codec` (H.264 SPS/PPS, HEVC
/// VPS/SPS/PPS).
bool nal_is_parameter_set(AVCodecID codec, std::uint8_t nal_header) noexcept;

/// Split an Annex-B byte range (an encoder's in-band parameter sets) into NAL
/// payloads, keeping only the parameter sets.
void append_annex_b_parameter_sets(
    AVCodecID codec, const std::uint8_t* data, std::size_t size,
    std::vector<std::vector<std::uint8_t>>& out);

/// True when the packet payload carries an IDR/BLA/CRA random-access NAL.
bool nal_is_random_access(AVCodecID codec, const std::uint8_t* nal,
                          std::size_t size) noexcept;

/// NAL length size declared by an AVC/HEVC configuration record, or 0 when no
/// record valid for `codec` is present.
std::size_t config_record_nal_length_size(
    AVCodecID codec, const AVCodecParameters& params) noexcept;

/// Fail-closed packet random-access certification (configuration-record or
/// in-band Annex-B depending on the codec parameters).
bool packet_is_certified_random_access(
    const AVPacket& packet, const AVCodecParameters& params) noexcept;

/// Map a requested codec name (encoder or codec id) to a source codec id.
bool codec_matches(AVCodecID source, std::string_view requested) noexcept;

/// Byte-identical extradata comparison.
bool extradata_equal(const AVCodecParameters& source,
                     const AVCodecParameters& output) noexcept;

#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG

} // namespace chronon3d::cli::detail
