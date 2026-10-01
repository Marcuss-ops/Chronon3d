// SPDX-License-Identifier: MIT
//
// gop_smart_copy_bitstream.cpp — byte-level bitstream format helpers.
//
// Split out of gop_smart_copy.cpp: the NAL/Annex-B/configuration-record
// readers (now the shared detail seam in gop_smart_copy_bitstream.hpp) plus the
// format functions declared in gop_smart_copy.hpp. No demuxing, planning or
// muxing lives here.

#include "gop_smart_copy.hpp"
#include "gop_smart_copy_bitstream.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace chronon3d::cli::detail {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

/// Length of the Annex-B start code at `offset` (3 or 4 bytes), or 0 when
/// there is none. The one start-code reader the packet-format helpers share.
std::size_t annex_b_start_code_length_at(const std::uint8_t* data,
                                         std::size_t size,
                                         std::size_t offset) noexcept {
    if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
        data[offset + 2] == 1) {
        return 3;
    }
    if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
        data[offset + 2] == 0 && data[offset + 3] == 1) {
        return 4;
    }
    return 0;
}

/// True for the video parameter sets of `codec` (H.264 SPS/PPS, HEVC
/// VPS/SPS/PPS). Everything else — SEI, slices, filler — is not part of the
/// stream identity the splice gate compares.
bool nal_is_parameter_set(AVCodecID codec, std::uint8_t nal_header) noexcept {
    if (codec == AV_CODEC_ID_H264) {
        const unsigned type = nal_header & 0x1fU;
        return type == 7U || type == 8U; // SPS, PPS
    }
    if (codec == AV_CODEC_ID_HEVC) {
        const unsigned type = (nal_header >> 1U) & 0x3fU;
        return type == 32U || type == 33U || type == 34U; // VPS, SPS, PPS
    }
    return false;
}

/// Split an Annex-B byte range (an encoder's in-band parameter sets) into NAL
/// payloads, keeping only the parameter sets. Start codes are consumed, trailing
/// zero padding is trimmed, and the byte order of the stream is preserved.
void append_annex_b_parameter_sets(
    AVCodecID codec, const std::uint8_t* data, std::size_t size,
    std::vector<std::vector<std::uint8_t>>& out) {
    const auto start_code_at = [&](std::size_t offset, std::size_t& length) {
        if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 1) {
            length = 3;
            return true;
        }
        if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 0 && data[offset + 3] == 1) {
            length = 4;
            return true;
        }
        return false;
    };
    std::size_t nal_start = 0;
    bool have_nal = false;
    std::size_t cursor = 0;
    while (cursor < size) {
        std::size_t start_length = 0;
        if (!start_code_at(cursor, start_length)) {
            ++cursor;
            continue;
        }
        if (have_nal) {
            std::size_t nal_end = cursor;
            while (nal_end > nal_start && data[nal_end - 1] == 0) --nal_end;
            if (nal_end > nal_start && nal_is_parameter_set(codec, data[nal_start])) {
                out.emplace_back(data + nal_start, data + nal_end);
            }
        }
        cursor += start_length;
        nal_start = cursor;
        have_nal = true;
    }
    if (have_nal) {
        std::size_t nal_end = size;
        while (nal_end > nal_start && data[nal_end - 1] == 0) --nal_end;
        if (nal_end > nal_start && nal_is_parameter_set(codec, data[nal_start])) {
            out.emplace_back(data + nal_start, data + nal_end);
        }
    }
}

bool nal_is_random_access(AVCodecID codec, const std::uint8_t* nal,
                          std::size_t size) noexcept {
    if (!nal || size == 0) return false;
    if (codec == AV_CODEC_ID_H264) {
        return (nal[0] & 0x1fU) == 5U; // IDR slice
    }
    if (codec == AV_CODEC_ID_HEVC) {
        const unsigned type = (nal[0] >> 1U) & 0x3fU;
        return type >= 16U && type <= 21U; // BLA/IDR/CRA random access
    }
    return false;
}

