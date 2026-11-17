#include <chronon3d/media/video/canonical_x264.hpp>

#include <array>
#include <string>

extern "C" {
#include <libavutil/hash.h>
}

namespace chronon3d::media::video {
namespace {

struct AnnexBNal {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

// Splits libx264's Annex-B extradata into NAL units. The encoder publishes
// SPS/PPS with 3- or 4-byte start codes; the MP4 muxer is what turns them into
// the avcC record whose hash RenderingGen certifies.
[[nodiscard]] bool annexb_nals(const std::uint8_t* data, int size,
                               std::vector<AnnexBNal>& out) {
    const auto total = static_cast<std::size_t>(size);
    std::size_t index = 0;
    std::size_t start = 0;
    bool in_nal = false;
    while (index + 3 <= total) {
        std::size_t prefix = 0;
        if (index + 4 <= total && data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 0 && data[index + 3] == 1) {
            prefix = 4;
        } else if (data[index] == 0 && data[index + 1] == 0 &&
                   data[index + 2] == 1) {
            prefix = 3;
        }
        if (prefix == 0) {
            ++index;
            continue;
        }
        if (in_nal) {
            out.push_back({data + start, index - start});
        }
        index += prefix;
        start = index;
        in_nal = true;
    }
    if (in_nal && start < total) {
        out.push_back({data + start, total - start});
    }
    return !out.empty();
}

} // namespace

std::string canonical_x264_sha256_hex(const std::uint8_t* data,
                                      std::size_t size) {
    AVHashContext* context = nullptr;
    if (av_hash_alloc(&context, "sha256") < 0 || context == nullptr) {
        return {};
    }
    av_hash_init(context);
    av_hash_update(context, data, size);
    const int digest_size = av_hash_get_size(context);
    std::array<std::uint8_t, AV_HASH_MAX_SIZE> digest{};
    av_hash_final(context, digest.data());
    av_hash_freep(&context);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_size) * 2U);
    for (int i = 0; i < digest_size; ++i) {
        out.push_back(kHex[digest[static_cast<std::size_t>(i)] >> 4U]);
        out.push_back(kHex[digest[static_cast<std::size_t>(i)] & 0x0fU]);
    }
    return out;
}

bool canonical_x264_avcc_bytes(const std::uint8_t* extradata, int size,
                               std::vector<std::uint8_t>& out,
                               std::string& reason) {
    if (extradata == nullptr || size <= 0) {
        reason = "canonical x264 lane: encoder produced no extradata";
        return false;
    }
    const auto total = static_cast<std::size_t>(size);
    // Already an AVCDecoderConfigurationRecord (future encoder paths).
    if (total >= 7 && extradata[0] == 1) {
        out.assign(extradata, extradata + total);
        return true;
    }
    std::vector<AnnexBNal> nals;
    if (!annexb_nals(extradata, size, nals)) {
        reason = "canonical x264 lane: extradata is neither avcC nor Annex-B";
        return false;
    }
    const AnnexBNal* sps = nullptr;
    const AnnexBNal* pps = nullptr;
    for (const auto& nal : nals) {
        if (nal.size == 0) continue;
        const int type = nal.data[0] & 0x1F;
        if (type == 7) {
            if (sps != nullptr) {
                reason = "canonical x264 lane: expected one SPS, got several";
                return false;
            }
            sps = &nal;
        } else if (type == 8) {
            if (pps != nullptr) {
                reason = "canonical x264 lane: expected one PPS, got several";
                return false;
            }
            pps = &nal;
        }
    }
    if (sps == nullptr || pps == nullptr || sps->size < 4 ||
        sps->size > 0xFFFFU || pps->size > 0xFFFFU) {
        reason = "canonical x264 lane: extradata lacks a usable SPS/PPS";
        return false;
    }
    out.clear();
    out.reserve(sps->size + pps->size + 16U);
    out.push_back(1);                 // configurationVersion
    out.push_back(sps->data[1]);      // AVCProfileIndication
    out.push_back(sps->data[2]);      // profile_compatibility
    out.push_back(sps->data[3]);      // AVCLevelIndication
    out.push_back(0xFF);              // lengthSizeMinusOne = 3
    out.push_back(0xE1);              // one SPS
    out.push_back(static_cast<std::uint8_t>((sps->size >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(sps->size & 0xFFU));
    out.insert(out.end(), sps->data, sps->data + sps->size);
    out.push_back(1);                 // one PPS
    out.push_back(static_cast<std::uint8_t>((pps->size >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(pps->size & 0xFFU));
    out.insert(out.end(), pps->data, pps->data + pps->size);
    const std::uint8_t profile = sps->data[1];
    if (profile == 100 || profile == 110 || profile == 122 || profile == 144) {
        // High-profile extension: chroma_format_idc = 1 (4:2:0), 8-bit
        // luma/chroma, no SPS extension. This is exactly what the MP4 muxer
        // appends for the canonical yuv420p stream.
        out.push_back(0xFD);
        out.push_back(0xF8);
        out.push_back(0xF8);
        out.push_back(0x00);
    }
    return true;
}

CanonicalX264AvcC canonical_x264_verify_extradata(
    const std::uint8_t* extradata, int size) {
    CanonicalX264AvcC result;
    std::vector<std::uint8_t> avcc;
    if (!canonical_x264_avcc_bytes(extradata, size, avcc, result.reason)) {
        return result;
    }
    result.sha256 = canonical_x264_sha256_hex(avcc.data(), avcc.size());
    if (result.sha256 != kCanonicalAvcCSHA256) {
        result.reason = "canonical x264 lane: emitted avcC " + result.sha256 +
                        " does not match the certified " +
                        std::string(kCanonicalAvcCSHA256);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace chronon3d::media::video
