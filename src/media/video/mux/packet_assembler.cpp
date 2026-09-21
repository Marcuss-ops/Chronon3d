#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/audio_resampler.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include "packet_assembler_hash_writer.hpp"
#include "packet_assembler_segments.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::media {
namespace {

constexpr int kMuxAvioBufferSize = 64 * 1024;
struct InputFile {
    InputFile() = default;
    AVFormatContext* format{nullptr};
    int video_index{-1};
    int audio_index{-1};

    ~InputFile() {
        if (format) avformat_close_input(&format);
    }
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;
};

bool find_streams(InputFile& input, std::string& reason) {
    if (avformat_find_stream_info(input.format, nullptr) < 0) {
        reason = "failed to read stream information";
        return false;
    }
    for (unsigned i = 0; i < input.format->nb_streams; ++i) {
        const auto type = input.format->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && input.video_index < 0) input.video_index = static_cast<int>(i);
        if (type == AVMEDIA_TYPE_AUDIO && input.audio_index < 0) input.audio_index = static_cast<int>(i);
    }
    if (input.video_index < 0) { reason = "segment has no video stream"; return false; }
    return true;
}

} // namespace

SegmentAssemblyResult assemble_segments(const SegmentAssemblyRequest& request) {
    SegmentAssemblyResult result;
    if (request.input_paths.empty() || request.output_path.empty()) {
        result.reason = "segment assembly requires inputs and output path";
        return result;
    }

    std::vector<std::unique_ptr<InputFile>> inputs;
    inputs.reserve(request.input_paths.size());
    for (const auto& path : request.input_paths) {
        auto input = std::make_unique<InputFile>();
        if (avformat_open_input(&input->format, path.c_str(), nullptr, nullptr) < 0) {
            result.reason = "failed to open segment: " + path;
            return result;
        }
        if (!find_streams(*input, result.reason)) return result;
        inputs.push_back(std::move(input));
    }

    const auto* reference_video = inputs.front()->format->streams[inputs.front()->video_index];
    const auto* reference_audio = inputs.front()->audio_index >= 0
        ? inputs.front()->format->streams[inputs.front()->audio_index] : nullptr;
    for (std::size_t i = 1; i < inputs.size(); ++i) {
        const auto* video = inputs[i]->format->streams[inputs[i]->video_index];
        if (!detail::compatible_codec_parameters(reference_video->codecpar, video->codecpar, result.reason)) return result;
        if (video->time_base.num != reference_video->time_base.num ||
            video->time_base.den != reference_video->time_base.den) {
            result.reason = "video time base mismatch";
            return result;
        }
        const auto* audio = inputs[i]->audio_index >= 0
            ? inputs[i]->format->streams[inputs[i]->audio_index] : nullptr;
        if ((reference_audio == nullptr) != (audio == nullptr)) {
            result.reason = "audio stream presence mismatch";
            return result;
        }
        if (reference_audio && !detail::compatible_codec_parameters(reference_audio->codecpar,
                                                                     audio->codecpar, result.reason)) return result;
        if (reference_audio && (audio->time_base.num != reference_audio->time_base.num ||
                                audio->time_base.den != reference_audio->time_base.den)) {
            result.reason = "audio time base mismatch";
            return result;
        }
    }

    AVCodecContext* video_codec = avcodec_alloc_context3(nullptr);
    if (!video_codec || avcodec_parameters_to_context(video_codec, reference_video->codecpar) < 0) {
        avcodec_free_context(&video_codec);
        result.reason = "failed to create video mux context";
        return result;
    }
    video_codec->time_base = reference_video->time_base;
    struct CodecContextDeleter { void operator()(AVCodecContext* value) const { if (value) avcodec_free_context(&value); } };
    std::unique_ptr<AVCodecContext, CodecContextDeleter> video_guard(video_codec);

    struct CodecParametersDeleter { void operator()(AVCodecParameters* value) const { if (value) avcodec_parameters_free(&value); } };
    std::unique_ptr<AVCodecParameters, CodecParametersDeleter> audio_params(nullptr);
    std::optional<AudioStreamConfig> audio_config;
    if (reference_audio) {
        auto* params = avcodec_parameters_alloc();
        if (!params || avcodec_parameters_copy(params, reference_audio->codecpar) < 0) {
            if (params) avcodec_parameters_free(&params);
            result.reason = "failed to create audio mux parameters";
            return result;
        }
        audio_params.reset(params);
        audio_config = AudioStreamConfig{audio_params.get(), reference_audio->time_base};
    }

    std::error_code ec;
    if (const auto parent = std::filesystem::path(request.output_path).parent_path();
        !parent.empty()) std::filesystem::create_directories(parent, ec);

    MuxSession mux;
    // This is the caller whose ONLY digest is the mux's: SegmentAssemblyResult
    // publishes output_checksum as the concatenated artifact's content address,
    // so it requests the checksum (the exporter lane deliberately does not —
    // see MuxOpenConfig::checksum).
    if (!mux.open(MuxOpenConfig{request.output_path, video_codec, audio_config,
                               /*video_params=*/nullptr, /*video_time_base=*/AVRational{0, 1},
                               /*checksum=*/true},
                  result.reason)) {
        return result;
    }

    std::int64_t video_offset = 0;
    std::int64_t audio_offset = 0;
    for (const auto& input : inputs) {
        auto* video_stream = input->format->streams[input->video_index];
        auto* audio_stream = input->audio_index >= 0 ? input->format->streams[input->audio_index] : nullptr;
        std::int64_t first_video_dts = AV_NOPTS_VALUE;
        std::int64_t first_audio_dts = AV_NOPTS_VALUE;
        std::int64_t segment_last_video_timestamp = AV_NOPTS_VALUE;
        std::int64_t segment_last_audio_timestamp = AV_NOPTS_VALUE;
        bool saw_video = false;
        bool saw_audio = false;
        AVPacket* raw = av_packet_alloc();
        if (!raw) { result.reason = "failed to allocate input packet"; return result; }
        while (av_read_frame(input->format, raw) >= 0) {
            if (raw->stream_index == input->video_index) {
                if (!saw_video) {
                    saw_video = true;
                    first_video_dts = raw->dts != AV_NOPTS_VALUE ? raw->dts : raw->pts;
                    if (&input != &inputs.front() && !(raw->flags & AV_PKT_FLAG_KEY)) {
                        av_packet_free(&raw);
                        result.reason = "segment does not begin at a video keyframe";
                        return result;
                    }
                }
                AVPacket* copy = av_packet_alloc();
                if (!copy) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to allocate video packet copy";
                    return result;
                }
                OwnedAVPacket owned(copy);
                if (av_packet_ref(copy, raw) < 0) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to copy video packet";
                    return result;
                }
                copy->pts = detail::normalize_timestamp(copy->pts, first_video_dts, video_offset);
                copy->dts = detail::normalize_timestamp(copy->dts, first_video_dts, video_offset);
                copy->stream_index = 0;
                const auto packet_duration = std::max<std::int64_t>(1, copy->duration);
                const auto packet_end = std::max(copy->dts, copy->pts);
                if (packet_end != AV_NOPTS_VALUE) {
                    segment_last_video_timestamp = std::max(
                        segment_last_video_timestamp, packet_end + packet_duration);
                }
                const bool keyframe = (copy->flags & AV_PKT_FLAG_KEY) != 0;
                if (!mux.submit_video(EncodedPacket{std::move(owned), video_stream->time_base,
                                                     keyframe})) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to write video packet";
                    return result;
                }
            } else if (audio_stream && raw->stream_index == input->audio_index) {
                if (!saw_audio) {
                    saw_audio = true;
                    first_audio_dts = raw->dts != AV_NOPTS_VALUE ? raw->dts : raw->pts;
                }
                AVPacket* copy = av_packet_alloc();
                if (!copy) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to allocate audio packet copy";
                    return result;
                }
                OwnedAVPacket owned(copy);
                if (av_packet_ref(copy, raw) < 0) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to copy audio packet";
                    return result;
                }
                copy->pts = detail::normalize_timestamp(copy->pts, first_audio_dts, audio_offset);
                copy->dts = detail::normalize_timestamp(copy->dts, first_audio_dts, audio_offset);
                copy->stream_index = 1;
                const auto packet_duration = std::max<std::int64_t>(1, copy->duration);
                const auto packet_end = std::max(copy->dts, copy->pts);
                if (packet_end != AV_NOPTS_VALUE) {
                    segment_last_audio_timestamp = std::max(
                        segment_last_audio_timestamp, packet_end + packet_duration);
                }
                if (!mux.submit_audio(EncodedPacket{std::move(owned), audio_stream->time_base, false})) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to write audio packet";
                    return result;
                }
            }
            av_packet_unref(raw);
        }
        av_packet_free(&raw);
        const auto video_duration = detail::stream_duration(video_stream);
        if (segment_last_video_timestamp != AV_NOPTS_VALUE) {
            video_offset = segment_last_video_timestamp;
        } else if (video_duration > 0) {
            video_offset += video_duration;
        }
        if (audio_stream) {
            const auto audio_duration = detail::stream_duration(audio_stream);
            if (segment_last_audio_timestamp != AV_NOPTS_VALUE) {
                audio_offset = segment_last_audio_timestamp;
            } else if (audio_duration > 0) {
                audio_offset += audio_duration;
            }
        }
    }
    if (!mux.finalize()) {
        result.reason = "failed to finalize assembled output";
        return result;
    }
    result.output_checksum = std::string(mux.output_checksum());
    result.checksum_used_reread = mux.checksum_used_reread();
    result.success = true;
    return result;
}

bool PacketAssembler::submit(AVPacket& packet, AVRational source_time_base,
                             AVStream* target, bool default_duration) const noexcept {
    if (!format_ || !target) return false;
    if (default_duration && packet.duration <= 0) packet.duration = 1;
    av_packet_rescale_ts(&packet, source_time_base, target->time_base);
    packet.stream_index = target->index;
    return av_interleaved_write_frame(format_, &packet) >= 0;
}

bool PacketAssembler::submit_video(AVPacket& packet,
                                   AVRational source_time_base) const noexcept {
    return submit(packet, source_time_base, video_stream_, true);
}

bool PacketAssembler::submit_copied_video(
    AVPacket& packet, AVRational source_time_base) const noexcept {
    return submit(packet, source_time_base, video_stream_, true);
}

bool PacketAssembler::submit_audio(AVPacket& packet,
                                   AVRational source_time_base) const noexcept {
    return submit(packet, source_time_base, audio_stream_, false);
}

bool PacketAssembler::finalize() const noexcept {
    return format_ && av_write_trailer(format_) >= 0;
}

} // namespace chronon3d::media