/// NAL length size declared by an AVC/HEVC configuration record.
///
/// The offset is CODEC-SPECIFIC and must never be shared: `avcC` carries
/// lengthSizeMinusOne in byte 4, `hvcC` in byte 21. Reading the AVC offset out
/// of an HEVC record yielded a bogus length (1 instead of 4), which made every
/// HEVC packet fail random-access certification and silently downgraded the
/// whole HEVC packet-copy lane to a full re-encode.
///
/// Returns 0 when no configuration record valid for `codec` is present.
std::size_t config_record_nal_length_size(
    AVCodecID codec, const AVCodecParameters& params) noexcept {
    if (!params.extradata || params.extradata_size <= 0) return 0;
    const auto* extradata = params.extradata;
    const int size = params.extradata_size;
    if (codec == AV_CODEC_ID_H264) {
        // avcC: configurationVersion(1) .. lengthSizeMinusOne at byte 4.
        if (size < 7 || extradata[0] != 1) return 0;
        return static_cast<std::size_t>((extradata[4] & 0x03U) + 1U);
    }
    if (codec == AV_CODEC_ID_HEVC) {
        // hvcC: configurationVersion(1), profile/tier/level(11),
        // min_spatial_segmentation(2), parallelismType(1), chromaFormat(1),
        // bitDepthLuma(1), bitDepthChroma(1), avgFrameRate(2),
        // constantFrameRate/numTemporalLayers/temporalIdNested/
        // lengthSizeMinusOne(1) → byte 21.
        if (size < 23 || extradata[0] != 1) return 0;
        return static_cast<std::size_t>((extradata[21] & 0x03U) + 1U);
    }
    return 0;
}

bool packet_is_certified_random_access(
    const AVPacket& packet, const AVCodecParameters& params) noexcept {
    if ((packet.flags & AV_PKT_FLAG_KEY) == 0 || !packet.data || packet.size <= 0) {
        return false;
    }
    if (params.codec_id != AV_CODEC_ID_H264 && params.codec_id != AV_CODEC_ID_HEVC) {
        return false;
    }

    const auto* data = packet.data;
    const std::size_t size = static_cast<std::size_t>(packet.size);

    // A valid AVC/HEVC configuration record means the packets are
    // length-prefixed (MP4/MKV). Parse them deterministically — `00 00 01`
    // appears inside ordinary slice data, so inferring Annex B from the
    // payload bytes misclassifies valid streams.
    if (const std::size_t length_size =
            config_record_nal_length_size(params.codec_id, params);
        length_size > 0) {
        std::size_t offset = 0;
        while (offset + length_size <= size) {
            std::uint32_t nal_size = 0;
            for (std::size_t byte = 0; byte < length_size; ++byte) {
                nal_size = (nal_size << 8U) | data[offset + byte];
            }
            offset += length_size;
            if (nal_size == 0 || offset + nal_size > size) return false;
            if (nal_is_random_access(params.codec_id, data + offset, nal_size)) {
                return true;
            }
            offset += nal_size;
        }
        return offset == size;
    }

    // No configuration record: the packet is expected to carry in-band
    // Annex B start codes.
    const auto has_annexb_start = [&]() {
        for (std::size_t i = 0; i + 3 < size; ++i) {
            if (data[i] == 0 && data[i + 1] == 0 &&
                (data[i + 2] == 1 ||
                 (data[i + 2] == 0 && data[i + 3] == 1))) {
                return true;
            }
        }
        return false;
    };

    if (has_annexb_start()) {
        std::size_t i = 0;
        while (i + 3 < size) {
            while (i + 3 < size &&
                   !(data[i] == 0 && data[i + 1] == 0 &&
                     (data[i + 2] == 1 ||
                      (data[i + 2] == 0 && data[i + 3] == 1)))) {
                ++i;
            }
            if (i + 3 >= size) break;
            i += data[i + 2] == 1 ? 3 : 4;
            const std::size_t nal_start = i;
            while (i + 3 < size &&
                   !(data[i] == 0 && data[i + 1] == 0 &&
                     (data[i + 2] == 1 ||
                      (data[i + 2] == 0 && data[i + 3] == 1)))) {
                ++i;
            }
            if (nal_is_random_access(params.codec_id, data + nal_start,
                                     i - nal_start)) {
                return true;
            }
        }
        return false;
    }

    // An Annex-B packet with no configuration record cannot be certified: the
    // splice contract is fail-closed, so unknown syntax is rejected rather
    // than guessed.
    return false;
}

