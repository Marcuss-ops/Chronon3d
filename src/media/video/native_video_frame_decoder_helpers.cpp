// native_video_frame_decoder_helpers.cpp — shared decode helpers (CUDA format
// selection, CPU frame materialization, PTS sample-table scan, diagnostics).

#include "native_video_frame_decoder_detail.hpp"

#include <chronon3d/color/output_transform.hpp>
#ifdef CHRONON3D_ENABLE_OCIO
#include <chronon3d/color/ocio_color_transform.hpp>
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

extern "C" {
#include <libavutil/imgutils.h>
}

namespace chronon3d::media {

enum AVPixelFormat select_cuda_format(AVCodecContext*, const enum AVPixelFormat* formats) {
    for (const enum AVPixelFormat* it = formats; *it != AV_PIX_FMT_NONE; ++it) {
        if (*it == AV_PIX_FMT_CUDA) return *it;
    }
    return formats[0];
}

std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws,
    std::vector<std::uint8_t>& rgba_staging,
    RenderCounters* counters, bool enable_swscale
#ifdef CHRONON3D_ENABLE_OCIO
    , const color::OcioColorTransform* ocio_transform
#endif
    ) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return nullptr;
    const auto supported_transfer = [](AVColorTransferCharacteristic transfer) {
        switch (transfer) {
            case AVCOL_TRC_UNSPECIFIED:
            case AVCOL_TRC_BT709:
            case AVCOL_TRC_IEC61966_2_1:
            case AVCOL_TRC_LINEAR:
            case AVCOL_TRC_GAMMA22:
            case AVCOL_TRC_SMPTE2084:
            case AVCOL_TRC_ARIB_STD_B67:
                return true;
            default:
                return false;
        }
    };
    int effective_primaries = frame->color_primaries;
    if (effective_primaries == AVCOL_PRI_UNSPECIFIED) {
        if (frame->colorspace == AVCOL_SPC_BT470BG)
            effective_primaries = AVCOL_PRI_BT470BG;
        else if (frame->colorspace == AVCOL_SPC_SMPTE170M)
            effective_primaries = AVCOL_PRI_SMPTE170M;
    }
    const bool supported_primaries =
        effective_primaries == AVCOL_PRI_UNSPECIFIED ||
        effective_primaries == AVCOL_PRI_BT709 ||
        effective_primaries == AVCOL_PRI_BT2020 ||
        effective_primaries == AVCOL_PRI_BT470BG ||
        effective_primaries == AVCOL_PRI_SMPTE170M ||
        effective_primaries == AVCOL_PRI_SMPTE432;
    const bool supported_matrix =
        frame->colorspace == AVCOL_SPC_UNSPECIFIED ||
        frame->colorspace == AVCOL_SPC_RGB ||
        frame->colorspace == AVCOL_SPC_BT709 ||
        frame->colorspace == AVCOL_SPC_BT470BG ||
        frame->colorspace == AVCOL_SPC_SMPTE170M ||
        frame->colorspace == AVCOL_SPC_BT2020_NCL;
    if ((!supported_transfer(frame->color_trc)
#ifdef CHRONON3D_ENABLE_OCIO
         && ocio_transform == nullptr
#endif
         ) || (!supported_primaries
#ifdef CHRONON3D_ENABLE_OCIO
               && ocio_transform == nullptr
#endif
               ) ||
        !supported_matrix)
        return nullptr;
    if (!enable_swscale) {
#ifdef CHRONON3D_ENABLE_OCIO
        if (ocio_transform != nullptr) return nullptr;
#endif
        return std::make_shared<Framebuffer>(frame->width, frame->height, false);
    }
    sws = sws_getCachedContext(sws, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_RGBA64LE, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return nullptr;
    // Keep the CPU reference boundary identical to the CUDA NV12 importer.
    // Goal3 sources commonly omit colour metadata; Chronon's canonical SDR
    // fallback is BT.709 limited-range YUV decoded to full-range RGB. The
    // libswscale defaults otherwise select BT.601 for these frames, which
    // produces a large green-channel parity error despite correct placement.
    const int source_cs = frame->colorspace == AVCOL_SPC_BT2020_NCL
        ? SWS_CS_BT2020
        : ((frame->colorspace == AVCOL_SPC_BT470BG ||
            frame->colorspace == AVCOL_SPC_SMPTE170M)
            ? SWS_CS_DEFAULT : SWS_CS_ITU709);
    const int* source_coefficients = sws_getCoefficients(source_cs);
    const int source_range = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    const int* destination_coefficients = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(sws, source_coefficients, source_range,
                             destination_coefficients, 1, 0, 1 << 16, 1 << 16);
    uint8_t* dst_data[4] = {nullptr, nullptr, nullptr, nullptr};
    int dst_stride[4] = {0, 0, 0, 0};
    constexpr std::size_t kSwscaleSimdPadding = 64;
    const int image_bytes = av_image_get_buffer_size(
        AV_PIX_FMT_RGBA64LE, frame->width, frame->height, 32);
    if (image_bytes <= 0) return nullptr;
    const auto required_bytes = static_cast<std::size_t>(image_bytes) +
                                kSwscaleSimdPadding;
    if (rgba_staging.size() < required_bytes) {
        rgba_staging.resize(required_bytes);
    }
    if (av_image_fill_arrays(dst_data, dst_stride, rgba_staging.data(),
                             AV_PIX_FMT_RGBA64LE, frame->width, frame->height, 32) < 0) {
        return nullptr;
    }
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
            const auto* row = reinterpret_cast<const std::uint16_t*>(
                dst_data[0] + static_cast<size_t>(y) * dst_stride[0]);
            Color* dst = pixels + static_cast<usize>(y) * stride;
            for (int x = 0; x < frame->width; ++x) {
                const auto* p = row + static_cast<size_t>(x) * 4;
                // swscale returns transfer-coded RGB in the source primaries.
                // Decode the frame's transfer before entering the canonical
                // linear framebuffer domain; PQ values are relative to 100 nits
                // and retain highlight headroom above SDR white.
                const float encoded_r = p[0] / 65535.0f;
                const float encoded_g = p[1] / 65535.0f;
                const float encoded_b = p[2] / 65535.0f;
                const float alpha = p[3] / 65535.0f;
                const auto decode = [frame](float value) {
                    switch (frame->color_trc) {
                        case AVCOL_TRC_IEC61966_2_1:
                            return value <= 0.04045f ? value / 12.92f
                                : std::pow((value + 0.055f) / 1.055f, 2.4f);
                        case AVCOL_TRC_LINEAR:
                            return value;
                        case AVCOL_TRC_GAMMA22:
                            return std::pow(std::max(value, 0.0f), 2.2f);
                        case AVCOL_TRC_SMPTE2084:
                            return color::pq_to_linear_100_nits(value);
                        case AVCOL_TRC_ARIB_STD_B67:
                            return color::hlg_to_linear_scene(value);
                        case AVCOL_TRC_UNSPECIFIED:
                        case AVCOL_TRC_BT709:
                        default:
                            return color::rec709_to_linear_float(value);
                    }
                };
                Color linear{decode(encoded_r), decode(encoded_g), decode(encoded_b), alpha};
#ifdef CHRONON3D_ENABLE_OCIO
                if (ocio_transform != nullptr) {
                    float rgb[3]{encoded_r, encoded_g, encoded_b};
                    if (!ocio_transform->apply_rgb(rgb)) {
                        linear = {std::numeric_limits<float>::quiet_NaN(), 0, 0, alpha};
                    } else {
                        linear = {rgb[0], rgb[1], rgb[2], alpha};
                    }
                } else
#endif
                {
                if (effective_primaries == AVCOL_PRI_BT2020) {
                    const auto converted = color::bt2020_to_bt709_linear(
                        linear.r, linear.g, linear.b);
                    linear.r = converted[0];
                    linear.g = converted[1];
                    linear.b = converted[2];
                } else if (effective_primaries == AVCOL_PRI_SMPTE170M) {
                    const auto converted = color::smpte170m_to_bt709_linear(
                        linear.r, linear.g, linear.b);
                    linear.r = converted[0];
                    linear.g = converted[1];
                    linear.b = converted[2];
                } else if (effective_primaries == AVCOL_PRI_BT470BG) {
                    const auto converted = color::bt470bg_to_bt709_linear(
                        linear.r, linear.g, linear.b);
                    linear.r = converted[0];
                    linear.g = converted[1];
                    linear.b = converted[2];
                } else if (effective_primaries == AVCOL_PRI_SMPTE432) {
                    const auto converted = color::display_p3_to_bt709_linear(
                        linear.r, linear.g, linear.b);
                    linear.r = converted[0];
                    linear.g = converted[1];
                    linear.b = converted[2];
                }
                }
                dst[x] = linear;
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
#ifdef CHRONON3D_ENABLE_OCIO
    if (ocio_transform != nullptr) {
        for (int y = 0; y < frame->height; ++y) {
            const Color* row = fb->pixels_row(y);
            for (int x = 0; x < frame->width; ++x) {
                if (!std::isfinite(row[x].r) || !std::isfinite(row[x].g) ||
                    !std::isfinite(row[x].b)) return nullptr;
            }
        }
    }
#endif
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
