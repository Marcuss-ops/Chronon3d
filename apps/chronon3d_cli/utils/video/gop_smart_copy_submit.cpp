// SPDX-License-Identifier: MIT
//
// gop_smart_copy_submit.cpp — work-unit packet submission.
//
// Split out of gop_smart_copy.cpp (P2 structure pass): the functions that
// submit ONE planned work unit's copied video packets and the source audio
// track straight into a shared mux. They consume the schedule produced by the
// source-analysis TU and never re-derive it from seconds or frames.

#include "gop_smart_copy.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include <chronon3d/media/video/packet_assembler.hpp>
extern "C" {
#include <libavformat/avformat.h>
}
#endif

#include <algorithm>
#include <cstdint>
#include <utility>

namespace chronon3d::cli {

std::optional<GopCopyResult> submit_gop_source_packets(
    const std::string& source_path, const chronon3d::media::GopWorkUnit& unit,
    chronon3d::media::MuxSession& mux, std::int64_t window_origin) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)source_path;
    (void)unit;
    (void)mux;
    (void)window_origin;
    return std::nullopt;
#else
    if (source_path.empty()) return std::nullopt;
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, source_path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    const auto close_format = [&]() { avformat_close_input(&format); };
    if (avformat_find_stream_info(format, nullptr) < 0) {
        close_format();
        return std::nullopt;
    }
    int video_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        if (format->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = static_cast<int>(index);
            break;
        }
    }
    if (video_index < 0) {
        close_format();
        return std::nullopt;
    }
    const auto time_base = format->streams[video_index]->time_base;

    // The unit's packet envelope, in the SOURCE time base. DTS is the order the
    // container stores and the muxer re-emits, so it is the selection key.
    // Nothing here converts to seconds or frames: the schedule already speaks
    // the source's own timestamps.
    std::int64_t first = unit.source_packets.present ? unit.source_packets.first
                                                     : unit.first_dts;
    std::int64_t last = unit.source_packets.present ? unit.source_packets.last
                                                    : unit.last_dts;
    if (first > last) std::swap(first, last);

    GopCopyResult result;
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        close_format();
        return std::nullopt;
    }
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_index) {
            const auto stamp =
                packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
            if (stamp != AV_NOPTS_VALUE && stamp >= first && stamp <= last) {
                // Verbatim payload, translated onto the container's zero: the
                // schedule's left edge is the origin, so a trimmed export
                // starts where the requested window starts. Integer ticks in
                // the source time base — the mux still owns the rescale.
                if (window_origin != 0) {
                    if (packet->pts != AV_NOPTS_VALUE) packet->pts -= window_origin;
                    if (packet->dts != AV_NOPTS_VALUE) packet->dts -= window_origin;
                }
                // submit_video consumes the packet, so reuse of this AVPacket
                // for the next read is safe.
                if (!mux.submit_video(*packet, time_base)) {
                    av_packet_unref(packet);
                    av_packet_free(&packet);
                    close_format();
                    return std::nullopt;
                }
                ++result.video_packets;
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    close_format();
    return result;
#endif
}

std::optional<GopCopyResult> submit_source_audio(
    const std::string& source_path, chronon3d::media::MuxSession& mux,
    chronon3d::media::TimestampRange video_window,
    std::int64_t window_origin) {
#ifndef CHRONON3D_ENABLE_NATIVE_FFMPEG
    (void)source_path;
    (void)mux;
    (void)video_window;
    (void)window_origin;
    return std::nullopt;
#else
    // Audio is not GOP-aligned: the whole track is submitted exactly once, by
    // the caller, no matter how many work units ran.
    if (!mux.has_audio()) return GopCopyResult{};
    if (source_path.empty()) return std::nullopt;
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, source_path.c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    const auto close_format = [&]() { avformat_close_input(&format); };
    if (avformat_find_stream_info(format, nullptr) < 0) {
        close_format();
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
    if (audio_index < 0) {
        close_format();
        return GopCopyResult{};
    }
    const auto time_base = format->streams[audio_index]->time_base;

    // The scheduled window arrives in the SOURCE VIDEO time base — the
    // authority the work units speak. This is the one and only conversion, and
    // it is the container's own rescale: no seconds are reintroduced, so the
    // schedule keeps a single interpretation. The window is half-open
    // [first, last), matching how a GOP's right edge is defined (the next GOP's
    // start), so a packet exactly on the boundary belongs to the next window.
    std::int64_t audio_first = INT64_MIN;
    std::int64_t audio_last = INT64_MAX;
    // The video track is placed at the container's zero by the same origin, so
    // the audio must move with it: one translation, in the audio time base.
    const std::int64_t audio_origin = video_index >= 0
        ? av_rescale_q(window_origin, format->streams[video_index]->time_base,
                       time_base)
        : 0;
    if (video_window.present) {
        const auto video_time_base = video_index >= 0
            ? format->streams[video_index]->time_base : AVRational{0, 1};
        if (video_index < 0 || video_time_base.num <= 0 ||
            video_time_base.den <= 0) {
            close_format();
            return std::nullopt;
        }
        audio_first = av_rescale_q(video_window.first, video_time_base,
                                   time_base);
        audio_last = av_rescale_q(video_window.last, video_time_base,
                                 time_base);
        if (audio_first > audio_last) std::swap(audio_first, audio_last);
    }

    GopCopyResult result;
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        close_format();
        return std::nullopt;
    }
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == audio_index) {
            const auto stamp =
                packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
            if (stamp != AV_NOPTS_VALUE &&
                (stamp < audio_first || stamp >= audio_last)) {
                av_packet_unref(packet);
                continue;
            }
            auto owned = chronon3d::media::OwnedAVPacket(av_packet_alloc());
            if (audio_origin != 0) {
                if (packet->pts != AV_NOPTS_VALUE) packet->pts -= audio_origin;
                if (packet->dts != AV_NOPTS_VALUE) packet->dts -= audio_origin;
            }
            const bool submitted = owned &&
                av_packet_ref(owned.get(), packet) >= 0 &&
                mux.submit_audio(chronon3d::media::EncodedPacket{
                    std::move(owned), time_base, false});
            if (!submitted) {
                av_packet_unref(packet);
                av_packet_free(&packet);
                close_format();
                return std::nullopt;
            }
            ++result.audio_packets;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    close_format();
    return result;
#endif
}

} // namespace chronon3d::cli
