#include "packet_assembler.hpp"

namespace chronon3d::cli {

#if 0
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

#endif

} // namespace chronon3d::cli
