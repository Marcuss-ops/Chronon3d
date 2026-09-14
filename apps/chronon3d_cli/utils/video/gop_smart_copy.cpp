#include "gop_smart_copy.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include <chronon3d/media/video/packet_assembler.hpp>
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <limits>
#include <cstring>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
namespace {
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

    // AVC/HEVC in MP4/MKV is normally length-prefixed. The NAL length size
    // is carried in avcC/hvcC extradata; unknown lengths are rejected rather
    // than guessed, preserving the fail-closed splice contract.
    if (!params.extradata || params.extradata_size < 5 ||
        params.extradata[0] != 1) {
        return false;
    }
    const std::size_t length_size =
        static_cast<std::size_t>((params.extradata[4] & 0x03) + 1);
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
} // namespace
#endif

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
BitstreamCompatibility compare_bitstream_compatibility(
    const AVCodecParameters& source,
    const BitstreamTargetContract& target,
    bool random_access_safe) noexcept {
    BitstreamCompatibility result;
    result.codec_match = source.codec_id != AV_CODEC_ID_NONE &&
        source.codec_id == target.codec;
    result.profile_match = source.profile >= 0 &&
        source.profile == target.profile;
    result.level_compatible = source.level >= 0 &&
        target.level >= 0 && source.level == target.level;
    result.dimensions_match = source.width > 0 && source.height > 0 &&
        source.width == target.width && source.height == target.height;
    result.pixel_format_match = source.format >= 0 &&
        source.format == target.pixel_format;
    result.parameter_sets_compatible = source.extradata &&
        source.extradata_size > 0 &&
        source.extradata_size == static_cast<int>(target.parameter_sets.size()) &&
        std::memcmp(source.extradata, target.parameter_sets.data(),
                    target.parameter_sets.size()) == 0;
    result.color_params_match = source.color_range == target.color_range &&
        source.color_space == target.color_space &&
        source.color_primaries == target.color_primaries &&
        source.color_trc == target.color_trc;
    result.random_access_safe = random_access_safe;
    return result;
}
#endif

std::optional<GopSourceAnalysis> inspect_gop_source(
    const std::string& path,
    const BitstreamTargetContract& target,
    double edit_start_seconds,
    double edit_end_seconds,
    bool content_changed,
    std::span<const chronon3d::media::ChangedInterval> changed_intervals) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)path;
    (void)target;
    (void)edit_start_seconds;
    (void)edit_end_seconds;
    (void)content_changed;
    (void)changed_intervals;
    return std::nullopt;
