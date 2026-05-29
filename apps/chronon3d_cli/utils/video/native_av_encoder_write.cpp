#include "native_av_encoder.hpp"
#include <chronon3d/video/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chrono>

namespace chronon3d::cli {

namespace {
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // anonymous namespace

// Map FfmpegPipeOptions to the encoder pixel format.
static video::EncoderPixelFormat resolve_encoder_pix_fmt(const FfmpegPipeOptions& opt) {
    return video::EncoderPixelFormat::YUV420P;
}

// Map FfmpegPipeOptions color space to the color matrix ID.
static int resolve_color_matrix(const FfmpegPipeOptions& opt) {
    switch (opt.color_transform.output) {
        case chronon3d::color::ColorSpace::Rec709:
        case chronon3d::color::ColorSpace::SRGB:
        case chronon3d::color::ColorSpace::LinearSRGB:
        default:
            return 0; // BT.709
    }
}

bool NativeAvEncoder::write_frame(const Framebuffer& fb) {
    if (!codec_ || !frame_ || !packet_) {
        return false;
    }

    // 1. Make the AVFrame writable (FFmpeg internal ref-counting)
    if (av_frame_make_writable(frame_) < 0) {
        spdlog::error("[native_av] av_frame_make_writable failed");
        return false;
    }

    // 2. Convert the framebuffer directly into AVFrame data planes.
    const auto t_conv0 = Clock::now();

    video::ConvertFrameRequest req{
        .src           = fb,
        .dst_y         = frame_->data[0],
        .dst_u         = frame_->data[1],
        .dst_v         = frame_->data[2],
        .dst_uv        = nullptr,
        .dst_stride_y  = frame_->linesize[0],
        .dst_stride_u  = frame_->linesize[1],
        .dst_stride_v  = frame_->linesize[2],
        .color_matrix  = resolve_color_matrix(options_),
        .width         = options_.width,
        .height        = options_.height,
        .format        = video::EncoderPixelFormat::YUV420P,
        .apply_gamma   = options_.color_transform.apply_gamma,
    };

    auto conv_result = video::convert_frame(req);
    if (!conv_result.success) {
        spdlog::error("[native_av] convert_frame failed");
        return false;
    }

    const double conv_ms = elapsed_ms(t_conv0);
    native_convert_ms_ += conv_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->video_conversion_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        profiling::g_current_counters->frame_conversion_copy_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        profiling::g_current_counters->native_av_convert_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
    }

    // 3. Set PTS (presentation timestamp) in frame number units
    frame_->pts = static_cast<int64_t>(frames_written_);

    // 4. Send the frame to the encoder
    const auto t_send0 = Clock::now();
    int ret = avcodec_send_frame(codec_, frame_);
    const double send_ms = elapsed_ms(t_send0);
    native_send_frame_ms_ += send_ms;

    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        spdlog::error("[native_av] avcodec_send_frame error: {} ({})", ret, err_buf);
        return false;
    }

    if (profiling::g_current_counters) {
        profiling::g_current_counters->native_av_send_frame_ms.fetch_add(
            static_cast<uint64_t>(send_ms), std::memory_order_relaxed);
    }

    // 5. Drain all packets produced from this frame
    //    (timing for receive_packet and mux_write is tracked inside drain_packets)
    if (!drain_packets()) {
        return false;
    }

    // 6. Push mux/receive counters to global counters (delta only)
    //     native_receive_packet_ms_ and native_mux_write_ms_ are totals
    //     accumulated inside drain_packets() across all frames. We compute
    //     the delta since the previous write_frame() call.
    {
        static thread_local double prev_recv = 0.0;
        static thread_local double prev_mux  = 0.0;
        const double delta_recv = native_receive_packet_ms_ - prev_recv;
        const double delta_mux  = native_mux_write_ms_ - prev_mux;
        prev_recv = native_receive_packet_ms_;
        prev_mux  = native_mux_write_ms_;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->native_av_receive_packet_ms.fetch_add(
                static_cast<uint64_t>(delta_recv), std::memory_order_relaxed);
            profiling::g_current_counters->native_av_mux_write_ms.fetch_add(
                static_cast<uint64_t>(delta_mux), std::memory_order_relaxed);
        }
    }

    ++frames_written_;
    return true;
}

} // namespace chronon3d::cli
