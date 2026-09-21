#include "packet_assembler_segments.hpp"

#include <cstring>

extern "C" {
#include <libavcodec/version.h>
#include <libavutil/channel_layout.h>
}

namespace chronon3d::media::detail {
namespace {

bool same_bytes(const std::uint8_t* left, int left_size,
                const std::uint8_t* right, int right_size) noexcept {
    return left_size == right_size && (left_size == 0 ||
        (left != nullptr && right != nullptr &&
         std::memcmp(left, right, static_cast<std::size_t>(left_size)) == 0));
}

} // namespace

bool compatible_codec_parameters(const AVCodecParameters* left,
                                 const AVCodecParameters* right,
                                 std::string& reason) {
    if (!left || !right) {
        reason = "missing codec parameters";
        return false;
    }
    if (left->codec_type != right->codec_type ||
        left->codec_id != right->codec_id ||
        left->profile != right->profile || left->level != right->level) {
        reason = "codec/profile/level mismatch";
        return false;
    }
    if (left->codec_type == AVMEDIA_TYPE_VIDEO &&
        (left->width != right->width || left->height != right->height ||
         left->format != right->format)) {
        reason = "video resolution or pixel format mismatch";
        return false;
    }
    if (left->codec_type == AVMEDIA_TYPE_AUDIO &&
        (left->sample_rate != right->sample_rate || left->format != right->format ||
#if LIBAVCODEC_VERSION_MAJOR >= 59
         av_channel_layout_compare(&left->ch_layout, &right->ch_layout) != 0)) {
#else
         left->channel_layout != right->channel_layout ||
         left->channels != right->channels)) {
#endif
        reason = "audio sample rate, format, or channel layout mismatch";
        return false;
    }
    if (!same_bytes(left->extradata, left->extradata_size,
                    right->extradata, right->extradata_size)) {
        reason = "codec extradata mismatch";
        return false;
    }
    return true;
}

std::int64_t stream_duration(const AVStream* stream) noexcept {
    if (!stream || stream->duration == AV_NOPTS_VALUE || stream->duration < 0) {
        return 0;
    }
    return stream->duration;
}

std::int64_t normalize_timestamp(const std::int64_t value,
                                 const std::int64_t first,
                                 const std::int64_t offset) noexcept {
    if (value == AV_NOPTS_VALUE) return value;
    return value - first + offset;
}

} // namespace chronon3d::media::detail