#else
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    const auto close_format = [&]() { avformat_close_input(&format); };
    if (avformat_find_stream_info(format, nullptr) < 0) {
        close_format();
        return std::nullopt;
    }

    int video_stream_index = -1;
    for (unsigned int i = 0; i < format->nb_streams; ++i) {
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = static_cast<int>(i);
            break;
        }
    }
    if (video_stream_index < 0) {
        close_format();
        return std::nullopt;
    }
    const auto* source_params = format->streams[video_stream_index]->codecpar;
    const bool codec_parameters_match = source_params->codec_id == target.codec;
    const auto source_compatibility = compare_bitstream_compatibility(
        *source_params, target, true);
    const auto time_base = format->streams[video_stream_index]->time_base;
    const double time_base_seconds = av_q2d(time_base);
    if (!(time_base_seconds > 0.0) || edit_start_seconds < 0.0 ||
        !(edit_end_seconds > edit_start_seconds)) {
        close_format();
        return std::nullopt;
    }
    const auto edit_start = static_cast<std::int64_t>(std::llround(
        edit_start_seconds / time_base_seconds));
    const auto edit_end = static_cast<std::int64_t>(std::llround(
        edit_end_seconds / time_base_seconds));
    const auto* video_stream = format->streams[video_stream_index];
    const double source_duration_seconds = video_stream->duration != AV_NOPTS_VALUE
        ? static_cast<double>(video_stream->duration) * time_base_seconds
        : (format->duration != AV_NOPTS_VALUE
            ? static_cast<double>(format->duration) / static_cast<double>(AV_TIME_BASE)
            : 0.0);
    for (const auto& interval : changed_intervals) {
        if (!interval.valid()) {
            close_format();
            return std::nullopt;
        }
    }
    const bool interval_content_changed = std::any_of(
        changed_intervals.begin(), changed_intervals.end(),
        [](const auto& interval) {
            return interval.kind == chronon3d::media::ChangedIntervalKind::Content;
        });
    const bool effective_content_changed = changed_intervals.empty()
        ? content_changed : interval_content_changed;
    const bool explicit_trim = std::any_of(
        changed_intervals.begin(), changed_intervals.end(),
        [](const auto& interval) {
            return interval.kind == chronon3d::media::ChangedIntervalKind::Trim;
        });
    const bool trim_requested = !effective_content_changed &&
        (explicit_trim ||
        (edit_start_seconds > 0.0 ||
         (source_duration_seconds > 0.0 && edit_end_seconds + 1e-9 <
              source_duration_seconds)));

    std::vector<std::vector<CompressedPacketInfo>> groups;
    bool has_trim_gop = false;
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        close_format();
        return std::nullopt;
    }
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            const bool random_access = packet_is_certified_random_access(
                *packet, *source_params);
            const CompressedPacketInfo info{
                .pts = packet->pts == AV_NOPTS_VALUE ? packet->dts : packet->pts,
                .dts = packet->dts == AV_NOPTS_VALUE ? packet->pts : packet->dts,
                .keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0,
                // A GOP starts at a keyframe. Packets before the first
                // keyframe are deliberately left out of copy candidates.
                // Ordinary I-frames and unknown packet syntax are not
                // certified random-access boundaries.
                .references_prior_gop = !random_access,
                .random_access_certified = random_access,
            };
            if (info.keyframe || groups.empty()) {
                groups.emplace_back();
            }
            groups.back().push_back(info);
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);

    GopSourceAnalysis result;
    result.source_time_base_num = time_base.num;
    result.source_time_base_den = time_base.den;
    for (const auto& group : groups) {
        if (group.empty()) continue;
        const auto& first = group.front();
        if (!effective_content_changed && first.pts != AV_NOPTS_VALUE &&
            first.pts >= edit_start && first.pts < edit_end) {
            has_trim_gop = true;
        }
        // A trim/copy request changes the selected time range, not pixels.
        // Only a content-changing request marks intersecting GOPs for
        // re-encode. Keeping this fact explicit prevents a full-clip
        // `gop_copy_only` request from being classified as an edit merely
        // because its output interval is non-empty.
        bool intersects = false;
        if (changed_intervals.empty()) {
            intersects = content_changed && gop_intersects_edit(
                group, edit_start, edit_end);
        } else {
            for (const auto& interval : changed_intervals) {
                if (interval.kind != chronon3d::media::ChangedIntervalKind::Content) {
                    continue;
                }
                const auto interval_start = static_cast<std::int64_t>(std::llround(
                    interval.start_seconds / time_base_seconds));
                const auto interval_end = static_cast<std::int64_t>(std::llround(
                    interval.end_seconds / time_base_seconds));
                if (gop_intersects_edit(group, interval_start, interval_end)) {
                    intersects = true;
                    break;
                }
            }
        }
        auto analysis = analyze_gop(group, codec_parameters_match, intersects);
        analysis.compatibility = source_compatibility;
        auto plan = plan_gop(analysis);
        plan.ordinal = result.plans.size();
        result.plans.push_back(plan);
    }
    close_format();
    if (result.plans.empty()) return std::nullopt;

    // A key packet is not sufficient to certify a splice boundary. If the
    // first packet of a GOP is an ordinary I-frame (or its syntax is
    // unknown), conservatively expand the re-encode closure backwards to the
    // last certified random-access GOP. This keeps the copied stream outside
    // the unsafe boundary and gives the re-encoder a dependency window it can
    // decode before emitting a forced-IDR output GOP.
    for (std::size_t index = 0; index < result.plans.size(); ++index) {
        auto& plan = result.plans[index];
        if (plan.compatibility.random_access_safe) continue;

        std::size_t closure_start = index;
        while (closure_start > 0 &&
               !result.plans[closure_start].compatibility.random_access_safe) {
            --closure_start;
        }
        // Include the last certified GOP as the decode/render closure anchor:
        // the executor may decode it, but only the unsafe suffix is required
        // to be emitted as re-encoded output.
        if (closure_start > 0) --closure_start;
        for (std::size_t closure = closure_start; closure <= index; ++closure) {
            result.plans[closure].mode = GopExecutionMode::Reencode;
        }
        plan.decode_first_pts = result.plans[closure_start].first_pts;
        plan.decode_first_dts = result.plans[closure_start].first_dts;
    }

    result.first_pts = result.plans.front().first_pts;
    result.last_pts = result.plans.back().last_pts;
    // Aggregate the per-GOP plans into copy/reencode counts and the
    // all_copy_eligible flag. all_copy_eligible requires every GOP to
    // be copy-eligible AND the bitstream compatibility gate to be safe.
    result.copy_count = 0;
    result.reencode_count = 0;
    for (const auto& plan : result.plans) {
        if (plan.copy_packets()) {
            ++result.copy_count;
        } else {
            ++result.reencode_count;
        }
    }
    result.all_copy_eligible = result.valid() && result.copy_count == result.plans.size();
    result.trim_only = trim_requested && has_trim_gop && result.all_copy_eligible;
    return result;
