#include "native_av_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
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

    // NVENC consumes CUDA frames, while the Chronon framebuffer converter
    // writes host YUV planes.  Trying to make the CUDA frame writable (or
    // writing its device pointers as if they were host memory) fails before
    // the first FullGraph frame.  Convert into a host staging frame, then
    // upload it through FFmpeg's CUDA frames context.
    AVFrame* conversion_frame = frame_;
    AVFrame* cpu_staging = nullptr;
    if (gpu_nvenc_) {
        cpu_staging = av_frame_alloc();
        if (!cpu_staging) return false;
        cpu_staging->format = AV_PIX_FMT_YUV420P;
        cpu_staging->width = options_.width;
        cpu_staging->height = options_.height;
        if (av_frame_get_buffer(cpu_staging, 32) < 0 ||
            av_frame_make_writable(cpu_staging) < 0) {
            av_frame_free(&cpu_staging);
            spdlog::error("[native_av] CPU staging frame allocation failed");
            return false;
        }
        conversion_frame = cpu_staging;
    } else if (av_frame_make_writable(frame_) < 0) {
        spdlog::error("[native_av] av_frame_make_writable failed");
        return false;
    }

    // ── Single-entry YUV conversion cache ────────────────────────────────
    // For static/frozen frames (common in intros, backgrounds), skip
    // the expensive RGBA→YUV conversion entirely when the same framebuffer
    // digest arrives consecutively.  The AVFrame planes already contain
    // the correct YUV data from the previous conversion — no memcpy needed.
    // ----------------------------------------------------------------------
    const uint64_t digest       = fb.key_digest();
    const int      color_matrix = resolve_color_matrix(options_);

    const bool same_as_last =
        !gpu_nvenc_ &&
        digest != 0 &&
        digest == last_converted_digest_ &&
        options_.width                           == last_converted_width_ &&
        options_.height                          == last_converted_height_ &&
        static_cast<int>(codec_->pix_fmt)        == last_converted_pix_fmt_ &&
        options_.color_transform.apply_gamma     == last_converted_apply_gamma_ &&
        color_matrix                             == last_converted_color_matrix_;

    // 2. Convert the framebuffer to YUV (or skip for cache hit)
    const auto t_conv0 = Clock::now();
    const uint64_t pixel_before = profiling::g_current_counters
        ? profiling::g_current_counters->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_before = profiling::g_current_counters
        ? profiling::g_current_counters->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;

    double frame_conv_ms = 0.0;
    double frame_send_ms = 0.0;
    if (same_as_last) {
        // ── Cache HIT: AVFrame already has correct YUV data ──
        ++cache_hits_;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->converted_frame_cache_hits.fetch_add(
                1, std::memory_order_relaxed);
        }
    } else {
        // ── Cache MISS: perform the full RGBA→YUV conversion ──
        ++cache_misses_;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->converted_frame_cache_misses.fetch_add(
                1, std::memory_order_relaxed);
        }

        video::ConvertFrameRequest req{
            .src           = fb,
            .planes        = video::FramePlanes{
                .y         = conversion_frame->data[0],
                .u         = conversion_frame->data[1],
                .v         = conversion_frame->data[2],
                .uv        = nullptr,
                .stride_y  = conversion_frame->linesize[0],
                .stride_u  = conversion_frame->linesize[1],
                .stride_v  = conversion_frame->linesize[2],
                .stride_uv = 0,
            },
            .width         = options_.width,
            .height        = options_.height,
            .format        = video::EncoderPixelFormat::YUV420P,
            .matrix        = static_cast<video::YuvMatrix>(color_matrix),
            .range         = video::ColorRange::Limited,
            .apply_gamma   = options_.color_transform.apply_gamma,
        };

        auto conv_result = video::convert_frame(req);
        if (!conv_result.success) {
            av_frame_free(&cpu_staging);
            spdlog::error("[native_av] convert_frame failed");
            return false;
        }

        // Update the single-entry cache state for the next frame.
        last_converted_digest_      = digest;
        last_converted_width_       = options_.width;
        last_converted_height_      = options_.height;
        last_converted_pix_fmt_     = static_cast<int>(codec_->pix_fmt);
        last_converted_apply_gamma_ = options_.color_transform.apply_gamma;
        last_converted_color_matrix_= color_matrix;
    }

    const uint64_t pixel_after = profiling::g_current_counters
        ? profiling::g_current_counters->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_after = profiling::g_current_counters
        ? profiling::g_current_counters->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const double conv_ms = elapsed_ms(t_conv0);
    frame_conv_ms = conv_ms;
    native_convert_ms_ += conv_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->video_conversion_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        profiling::g_current_counters->frame_conversion_copy_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        if (same_as_last) {
            // Track how much time we saved by skipping conversion.
            profiling::g_current_counters->native_av_convert_skipped_wall_ms.fetch_add(
                static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        } else {
            profiling::g_current_counters->native_av_convert_wall_ms.fetch_add(
                static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        }
    }

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (gpu_nvenc_) {
        av_frame_unref(frame_);
        frame_->format = AV_PIX_FMT_CUDA;
        frame_->width = options_.width;
        frame_->height = options_.height;
        frame_->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
        if (!cuda_frames_ref_ || av_hwframe_get_buffer(cuda_frames_ref_, frame_, 0) < 0 ||
            av_hwframe_transfer_data(frame_, cpu_staging, 0) < 0) {
            av_frame_free(&cpu_staging);
            spdlog::error("[native_av] CPU-to-CUDA frame transfer failed");
            return false;
        }
        av_frame_free(&cpu_staging);
    }
#endif

    // 3. Set PTS (presentation timestamp) in frame number units
    frame_->pts = static_cast<int64_t>(frames_written_);

    // 4. Send the frame to the encoder (with EAGAIN back-pressure loop).
    //    `send_ms` measures ONLY avcodec_send_frame time (pure submit CPU),
    //    excluding drain_packets, to avoid temporal double-counting with
    //    receive/mux counters.  `backpressure_ms` separately captures the
    //    drain+retry wait that EAGAIN triggers (encoder back-pressure).
    const auto t_send0 = Clock::now();
    int ret = avcodec_send_frame(codec_, frame_);
    double send_ms = elapsed_ms(t_send0);

    int eagain_retries = 0;
    double backpressure_ms = 0.0;
    // Allow enough retries to match x264's internal frame queue depth.
    // With threads=auto + thread_type=frame on an 8-core machine, x264
    // keeps ~10-12 frames in flight, so 3 retries often hit EAGAIN and
    // trigger back-pressure on the render queue.  12 retries let the
    // encoder drain smoothly without blocking the render thread.
    constexpr int kMaxEagainRetries = 12;
    while (ret == AVERROR(EAGAIN) && eagain_retries < kMaxEagainRetries) {
        ++eagain_retries;
        // Encoder buffer full — drain packets (back-pressure wait), then retry.
        const auto t_bp0 = Clock::now();
        if (!drain_packets()) {
            return false;
        }
        backpressure_ms += elapsed_ms(t_bp0);
        const auto t_retry = Clock::now();
        ret = avcodec_send_frame(codec_, frame_);
        send_ms += elapsed_ms(t_retry);
    }

    if (ret == AVERROR(EAGAIN)) {
        spdlog::error("[native_av] avcodec_send_frame still EAGAIN after {} retries", kMaxEagainRetries);
        return false;
    }
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        spdlog::error("[native_av] avcodec_send_frame error: {} ({})", ret, err_buf);
        return false;
    }

    frame_send_ms = send_ms;
    native_send_frame_ms_ += send_ms;
    native_backpressure_ms_ += backpressure_ms;
    if (profiling::g_current_counters) {
        profiling::g_current_counters->encoder_submit_cpu_ms.fetch_add(
            static_cast<uint64_t>(send_ms), std::memory_order_relaxed);
        profiling::g_current_counters->encoder_backpressure_wait_ms.fetch_add(
            static_cast<uint64_t>(backpressure_ms), std::memory_order_relaxed);
    }

    ++frames_written_;
    last_frame_telemetry_ = {
        .conversion_copy_ms = frame_conv_ms,
        .pixel_format_convert_ms = static_cast<double>(pixel_after - pixel_before),
        .color_space_convert_ms = static_cast<double>(color_after - color_before),
        .encoder_ms = frame_send_ms,
        .backpressure_wait_ms = backpressure_ms,
        .native_convert_ms = frame_conv_ms,
        .native_send_ms = frame_send_ms,
    };
    return true;
}

} // namespace chronon3d::cli
