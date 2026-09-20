// SPDX-License-Identifier: MIT
//
// gop_smart_copy.cpp — GOP source analysis, splice certification and packet
// copy.
//
// Split (P2 structure pass): the byte-level NAL/Annex-B readers moved to
// gop_smart_copy_bitstream.{hpp,cpp} (shared detail seam) and the work-unit
// submission functions moved to gop_smart_copy_submit.cpp. This TU owns the
// demuxing analysis, the bitstream-compatibility comparison and the whole-clip
// packet copy.

#include "gop_smart_copy.hpp"

#include "encoder_bitstream_contract.hpp"
#include "gop_smart_copy_bitstream.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include <chronon3d/media/video/packet_assembler.hpp>
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>

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

bool SourceMuxDeclaration::encoder_reproduces(
    const BitstreamTargetContract& encoder_contract) const noexcept {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)encoder_contract;
    return false;
#else
    // The certification itself lives in the one authority
    // (encoder_reproduces_declared_stream): this method only restates the
    // declaration as a contract, so the container owner and the encoder judge
    // a splice with exactly the same code.
    BitstreamTargetContract declared{};
    declared.producer = BitstreamTargetProducer::Remux;
    declared.codec = static_cast<BitstreamCodecId>(video_codec_id);
    declared.profile = video_profile;
    declared.level = video_level;
    declared.pixel_format = static_cast<BitstreamPixelFormat>(video_pixel_format);
    declared.width = static_cast<std::uint32_t>(video_width);
    declared.height = static_cast<std::uint32_t>(video_height);
    declared.parameter_sets = video_extradata;
    declared.color_range = color_range;
    declared.color_space = color_space;
    declared.color_primaries = color_primaries;
    declared.color_trc = color_trc;
    return encoder_reproduces_declared_stream(declared, encoder_contract);
#endif
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
std::optional<SourceMuxDeclaration> open_source_declared_mux(
    const std::string& source_path, const std::string& output_path,
    chronon3d::media::MuxSession& mux, bool include_audio) {
    if (source_path.empty() || output_path.empty()) return std::nullopt;
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, source_path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return std::nullopt;
    }
    int video_index = -1;
    int audio_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const auto type = format->streams[index]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && video_index < 0) {
            video_index = static_cast<int>(index);
        } else if (type == AVMEDIA_TYPE_AUDIO && audio_index < 0) {
            audio_index = static_cast<int>(index);
        }
    }
    if (video_index < 0) {
        avformat_close_input(&format);
        return std::nullopt;
    }

    const auto* video_params = format->streams[video_index]->codecpar;
    SourceMuxDeclaration declaration;
    declaration.source_video_stream = video_index;
    declaration.video_time_base = format->streams[video_index]->time_base;
    declaration.video_codec_id = video_params->codec_id;
    declaration.video_profile = video_params->profile;
    declaration.video_level = video_params->level;
    declaration.video_pixel_format = video_params->format;
    declaration.video_width = video_params->width;
    declaration.video_height = video_params->height;
    declaration.color_range = video_params->color_range;
    declaration.color_space = video_params->color_space;
    declaration.color_primaries = video_params->color_primaries;
    declaration.color_trc = video_params->color_trc;
    if (video_params->extradata && video_params->extradata_size > 0) {
        declaration.video_extradata.assign(
            video_params->extradata,
            video_params->extradata + video_params->extradata_size);
    }

    std::optional<chronon3d::media::AudioStreamConfig> audio_config;
    if (include_audio && audio_index >= 0) {
        audio_config = chronon3d::media::AudioStreamConfig{
            .params = format->streams[audio_index]->codecpar,
            .time_base = format->streams[audio_index]->time_base,
            .target_sample_rate = 0};
        declaration.source_audio_stream = audio_index;
        declaration.audio_declared = true;
    }

    std::string reason;
    const bool opened = mux.open(chronon3d::media::MuxOpenConfig{
        // The container's video stream IS the source stream. A copied packet is
        // therefore correct by construction; the encoder must prove it
        // reproduces this declaration
        // (SourceMuxDeclaration::encoder_reproduces).
        .output_path = output_path,
        .video_codec = nullptr,
        .audio = audio_config,
        .video_params = video_params,
        .video_time_base = declaration.video_time_base}, reason);
    avformat_close_input(&format);
    if (!opened) {
        spdlog::error("[gop-direct-mux] source-declared mux open failed: {}",
                      reason);
        return std::nullopt;
    }
    return declaration;
}
#endif