#endif
}

std::optional<GopCopyResult> copy_gop_source(
    const std::string& source_path,
    const std::string& output_path,
    double start_seconds,
    double end_seconds) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)source_path;
    (void)output_path;
    (void)start_seconds;
    (void)end_seconds;
    return std::nullopt;
#else
    const auto parent = std::filesystem::path(output_path).parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) return std::nullopt;
    }
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, source_path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    const auto close_input = [&]() { avformat_close_input(&input); };
    if (avformat_find_stream_info(input, nullptr) < 0) {
        close_input();
        return std::nullopt;
    }

    int input_video = -1;
    int input_audio = -1;
    for (unsigned int i = 0; i < input->nb_streams; ++i) {
        const auto type = input->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && input_video < 0) input_video = static_cast<int>(i);
        if (type == AVMEDIA_TYPE_AUDIO && input_audio < 0) input_audio = static_cast<int>(i);
    }
    if (input_video < 0) {
        close_input();
        return std::nullopt;
    }

    const auto to_source_pts = [](double seconds, AVRational time_base) {
        return static_cast<std::int64_t>(std::llround(seconds / av_q2d(time_base)));
    };
    const auto video_time_base = input->streams[input_video]->time_base;
    const auto video_start = to_source_pts(start_seconds, video_time_base);
    const auto video_end = to_source_pts(end_seconds, video_time_base);
    if (start_seconds < 0.0 || !(end_seconds > start_seconds) ||
        video_end <= video_start) {
        close_input();
        return std::nullopt;
    }

    // Packet trimming is legal only at a random-access boundary. Find the
    // first complete GOP in the requested interval and stop before the first
    // keyframe outside it. This deliberately snaps the video window rather
    // than emitting a partial GOP with dangling references.
    std::int64_t selected_gop_start = AV_NOPTS_VALUE;
    std::int64_t selected_gop_end = AV_NOPTS_VALUE;
    std::int64_t last_video_timestamp = AV_NOPTS_VALUE;
    AVPacket* scan_packet = av_packet_alloc();
    if (!scan_packet) {
        close_input();
        return std::nullopt;
    }
    while (av_read_frame(input, scan_packet) >= 0) {
        if (scan_packet->stream_index == input_video) {
            const auto pts = scan_packet->pts != AV_NOPTS_VALUE
                ? scan_packet->pts : scan_packet->dts;
            if (pts != AV_NOPTS_VALUE) {
                last_video_timestamp = std::max(last_video_timestamp, pts);
                if ((scan_packet->flags & AV_PKT_FLAG_KEY) != 0) {
                    if (selected_gop_start == AV_NOPTS_VALUE &&
                        pts >= video_start && pts < video_end) {
                        selected_gop_start = pts;
                    } else if (selected_gop_start != AV_NOPTS_VALUE &&
                               pts >= video_end) {
                        selected_gop_end = pts;
                        break;
                    }
                }
            }
        }
        av_packet_unref(scan_packet);
    }
    av_packet_unref(scan_packet);
    av_packet_free(&scan_packet);
    if (selected_gop_start == AV_NOPTS_VALUE ||
        av_seek_frame(input, input_video, selected_gop_start,
                      AVSEEK_FLAG_BACKWARD) < 0) {
        close_input();
        return std::nullopt;
    }
    if (selected_gop_end == AV_NOPTS_VALUE) {
        const auto stream = input->streams[input_video];
        selected_gop_end = stream->duration != AV_NOPTS_VALUE
            ? av_rescale_q(stream->duration, stream->time_base, video_time_base)
            : (last_video_timestamp == AV_NOPTS_VALUE
                   ? AV_NOPTS_VALUE : last_video_timestamp + 1);
        if (selected_gop_end <= selected_gop_start) {
            close_input();
            return std::nullopt;
        }
    }
    avformat_flush(input);

    // Both ends are snapped to complete GOP boundaries. Audio follows the
    // same snapped window, so the copied streams have matching duration and
    // no packet in the output depends on material outside the trim.
    const double snapped_start_seconds =
        static_cast<double>(selected_gop_start) * av_q2d(video_time_base);
    const double snapped_end_seconds =
        static_cast<double>(selected_gop_end) * av_q2d(video_time_base);
    const auto audio_start = input_audio >= 0
        ? to_source_pts(snapped_start_seconds,
                        input->streams[input_audio]->time_base) : 0;
    const auto audio_end = input_audio >= 0
        ? to_source_pts(snapped_end_seconds, input->streams[input_audio]->time_base) : 0;

    chronon3d::media::MuxSession mux;
    std::optional<chronon3d::media::AudioStreamConfig> audio_config;
    if (input_audio >= 0) {
        audio_config = chronon3d::media::AudioStreamConfig{
            .params = input->streams[input_audio]->codecpar,
            .time_base = input->streams[input_audio]->time_base};
    }
    std::string mux_reason;
    if (!mux.open(chronon3d::media::MuxOpenConfig{
            .output_path = output_path,
            .video_codec = nullptr,
            .audio = audio_config,
            .video_params = input->streams[input_video]->codecpar,
            .video_time_base = input->streams[input_video]->time_base}, mux_reason)) {
        spdlog::error("[bitstream-copy] mux open failed for '{}': {}",
                      output_path, mux_reason);
        close_input();
        return std::nullopt;
    }
    const auto fail = [&]() -> std::optional<GopCopyResult> {
        close_input();
        return std::nullopt;
    };

    GopCopyResult result;
    bool selected_video_started = false;
    std::int64_t current_gop_start = AV_NOPTS_VALUE;
    bool selected_gop = false;
    bool finished_video = false;
    AVPacket* packet = av_packet_alloc();
    if (!packet) return fail();
    while (av_read_frame(input, packet) >= 0) {
        const bool is_video = packet->stream_index == input_video;
        const bool is_audio = packet->stream_index == input_audio;
        const auto pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        if (is_video && (packet->flags & AV_PKT_FLAG_KEY) != 0 &&
            pts != AV_NOPTS_VALUE) {
            current_gop_start = pts;
            selected_gop = !finished_video && current_gop_start >= selected_gop_start &&
                current_gop_start < selected_gop_end;
            if (finished_video || current_gop_start >= selected_gop_end) {
                finished_video = true;
                selected_gop = false;
            }
        }
        const bool emit_video = is_video && selected_gop && !finished_video;
        const bool emit_audio = is_audio && pts != AV_NOPTS_VALUE &&
            pts >= audio_start && pts < audio_end;
        if (emit_video || emit_audio) {
            const auto start = is_video ? selected_gop_start : audio_start;
            if (is_video && !selected_video_started) {
                if ((packet->flags & AV_PKT_FLAG_KEY) == 0) {
                    av_packet_unref(packet);
                    av_packet_free(&packet);
                    return fail();
                }
                selected_video_started = true;
            }
            if (packet->pts != AV_NOPTS_VALUE) packet->pts -= start;
            if (packet->dts != AV_NOPTS_VALUE) packet->dts -= start;
            bool submitted = false;
            if (is_video) {
                submitted = mux.submit_video(
                    *packet, input->streams[input_video]->time_base);
            } else {
                auto owned = chronon3d::media::OwnedAVPacket(av_packet_alloc());
                submitted = owned && av_packet_ref(owned.get(), packet) >= 0 &&
                    mux.submit_audio(chronon3d::media::EncodedPacket{
                        std::move(owned), input->streams[input_audio]->time_base, false});
            }
            if (!submitted) {
                av_packet_unref(packet);
                av_packet_free(&packet);
                return fail();
            }
            if (is_video) ++result.video_packets;
            else ++result.audio_packets;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if (!selected_video_started) return fail();
    if (!mux.finalize()) return fail();
    close_input();
    return result;
#endif
}

} // namespace chronon3d::cli
