#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// media_contract_from_probe.hpp — boundary mapper for the render receipt.
//
// FIX 2 (TICKET-OVERLAY-THROUGHPUT-100-MIN): the export boundary already
// inspected the freshly muxed output (`media::video::verify_output_contract`,
// canonical in-process probe). Mapping that ONE observation onto the receipt's
// MediaContract keeps one probe per artifact instead of a second
// `ffprobe -show_streams -show_format` subprocess (~300 ms, spawn dominated).
// Fields the in-process probe cannot observe (codec profile, SAR, HDR side
// data, GOP markers) stay at their documented defaults — the receipt's
// verification verdicts only consume the fields mapped here.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/verification/render_receipt.hpp>

namespace chronon3d::cli {

[[nodiscard]] inline verification::MediaContract media_contract_from_probe(
    const media::video::MediaProbeInfo& probe) {
    using media::video::MediaStreamKind;
    verification::MediaContract media;
    media.container = probe.format_name;
    if (probe.duration.has_value()) {
        const auto& duration = *probe.duration;
        if (duration.time_base.denominator > 0) {
            media.duration_ms =
                static_cast<double>(duration.ticks()) *
                static_cast<double>(duration.time_base.numerator) /
                static_cast<double>(duration.time_base.denominator) * 1000.0;
        }
    }
    int video_streams = 0;
    int audio_streams = 0;
    for (const auto& stream : probe.streams) {
        switch (stream.kind) {
            case MediaStreamKind::Video: {
                ++video_streams;
                if (video_streams > 1) break;  // first video stream is the contract
                media.codec = stream.codec;
                media.pixel_format = stream.pixel_format;
                media.width = stream.width;
                media.height = stream.height;
                media.fps_num = stream.frame_rate.numerator;
                media.fps_den = stream.frame_rate.denominator > 0
                    ? stream.frame_rate.denominator : 1;
                // The canonical probe reports the CFR nominal rate (r_frame_rate
                // with avg fallback). The fps verdict accepts either signal, so
                // publishing it as both keeps the verdict identical for the CFR
                // outputs this exporter produces.
                media.r_fps_num = media.fps_num;
                media.r_fps_den = media.fps_den;
                // The receipt's frame-count verdict treats a missing container
                // count as "skip", never as zero frames.
                media.frame_count = stream.frame_count > 0 ? stream.frame_count : -1;
                media.color_range = stream.color_range;
                media.color_space = stream.color_space;
                media.color_transfer = stream.color_transfer;
                media.color_primaries = stream.color_primaries;
                media.has_video = true;
                media.video_time_base_num = stream.time_base.numerator;
                media.video_time_base_den = stream.time_base.denominator > 0
                    ? stream.time_base.denominator : 1;
                break;
            }
            case MediaStreamKind::Audio: {
                ++audio_streams;
                if (audio_streams == 1) {
                    media.audio_codec = stream.codec;
                    media.sample_rate = stream.sample_rate;
                    media.channels = stream.channels;
                    media.audio_time_base_num = stream.time_base.numerator;
                    media.audio_time_base_den = stream.time_base.denominator > 0
                        ? stream.time_base.denominator : 1;
                    media.has_audio = true;
                }
                break;
            }
            default:
                break;
        }
    }
    media.video_streams = video_streams;
    media.audio_streams = audio_streams;
    media.probed = true;
    return media;
}

} // namespace chronon3d::cli
