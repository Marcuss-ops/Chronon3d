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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
namespace {
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
    const AVCodecParameters& output,
    bool random_access_safe) noexcept {
    BitstreamCompatibility result;
    result.codec_match = source.codec_id != AV_CODEC_ID_NONE &&
        source.codec_id == output.codec_id;
    result.profile_match = source.profile >= 0 &&
        source.profile == output.profile;
    result.level_compatible = source.level >= 0 &&
        output.level >= 0 && source.level == output.level;
    result.dimensions_match = source.width > 0 && source.height > 0 &&
        source.width == output.width && source.height == output.height;
    result.pixel_format_match = source.format >= 0 &&
        source.format == output.format;
    result.parameter_sets_compatible = extradata_equal(source, output);
    result.color_params_match = source.color_range == output.color_range &&
        source.color_space == output.color_space &&
        source.color_primaries == output.color_primaries &&
        source.color_trc == output.color_trc;
    result.random_access_safe = random_access_safe;
    return result;
}
#endif

std::optional<GopSourceAnalysis> inspect_gop_source(
    const std::string& path,
    std::string_view requested_codec,
    double edit_start_seconds,
    double edit_end_seconds) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)path;
    (void)requested_codec;
    (void)edit_start_seconds;
    (void)edit_end_seconds;
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
    const bool codec_parameters_match = codec_matches(
        source_params->codec_id, requested_codec);
    // No output encoder parameters are available during source inspection.
    // Keep this aggregate only for diagnostics; plan_gop remains fail-closed
    // until the caller supplies an actual output configuration.
    const BitstreamCompatibility unavailable_compatibility{};
    const auto time_base = format->streams[video_stream_index]->time_base;
    const double time_base_seconds = av_q2d(time_base);
    if (!(time_base_seconds > 0.0)) {
        close_format();
        return std::nullopt;
    }
    const auto edit_start = static_cast<std::int64_t>(std::llround(
        edit_start_seconds / time_base_seconds));
    const auto edit_end = static_cast<std::int64_t>(std::llround(
        edit_end_seconds / time_base_seconds));

    std::vector<std::vector<CompressedPacketInfo>> groups;
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        close_format();
        return std::nullopt;
    }
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            const CompressedPacketInfo info{
                .pts = packet->pts == AV_NOPTS_VALUE ? packet->dts : packet->pts,
                .dts = packet->dts == AV_NOPTS_VALUE ? packet->pts : packet->dts,
                .keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0,
                // A GOP starts at a keyframe. Packets before the first
                // keyframe are deliberately left out of copy candidates.
                .references_prior_gop = false,
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
    for (const auto& group : groups) {
        if (group.empty()) continue;
        const auto& first = group.front();
        const auto& last = group.back();
        const bool intersects = edit_end > edit_start &&
            last.pts >= edit_start && first.pts < edit_end;
        auto analysis = analyze_gop(group, codec_parameters_match, intersects);
        analysis.compatibility = unavailable_compatibility;
        auto plan = plan_gop(analysis);
        plan.ordinal = result.plans.size();
        result.plans.push_back(plan);
    }
    close_format();
    if (result.plans.empty()) return std::nullopt;
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

    AVFormatContext* output = nullptr;
    if (avformat_alloc_output_context2(&output, nullptr, nullptr, output_path.c_str()) < 0 || !output) {
        close_input();
        return std::nullopt;
    }
    const auto fail = [&]() -> std::optional<GopCopyResult> {
        if (output) {
            if (output->pb && output->oformat &&
                !(output->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output->pb);
            }
            avformat_free_context(output);
            output = nullptr;
        }
        close_input();
        return std::nullopt;
    };

    AVStream* output_video = avformat_new_stream(output, nullptr);
    if (!output_video || avcodec_parameters_copy(
            output_video->codecpar, input->streams[input_video]->codecpar) < 0) {
        return fail();
    }
    output_video->time_base = input->streams[input_video]->time_base;
    AVStream* output_audio = nullptr;
    if (input_audio >= 0) {
        output_audio = avformat_new_stream(output, nullptr);
        if (!output_audio || avcodec_parameters_copy(
                output_audio->codecpar, input->streams[input_audio]->codecpar) < 0) {
            return fail();
        }
        output_audio->time_base = input->streams[input_audio]->time_base;
    }

    if (!(output->oformat->flags & AVFMT_NOFILE) &&
        avio_open(&output->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
        return fail();
    }
    if (avformat_write_header(output, nullptr) < 0) return fail();
    chronon3d::media::PacketAssembler assembler(output, output_video, output_audio);

    const auto to_source_pts = [](double seconds, AVRational time_base) {
        return static_cast<std::int64_t>(std::llround(seconds / av_q2d(time_base)));
    };
    const auto video_start = to_source_pts(start_seconds, input->streams[input_video]->time_base);
    const auto video_end = to_source_pts(end_seconds, input->streams[input_video]->time_base);
    const auto audio_start = input_audio >= 0
        ? to_source_pts(start_seconds, input->streams[input_audio]->time_base) : 0;
    const auto audio_end = input_audio >= 0
        ? to_source_pts(end_seconds, input->streams[input_audio]->time_base) : 0;

    GopCopyResult result;
    bool selected_video_started = false;
    AVPacket* packet = av_packet_alloc();
    if (!packet) return fail();
    while (av_read_frame(input, packet) >= 0) {
        const bool is_video = packet->stream_index == input_video;
        const bool is_audio = packet->stream_index == input_audio;
        const auto pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        const auto start = is_video ? video_start : audio_start;
        const auto end = is_video ? video_end : audio_end;
        if ((is_video || is_audio) && pts != AV_NOPTS_VALUE && pts >= start && pts < end) {
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
            const bool submitted = is_video
                ? assembler.submit_copied_video(*packet, input->streams[input_video]->time_base)
                : assembler.submit_audio(*packet, input->streams[input_audio]->time_base);
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
    if (!assembler.finalize()) return fail();
    if (output->pb && output->oformat &&
        !(output->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output->pb);
    }
    avformat_free_context(output);
    output = nullptr;
    close_input();
    return result;
#endif
}

} // namespace chronon3d::cli
