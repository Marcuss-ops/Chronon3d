// native_video_frame_decoder_helpers.cpp — shared decode helpers (CUDA format
// selection, CPU frame materialization, PTS sample-table scan, diagnostics).

#include "native_video_frame_decoder_detail.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

namespace chronon3d::media {

enum AVPixelFormat select_cuda_format(AVCodecContext*, const enum AVPixelFormat* formats) {
    for (const enum AVPixelFormat* it = formats; *it != AV_PIX_FMT_NONE; ++it) {
        if (*it == AV_PIX_FMT_CUDA) return *it;
    }
    return formats[0];
}

std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws, std::vector<uint8_t>& rgba,
    RenderCounters* counters, bool enable_swscale) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return nullptr;
    if (!enable_swscale) {
        return std::make_shared<Framebuffer>(frame->width, frame->height, false);
    }
    sws = sws_getCachedContext(sws, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return nullptr;
    rgba.resize(static_cast<size_t>(frame->width) * frame->height * 4);
    uint8_t* dst_data[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dst_stride[4] = {frame->width * 4, 0, 0, 0};
    const auto start = profiling::now();
    const int ret = sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                              dst_data, dst_stride);
    const auto sws_dur = static_cast<uint64_t>(profiling::duration_ms(start, profiling::now()));
    if (counters) {
        counters->video_decode_sws_wall_ms.fetch_add(sws_dur, std::memory_order_relaxed);
        counters->swscale_ms.fetch_add(sws_dur, std::memory_order_relaxed);
    }
    if (ret != frame->height) return nullptr;
    profiling::FramebufferAllocationScope allocation_scope(
        profiling::FramebufferAllocationCategory::Video);
    auto fb = std::make_shared<Framebuffer>(frame->width, frame->height, false);
    const i32 stride = fb->allocated_width();
    Color* pixels = fb->data();
    const auto conv_start = profiling::now();
    const auto convert_rows = [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const uint8_t* row = rgba.data() + static_cast<size_t>(y) * frame->width * 4;
            Color* dst = pixels + static_cast<usize>(y) * stride;
            for (int x = 0; x < frame->width; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                dst[x] = Color{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
            }
        }
    };
    constexpr std::size_t kParallelPixelThreshold = 4096;
    const auto pixels_count = static_cast<std::size_t>(frame->width) *
                              static_cast<std::size_t>(frame->height);
    if (pixels_count < kParallelPixelThreshold) {
        convert_rows(0, frame->height);
    } else {
        const int grain = std::max(16, frame->height / 16);
        parallel_for_tracked(tbb::blocked_range<int>(0, frame->height, grain),
            [&](const tbb::blocked_range<int>& rows) {
                convert_rows(rows.begin(), rows.end());
            });
    }
    const auto conv_dur = static_cast<uint64_t>(profiling::duration_ms(conv_start, profiling::now()));
    if (counters) {
        counters->cpu_pixel_conversion_ms.fetch_add(conv_dur, std::memory_order_relaxed);
        counters->software_color_convert_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return fb;
}

bool build_source_sample_table(AVFormatContext* fmt, int stream_index,
                               SourceSampleTable& out) {
    if (!fmt || stream_index < 0 || stream_index >= static_cast<int>(fmt->nb_streams)) return false;
    const AVStream* stream = fmt->streams[stream_index];
    out = SourceSampleTable{Rational{stream->time_base.num, stream->time_base.den}};

    const int64_t start_pts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    if (av_seek_frame(fmt, stream_index, start_pts, AVSEEK_FLAG_BACKWARD) < 0) {
        spdlog::error("[native-decoder] PTS table scan requires a seekable source");
        return false;
    }
    avformat_flush(fmt);

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;
    std::uint64_t source_order = 0;
    std::uint32_t continuity = 0;
    int64_t last_dts = AV_NOPTS_VALUE;
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream_index && packet->pts != AV_NOPTS_VALUE) {
            if (packet->dts != AV_NOPTS_VALUE && last_dts != AV_NOPTS_VALUE && packet->dts < last_dts) {
                ++continuity;
            }
            out.add(SourceSample{
                .pts = packet->pts,
                .duration = packet->duration > 0 ? packet->duration : 0,
                .dts = packet->dts == AV_NOPTS_VALUE ? packet->pts : packet->dts,
                .source_order = source_order++,
                .continuity_id = continuity,
                .keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0,
            });
            if (packet->dts != AV_NOPTS_VALUE) last_dts = packet->dts;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    out.finalize();

    if (av_seek_frame(fmt, stream_index, start_pts, AVSEEK_FLAG_BACKWARD) < 0) {
        spdlog::error("[native-decoder] failed to rewind after PTS table scan");
        return false;
    }
    avformat_flush(fmt);
    return !out.empty();
}

void set_decode_diagnostic(DecodeDiagnostic* out,
                           DecodeFailureReason reason,
                           int ffmpeg_error,
                           std::int64_t pts,
                           std::int64_t dts,
                           std::uint64_t source_order,
                           std::string message) {
    if (!out) return;
    *out = DecodeDiagnostic{reason, ffmpeg_error, pts, dts, source_order, std::move(message)};
}

} // namespace chronon3d::media

#endif