std::optional<BitstreamTargetContract> make_remux_target_contract(
    const std::string& source_path) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)source_path;
    return std::nullopt;
#else
    if (source_path.empty()) return std::nullopt;
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, source_path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return std::nullopt;
    }
    std::optional<BitstreamTargetContract> target;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const auto* params = format->streams[index]->codecpar;
        if (params->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        BitstreamTargetContract contract;
        // The output stream IS the source stream on this lane: a demuxer will
        // report exactly these parameters. `producer = Remux` records that no
        // encoder participates, and `encoder_materialized` stays false because
        // no encoder produced them.
        contract.producer = BitstreamTargetProducer::Remux;
        contract.encoder_materialized = false;
        contract.codec = params->codec_id;
        contract.profile = params->profile;
        contract.level = params->level;
        contract.width = static_cast<std::uint32_t>(params->width);
        contract.height = static_cast<std::uint32_t>(params->height);
        contract.pixel_format = static_cast<BitstreamPixelFormat>(params->format);
        if (params->extradata && params->extradata_size > 0) {
            contract.parameter_sets.assign(
                params->extradata, params->extradata + params->extradata_size);
        }
        contract.color_range = params->color_range;
        contract.color_space = params->color_space;
        contract.color_primaries = params->color_primaries;
        contract.color_trc = params->color_trc;
        target = std::move(contract);
        break;
    }
    avformat_close_input(&format);
    return target;
#endif
}

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
            const bool random_access = detail::packet_is_certified_random_access(
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

media::GopExecutionPlan build_gop_plan_from_analysis(
    const GopSourceAnalysis& analysis, bool splice_safe) {
    media::GopExecutionPlan plan;
    plan.total_gops = analysis.plans.size();
    plan.copy_gops = analysis.copy_count;
    plan.reencode_gops = analysis.reencode_count;
    plan.all_copy = analysis.all_copy_eligible;
    plan.hybrid = analysis.is_hybrid();
    plan.safe_to_splice = splice_safe;
    plan.source_time_base_num = analysis.source_time_base_num;
    plan.source_time_base_den = analysis.source_time_base_den;
    plan.trim_only = analysis.trim_only;
    plan.work_units.reserve(analysis.plans.size());
    for (std::size_t index = 0; index < analysis.plans.size(); ++index) {
        const auto& gop = analysis.plans[index];
        const media::TimestampRange source_packets{gop.first_dts, gop.last_dts,
                                                  true};
        const media::TimestampRange decode_range{gop.decode_first_dts,
                                                 gop.last_dts, true};
        const media::TimestampRange render_range{gop.first_pts, gop.last_pts,
                                                 true};
        const bool copy_packets = gop.copy_packets();
        plan.work_units.push_back(media::GopWorkUnit{
            // The executor requires contiguous ordinals, so this mapping is
            // the authority for numbering, not the analysis' own counter.
            .ordinal = index,
            .first_pts = gop.first_pts,
            .last_pts = gop.last_pts,
            .first_dts = gop.first_dts,
            .last_dts = gop.last_dts,
            .kind = copy_packets ? media::GopWorkKind::CopyPackets
                                 : media::GopWorkKind::Reencode,
            .source_packets = source_packets,
            .decode_range = copy_packets ? media::TimestampRange{}
                                         : decode_range,
            .render_range = copy_packets ? media::TimestampRange{}
                                         : render_range,
            .encode_range = copy_packets ? media::TimestampRange{}
                                         : render_range,
            .force_idr_at_start = !copy_packets,
            .closes_dependencies_at_end = gop.compatibility.random_access_safe,
            .compatibility = gop.compatibility});
    }
    return plan;
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
