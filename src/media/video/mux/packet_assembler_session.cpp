#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/audio_resampler.hpp>

#include "packet_assembler_hash_writer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <unordered_set>

extern "C" {
#include <libavutil/dict.h>
}

namespace chronon3d::media {
namespace {

constexpr int kMuxAvioBufferSize = 64 * 1024;

} // namespace

MuxSession::MuxSession() = default;

MuxSession::~MuxSession() {
    if (!format_) return;
    if (format_->pb && format_->oformat && !(format_->oformat->flags & AVFMT_NOFILE)) {
        if (writer_) {
            avio_flush(format_->pb);
            auto* buffer = format_->pb->buffer;
            avio_context_free(&format_->pb);
            av_free(buffer);
        } else {
            avio_closep(&format_->pb);
        }
    }
    writer_.reset();
    avformat_free_context(format_);
}

bool MuxSession::open(const MuxOpenConfig& config, std::string& reason) {
    const auto started = std::chrono::steady_clock::now();
    if (config.output_path.empty() || (!config.video_codec && !config.video_params)) {
        reason = "mux open requires output path and video codec parameters";
        return false;
    }
    output_path_ = config.output_path;
    output_checksum_.clear();
    checksum_enabled_ = config.checksum;
    fragmented_mp4_ = config.fragmented_mp4;
    checksum_used_reread_ = false;
    finalize_attempted_ = false;
    finalized_ = false;
    if (avformat_alloc_output_context2(&format_, nullptr, nullptr,
                                       config.output_path.c_str()) < 0 || !format_) {
        reason = "failed to allocate output format context";
        return false;
    }
    video_stream_ = avformat_new_stream(format_, nullptr);
    if (!video_stream_) {
        reason = "failed to create output video stream";
        return false;
    }
    const int copy_result = config.video_codec
        ? avcodec_parameters_from_context(video_stream_->codecpar, config.video_codec)
        : avcodec_parameters_copy(video_stream_->codecpar, config.video_params);
    if (copy_result < 0) {
        reason = "failed to copy output video codec parameters";
        return false;
    }
    video_stream_->time_base = config.video_codec
        ? config.video_codec->time_base
        : (config.video_time_base.num > 0 && config.video_time_base.den > 0
               ? config.video_time_base : AVRational{1, 1000});
    if (config.audio) {
        const auto& audio = *config.audio;
        if (!audio.params) {
            reason = "audio stream requires codec parameters";
            return false;
        }
        audio_stream_ = avformat_new_stream(format_, nullptr);
        if (!audio_stream_) {
            reason = "avformat_new_stream failed for audio";
            return false;
        }
        const bool transcode = audio.target_sample_rate > 0 &&
            audio.target_sample_rate != audio.params->sample_rate;
        if (transcode) {
            auto resampler = std::make_unique<AudioResampler>();
            std::string resample_reason;
            if (!resampler->open(audio.params, audio.target_sample_rate,
                                 audio.target_bitrate, resample_reason)) {
                reason = "audio resample failed: " + resample_reason;
                return false;
            }
            if (avcodec_parameters_copy(audio_stream_->codecpar,
                                        resampler->output_parameters()) < 0) {
                reason = "failed to copy resampled audio codec parameters";
                return false;
            }
            audio_stream_->time_base = resampler->output_time_base();
            audio_resampler_ = std::move(resampler);
        } else {
            if (avcodec_parameters_copy(audio_stream_->codecpar, audio.params) < 0) {
                reason = "avcodec_parameters_copy failed for audio";
                return false;
            }
            audio_stream_->time_base = audio.time_base;
        }
    }
    if (!write_header(config.output_path, reason)) return false;
    open_header_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return true;
}

bool MuxSession::write_header(const std::string& output_path,
                              std::string& reason) {
    if (!format_) {
        reason = "write_header called without a format context";
        return false;
    }
    if (!(format_->oformat->flags & AVFMT_NOFILE)) {
        writer_ = std::unique_ptr<MuxAvioHashWriter, void(*)(MuxAvioHashWriter*)>(
            new MuxAvioHashWriter(), delete_writer);
        if (!writer_->open(output_path, checksum_enabled_)) {
            reason = "failed to open output sink";
            writer_.reset();
            return false;
        }
        auto* buffer = static_cast<unsigned char*>(av_malloc(kMuxAvioBufferSize));
        if (!buffer) {
            reason = "failed to allocate mux AVIO buffer";
            writer_.reset();
            return false;
        }
        format_->pb = avio_alloc_context(buffer, kMuxAvioBufferSize, 1, writer_.get(),
                                         nullptr, +MuxAvioHashWriter::write_packet,
                                         &MuxAvioHashWriter::seek);
        if (!format_->pb) {
            av_free(buffer);
            reason = "failed to allocate checksum-aware AVIO context";
            writer_.reset();
            return false;
        }
        format_->pb->seekable = writer_->sink->seekable;
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    }
    // The append-oriented hash writer cannot support faststart's second
    // seek/rewrite pass. Callers that require faststart remux afterwards.
    AVDictionary* options = nullptr;
    if (std::filesystem::path(output_path).extension() == ".gif")
        av_dict_set(&options, "loop", "0", 0);
    // Fragmented MP4: every fragment (moof/mdat) is self-describing and final
    // the moment it is appended — no `mdat`-size backpatch, no second seek
    // pass, and the checksum computed by the append-only hash writer is the
    // file digest without any reread. Fragments start at keyframes
    // (frag_keyframe), so a packet-copy splice can still cut on fragment
    // boundaries. Only the ISO-BMFF muxers understand "movflags"; setting it
    // on another container would fail the header write, so guard by muxer.
    if (fragmented_mp4_ && format_->oformat) {
        const std::string_view muxer_name = format_->oformat->name ? format_->oformat->name : "";
        const bool iso_bmff = muxer_name == "mp4" || muxer_name == "mov" ||
            muxer_name == "3gp" || muxer_name == "ipod" || muxer_name == "psp" ||
            muxer_name == "f4v";
        if (iso_bmff)
            av_dict_set(&options, "movflags",
                        "frag_keyframe+empty_moov+default_base_moof", 0);
    }
    const int ret = avformat_write_header(format_, &options);
    av_dict_free(&options);
    if (ret < 0) {
        reason = "failed to write mux header";
        return false;
    }
    return true;
}

bool MuxSession::place_video_packet(
    AVPacket& packet, MuxVideoPlacement placement) const noexcept {
    if (placement != MuxVideoPlacement::ScheduledOrigin || video_origin_ == 0) {
        return true;
    }
    if (packet.pts != AV_NOPTS_VALUE) packet.pts += video_origin_;
    if (packet.dts != AV_NOPTS_VALUE) packet.dts += video_origin_;
    return packet.pts != AV_NOPTS_VALUE || packet.dts != AV_NOPTS_VALUE;
}

bool MuxSession::submit_video(EncodedPacket encoded,
                              MuxVideoPlacement placement) noexcept {
    if (!format_ || !video_stream_ || !encoded.packet) return false;
    const auto started = std::chrono::steady_clock::now();
    auto& packet = *encoded.packet;
    if (packet.duration <= 0) packet.duration = 1;
    av_packet_rescale_ts(&packet, encoded.time_base, video_stream_->time_base);
    packet.stream_index = video_stream_->index;
    if (!place_video_packet(packet, placement)) return false;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    packet_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::submit_video(AVPacket& packet, AVRational source_time_base,
                              MuxVideoPlacement placement) noexcept {
    if (!format_ || !video_stream_) return false;
    const auto started = std::chrono::steady_clock::now();
    if (packet.duration <= 0) packet.duration = 1;
    av_packet_rescale_ts(&packet, source_time_base, video_stream_->time_base);
    packet.stream_index = video_stream_->index;
    if (!place_video_packet(packet, placement)) return false;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    packet_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::write_audio_packet(AVPacket& packet,
                                    AVRational source_time_base) noexcept {
    if (!format_ || !audio_stream_) return false;
    const auto started = std::chrono::steady_clock::now();
    av_packet_rescale_ts(&packet, source_time_base, audio_stream_->time_base);
    packet.stream_index = audio_stream_->index;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    audio_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::submit_audio(EncodedPacket encoded) noexcept {
    if (!format_ || !audio_stream_ || !encoded.packet) return false;
    if (audio_resampler_) {
        std::vector<OwnedAVPacket> produced;
        if (!audio_resampler_->push(*encoded.packet, produced)) return false;
        for (auto& packet : produced) {
            if (!write_audio_packet(*packet,
                                    audio_resampler_->output_time_base())) {
                return false;
            }
        }
        return true;
    }
    return write_audio_packet(*encoded.packet, encoded.time_base);
}

bool MuxSession::finalize() noexcept {
    if (!format_) return false;
    if (finalize_attempted_) return finalized_;
    finalize_attempted_ = true;

    const auto started = std::chrono::steady_clock::now();
    bool audio_ok = true;
    if (audio_resampler_) {
        std::vector<OwnedAVPacket> produced;
        audio_ok = audio_resampler_->flush(produced);
        for (auto& packet : produced) {
            if (!audio_ok) break;
            audio_ok = write_audio_packet(*packet,
                                          audio_resampler_->output_time_base());
        }
    }
    const bool trailer_ok = av_write_trailer(format_) >= 0;
    trailer_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    bool checksum_ok = true;
    if (writer_) {
        if (format_->pb) {
            avio_flush(format_->pb);
            auto* buffer = format_->pb->buffer;
            avio_context_free(&format_->pb);
            av_free(buffer);
        }
        checksum_ok = writer_->close_and_checksum(output_path_, output_checksum_,
                                                   checksum_used_reread_);
    }
    finalized_ = trailer_ok && checksum_ok && audio_ok;
    return finalized_;
}

MuxHashMetrics MuxSession::hash_metrics() const noexcept {
    return writer_ ? writer_->hash_metrics() : MuxHashMetrics{};
}

MuxOutputSegmentWriter::~MuxOutputSegmentWriter() {
    if (finalized_) return;
    mirror_.reset();
    muxes_.clear();
    for (const auto& segment : segments_) {
        std::error_code ec;
        std::filesystem::remove(segment.output_path, ec);
    }
    if (!mirror_output_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(mirror_output_path_, ec);
    }
}

bool MuxOutputSegmentWriter::open(
    const AVCodecParameters* video_params,
    AVRational video_time_base,
    std::vector<MuxOutputSegment> segments,
    bool fragmented_mp4,
    std::string& reason,
    const std::string& mirror_output_path) {
    if (opened_ || finalize_attempted_) {
        reason = "segmented mux writer can only be opened once";
        return false;
    }
    if (!video_params || video_params->codec_type != AVMEDIA_TYPE_VIDEO ||
        video_time_base.num <= 0 || video_time_base.den <= 0 || segments.empty()) {
        reason = "segmented mux writer requires video parameters, time base, and outputs";
        return false;
    }

    std::int64_t expected_start = 0;
    std::unordered_set<std::string> output_paths;
    for (const auto& segment : segments) {
        if (segment.output_path.empty() || segment.start_pts != expected_start ||
            segment.end_pts <= segment.start_pts ||
            !output_paths.emplace(segment.output_path).second) {
            reason = "segments must have unique output paths and contiguous positive PTS ranges starting at zero";
            return false;
        }
        expected_start = segment.end_pts;
    }
    // The mirror shares the output namespace: a mirror path colliding with a
    // segment would have two muxers writing the same file.
    if (!mirror_output_path.empty() &&
        output_paths.find(mirror_output_path) != output_paths.end()) {
        reason = "mirror output path collides with a segment output path";
        return false;
    }

    segments_ = std::move(segments);
    video_time_base_ = video_time_base;
    mirror_output_path_ = mirror_output_path;
    muxes_.reserve(segments_.size());
    packet_counts_.assign(segments_.size(), 0);
    last_packet_pts_.assign(segments_.size(), AV_NOPTS_VALUE);
    for (const auto& segment : segments_) {
        auto mux = std::make_unique<MuxSession>();
        if (!mux->open(MuxOpenConfig{
                .output_path = segment.output_path,
                .video_params = video_params,
                .video_time_base = video_time_base,
                .fragmented_mp4 = fragmented_mp4}, reason)) {
            muxes_.clear();
            for (const auto& path : segments_) {
                std::error_code ec;
                std::filesystem::remove(path.output_path, ec);
            }
            segments_.clear();
            packet_counts_.clear();
            return false;
        }
        muxes_.push_back(std::move(mux));
    }
    if (!mirror_output_path_.empty()) {
        mirror_ = std::make_unique<MuxSession>();
        if (!mirror_->open(MuxOpenConfig{
                .output_path = mirror_output_path_,
                .video_params = video_params,
                .video_time_base = video_time_base,
                .fragmented_mp4 = fragmented_mp4}, reason)) {
            mirror_.reset();
            muxes_.clear();
            for (const auto& path : segments_) {
                std::error_code ec;
                std::filesystem::remove(path.output_path, ec);
            }
            segments_.clear();
            packet_counts_.clear();
            std::error_code ec;
            std::filesystem::remove(mirror_output_path_, ec);
            mirror_output_path_.clear();
            return false;
        }
    }
    opened_ = true;
    return true;
}

bool MuxOutputSegmentWriter::submit_video(
    AVPacket& packet, AVRational source_time_base) noexcept {
    if (!opened_ || finalize_attempted_ || failed_ ||
        source_time_base.num != video_time_base_.num ||
        source_time_base.den != video_time_base_.den ||
        packet.pts == AV_NOPTS_VALUE || packet.dts == AV_NOPTS_VALUE) {
        failed_ = true;
        return false;
    }

    const auto after = std::upper_bound(
        segments_.begin(), segments_.end(), packet.pts,
        [](std::int64_t pts, const MuxOutputSegment& segment) {
            return pts < segment.start_pts;
        });
    if (after == segments_.begin()) {
        failed_ = true;
        return false;
    }
    const auto index = static_cast<std::size_t>(
        std::distance(segments_.begin(), after) - 1);
    const auto& segment = segments_[index];
    if (packet.pts < segment.start_pts || packet.pts >= segment.end_pts) {
        failed_ = true;
        return false;
    }
    if ((packet_counts_[index] > 0 &&
         packet.pts <= last_packet_pts_[index]) ||
        (packet_counts_[index] == 0 &&
         (packet.pts != segment.start_pts ||
          (packet.flags & AV_PKT_FLAG_KEY) == 0))) {
        failed_ = true;
        return false;
    }

    AVPacket* shifted = av_packet_clone(&packet);
    if (!shifted) {
        failed_ = true;
        return false;
    }
    shifted->pts -= segment.start_pts;
    shifted->dts -= segment.start_pts;
    const bool submitted = muxes_[index]->submit_video(*shifted, source_time_base);
    av_packet_free(&shifted);
    if (!submitted) {
        failed_ = true;
        return false;
    }
    // Whole-timeline mirror (the batch's fused clip): the SAME packet at its
    // global PTS. A second clone is deliberate — MuxSession::submit_video may
    // place (mutate) the packet it receives, so the mirror must never see the
    // segment's shifted copy nor hand the caller back a moved packet.
    if (mirror_) {
        AVPacket* global = av_packet_clone(&packet);
        if (!global) {
            failed_ = true;
            return false;
        }
        const bool mirror_submitted =
            mirror_->submit_video(*global, source_time_base);
        av_packet_free(&global);
        if (!mirror_submitted) {
            failed_ = true;
            return false;
        }
        ++mirror_packets_;
    }
    ++packet_counts_[index];
    last_packet_pts_[index] = packet.pts;
    return true;
}

bool MuxOutputSegmentWriter::finalize() noexcept {
    if (!opened_) return false;
    if (finalize_attempted_) return finalized_;
    finalize_attempted_ = true;
    bool ok = !failed_;
    std::uint64_t total_packets = 0;
    for (std::size_t i = 0; i < muxes_.size(); ++i) {
        const auto expected = segments_[i].expected_packet_count != 0
            ? segments_[i].expected_packet_count
            : static_cast<std::uint64_t>(
                  segments_[i].end_pts - segments_[i].start_pts);
        if (packet_counts_[i] != expected) ok = false;
        total_packets += packet_counts_[i];
        if (!muxes_[i]->finalize()) ok = false;
    }
    // The mirror is the whole timeline: it must have received exactly the
    // packets the segments received, no more and no fewer.
    if (mirror_) {
        if (mirror_packets_ != total_packets) ok = false;
        if (!mirror_->finalize()) ok = false;
    }
    finalized_ = ok;
    return finalized_;
}

} // namespace chronon3d::media
