#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chrono>

namespace chronon3d::media {

MuxSession::~MuxSession() {
    if (!format_) return;
    if (format_->pb && format_->oformat && !(format_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&format_->pb);
    }
    avformat_free_context(format_);
}

bool MuxSession::open(const std::string& output_path,
                      const AVCodecContext& codec, std::string& reason) {
    const auto started = std::chrono::steady_clock::now();
    if (avformat_alloc_output_context2(&format_, nullptr, nullptr,
                                       output_path.c_str()) < 0 || !format_) {
        reason = "failed to allocate output format context";
        return false;
    }
    video_stream_ = avformat_new_stream(format_, nullptr);
    if (!video_stream_ || avcodec_parameters_from_context(video_stream_->codecpar, &codec) < 0) {
        reason = "failed to create output video stream";
        return false;
    }
    video_stream_->time_base = codec.time_base;
    return write_header(output_path, reason) ?
        (open_header_ms_ = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count(), true)
        : false;
}

bool MuxSession::open_with_audio(const std::string& output_path,
                                 const AVCodecContext& codec,
                                 const AudioStreamConfig& audio,
                                 std::string& reason) {
    const auto started = std::chrono::steady_clock::now();
    if (avformat_alloc_output_context2(&format_, nullptr, nullptr,
                                       output_path.c_str()) < 0 || !format_) {
        reason = "failed to allocate output format context";
        return false;
    }
    video_stream_ = avformat_new_stream(format_, nullptr);
    if (!video_stream_ || avcodec_parameters_from_context(video_stream_->codecpar, &codec) < 0) {
        reason = "failed to create output video stream";
        return false;
    }
    video_stream_->time_base = codec.time_base;
    // Register the audio stream BEFORE write_header so the muxer sees the
    // complete stream list and can compute interleaving correctly.
    if (!add_audio_stream(audio, reason)) {
        return false;
    }
    if (!write_header(output_path, reason)) {
        return false;
    }
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
    if (!(format_->oformat->flags & AVFMT_NOFILE) &&
        avio_open(&format_->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
        reason = "failed to open output";
        return false;
    }
    if (avformat_write_header(format_, nullptr) < 0) {
        reason = "failed to write mux header";
        return false;
    }
    return true;
}

bool MuxSession::add_audio_stream(const AudioStreamConfig& audio,
                                   std::string& reason) {
    if (!format_) {
        reason = "audio stream cannot be added before open()";
        return false;
    }
    if (!audio.params) {
        reason = "audio stream requires codec parameters";
        return false;
    }
    if (audio_stream_) {
        reason = "audio stream already added";
        return false;
    }
    audio_stream_ = avformat_new_stream(format_, nullptr);
    if (!audio_stream_) {
        reason = "avformat_new_stream failed for audio";
        return false;
    }
    if (avcodec_parameters_copy(audio_stream_->codecpar, audio.params) < 0) {
        reason = "avcodec_parameters_copy failed for audio";
        audio_stream_ = nullptr;
        return false;
    }
    audio_stream_->time_base = audio.time_base;
    return true;
}

bool MuxSession::submit_video(EncodedPacket encoded) noexcept {
    if (!format_ || !video_stream_ || !encoded.packet) return false;
    const auto started = std::chrono::steady_clock::now();
    auto& packet = *encoded.packet;
    av_packet_rescale_ts(&packet, encoded.time_base, video_stream_->time_base);
    packet.stream_index = video_stream_->index;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    packet_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::submit_audio(EncodedPacket encoded) noexcept {
    if (!format_ || !audio_stream_ || !encoded.packet) return false;
    const auto started = std::chrono::steady_clock::now();
    auto& packet = *encoded.packet;
    av_packet_rescale_ts(&packet, encoded.time_base, audio_stream_->time_base);
    packet.stream_index = audio_stream_->index;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    audio_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::finalize() noexcept {
    if (!format_) return false;
    const auto started = std::chrono::steady_clock::now();
    const bool ok = av_write_trailer(format_) >= 0;
    trailer_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
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