bool codec_matches(AVCodecID source, std::string_view requested) noexcept {
    if (requested == "h264" || requested == "libx264" || requested == "h264_nvenc") {
        return source == AV_CODEC_ID_H264;
    }
    if (requested == "hevc" || requested == "h265" || requested == "libx265" ||
        requested == "hevc_nvenc") {
        return source == AV_CODEC_ID_HEVC;
    }
    if (requested == "av1" || requested == "libsvtav1" || requested == "av1_nvenc") {
        return source == AV_CODEC_ID_AV1;
    }
    if (requested == "vp9") return source == AV_CODEC_ID_VP9;
    return false;
}
bool extradata_equal(const AVCodecParameters& source,
                     const AVCodecParameters& output) noexcept {
    if (source.extradata_size <= 0 || output.extradata_size <= 0 ||
        !source.extradata || !output.extradata) {
        return false;
    }
    return source.extradata_size == output.extradata_size &&
        std::memcmp(source.extradata, output.extradata,
                    static_cast<std::size_t>(source.extradata_size)) == 0;
}

#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG

} // namespace chronon3d::cli::detail

namespace chronon3d::cli {

std::size_t leading_parameter_set_prefix_size(
    BitstreamCodecId codec, const std::uint8_t* data, std::size_t size) noexcept {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)codec; (void)data; (void)size;
    return 0;
#else
    if (!data || size == 0) return 0;
    const auto start_code_at = [&](std::size_t offset, std::size_t& length) {
        if (offset + 3 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 1) {
            length = 3;
            return true;
        }
        if (offset + 4 <= size && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 0 && data[offset + 3] == 1) {
            length = 4;
            return true;
        }
        return false;
    };
    std::size_t offset = 0;
    std::size_t prefix_end = 0;
    while (offset < size) {
        std::size_t start_length = 0;
        if (!start_code_at(offset, start_length)) break;
        const std::size_t nal_start = offset + start_length;
        if (nal_start >= size ||
            !detail::nal_is_parameter_set(codec, data[nal_start])) {
            break;
        }
        // The NAL runs to the next start code (or to the end of the payload);
        // trailing zero padding belongs to the next start code.
        std::size_t cursor = nal_start + 1;
        while (cursor < size) {
            std::size_t next_length = 0;
            if (start_code_at(cursor, next_length)) break;
            ++cursor;
        }
        std::size_t nal_end = cursor;
        while (nal_end > nal_start && data[nal_end - 1] == 0) --nal_end;
        if (nal_end <= nal_start) break;
        prefix_end = cursor;
        offset = cursor;
    }
    return prefix_end;
#endif
}

bool parameter_sets_are_config_record(BitstreamCodecId codec,
                                      const std::uint8_t* data,
                                      std::size_t size) noexcept {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)codec; (void)data; (void)size;
    return false;
#else
    // avcC / hvcC: configurationVersion is the first byte, and only these two
    // codecs use that record shape here. A record means the container stores
    // its samples LENGTH-PREFIXED; Annex-B extradata means it does not.
    if (!data || size < 5) return false;
    if (codec != AV_CODEC_ID_H264 && codec != AV_CODEC_ID_HEVC) return false;
    return data[0] == 1;
