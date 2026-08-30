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
    if (!(format_->oformat->flags & AVFMT_NOFILE) &&
        avio_open(&format_->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
        reason = "failed to open output";
        return false;
    }
    if (avformat_write_header(format_, nullptr) < 0) {
        reason = "failed to write mux header";
        return false;
    }
    open_header_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return true;
}

bool MuxSession::submit(EncodedPacket encoded) noexcept {
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