#endif
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
bool convert_annex_b_to_length_prefixed(AVPacket& packet,
                                        BitstreamCodecId codec) noexcept {
    if (codec != AV_CODEC_ID_H264 && codec != AV_CODEC_ID_HEVC) return false;
    if (!packet.data || packet.size <= 0) return false;
    const std::uint8_t* data = packet.data;
    const std::size_t size = static_cast<std::size_t>(packet.size);
    // Already in the container's own format: nothing to rewrite.
    if (detail::annex_b_start_code_length_at(data, size, 0) == 0) return false;

    std::vector<std::pair<std::size_t, std::size_t>> nals;
    std::size_t index = 0;
    std::size_t nal_offset = std::numeric_limits<std::size_t>::max();
    while (index < size) {
        const std::size_t prefix =
            detail::annex_b_start_code_length_at(data, size, index);
        if (prefix > 0) {
            if (nal_offset != std::numeric_limits<std::size_t>::max()) {
                std::size_t end = index;
                while (end > nal_offset && data[end - 1] == 0) --end;
                if (end > nal_offset) {
                    nals.emplace_back(nal_offset, end - nal_offset);
                }
            }
            index += prefix;
            nal_offset = index;
            continue;
        }
        ++index;
    }
    if (nal_offset != std::numeric_limits<std::size_t>::max() &&
        nal_offset < size) {
        std::size_t end = size;
        while (end > nal_offset && data[end - 1] == 0) --end;
        if (end > nal_offset) nals.emplace_back(nal_offset, end - nal_offset);
    }
    if (nals.empty()) return false;

    std::size_t total = 0;
    for (const auto& nal : nals) total += 4 + nal.second;
    auto* buffer = static_cast<std::uint8_t*>(
        av_mallocz(total + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!buffer) return false;
    std::size_t write = 0;
    for (const auto& nal : nals) {
        const std::size_t length = nal.second;
        buffer[write++] = static_cast<std::uint8_t>((length >> 24) & 0xffU);
        buffer[write++] = static_cast<std::uint8_t>((length >> 16) & 0xffU);
        buffer[write++] = static_cast<std::uint8_t>((length >> 8) & 0xffU);
        buffer[write++] = static_cast<std::uint8_t>(length & 0xffU);
        std::memcpy(buffer + write, data + nal.first, length);
        write += length;
    }
    AVPacket converted{};
    if (av_packet_from_data(&converted, buffer,
                            static_cast<int>(write)) < 0) {
        av_free(buffer);
        return false;
    }
    // Timing, flags and side data belong to the packet, not to its framing.
    av_packet_copy_props(&converted, &packet);
    av_packet_move_ref(&packet, &converted);
    return true;
}
#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG

std::vector<std::vector<std::uint8_t>> parameter_set_nals(
    BitstreamCodecId codec, const std::uint8_t* data, std::size_t size) {
    std::vector<std::vector<std::uint8_t>> nals;
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)codec; (void)data; (void)size;
    return nals;
#else
    if (!data || size == 0) return nals;

    // ── Configuration record: what a DEMUXER reports ──────────────────────
    if (data[0] == 1) {
        if (codec == AV_CODEC_ID_H264 && size >= 7) {
            // AVCDecoderConfigurationRecord: every parameter set is preceded by
            // a 16-BIT length. The byte-4 width only describes how SAMPLE
            // packets are length-prefixed, so using it here silently misparsed
            // every avcC (length 0 or past the end) and returned no NALs at
            // all — i.e. the splice gate could never certify an mp4 source.
            std::size_t offset = 6;
            const auto read_one = [&](std::size_t& position) {
                if (position + 2 > size) return false;
                const std::size_t length =
                    (static_cast<std::size_t>(data[position]) << 8) |
                    static_cast<std::size_t>(data[position + 1]);
                position += 2;
                if (length == 0 || position + length > size) return false;
                nals.emplace_back(data + position, data + position + length);
                position += length;
                return true;
            };
            const std::size_t sps_count = data[5] & 0x1fU;
            for (std::size_t index = 0; index < sps_count; ++index) {
                if (!read_one(offset)) return nals;
            }
            if (offset >= size) return nals;
            const std::size_t pps_count = data[offset++];
            for (std::size_t index = 0; index < pps_count; ++index) {
                if (!read_one(offset)) return nals;
            }
            return nals;
        }
        if (codec == AV_CODEC_ID_HEVC && size >= 23) {
            std::size_t offset = 22;
            const std::size_t arrays = data[offset++];
            for (std::size_t array = 0; array < arrays; ++array) {
                if (offset + 3 > size) return nals;
                const std::uint8_t nal_header =
                    static_cast<std::uint8_t>(data[offset] & 0x3fU);
                const std::size_t count =
                    (static_cast<std::size_t>(data[offset + 1]) << 8) |
                    static_cast<std::size_t>(data[offset + 2]);
                offset += 3;
                for (std::size_t index = 0; index < count; ++index) {
                    if (offset + 2 > size) return nals;
                    const std::size_t length =
                        (static_cast<std::size_t>(data[offset]) << 8) |
                        static_cast<std::size_t>(data[offset + 1]);
                    offset += 2;
                    if (length == 0 || offset + length > size) return nals;
                    if (nal_header == 32U || nal_header == 33U ||
                        nal_header == 34U) {
                        nals.emplace_back(data + offset, data + offset + length);
                    }
                    offset += length;
                }
            }
            return nals;
        }
    }

    // ── Annex-B: what an ENCODER publishes in-band ────────────────────────
    detail::append_annex_b_parameter_sets(codec, data, size, nals);
    return nals;
#endif
}

} // namespace chronon3d::cli
