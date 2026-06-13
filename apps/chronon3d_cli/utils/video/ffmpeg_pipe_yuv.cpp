#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_conversion_service.hpp>

namespace chronon3d::cli {

// ── Per-format conversion methods ────────────────────────────────────────────
// These are THIN FORWARDING WRAPPERS around the central FrameConversionService.
// All float→YUV/RGBA conversion logic lives in chronon3d::video
// (frame_converter.cpp + direct_yuv_converter*.cpp + frame_conversion_service.cpp).
// DO NOT add conversion logic here.

bool FfmpegPipeEncoder::convert_framebuffer_to_rgba(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height)
        return false;

    if (!dst) {
        const size_t req = static_cast<size_t>(options_.width)
                         * static_cast<size_t>(options_.height) * 4u;
        if (rgba_buffer_.size() != req) rgba_buffer_.assign(req, 0);
        dst = rgba_buffer_.data();
    }

    const video::ConversionOptions copts{
        .width       = options_.width,
        .height      = options_.height,
        .format      = video::EncoderPixelFormat::RGBA8,
        .apply_gamma = options_.color_transform.apply_gamma,
        .color_matrix = 0,
        .use_cache   = false,  // RGBA is the native output format, no conversion to cache
    };

    return conv_svc_.convert_to_buffer(fb, copts, dst,
        static_cast<size_t>(options_.width) * options_.height * 4u);
}

bool FfmpegPipeEncoder::convert_framebuffer_to_yuv420p(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height)
        return false;
    if (options_.width % 2 != 0 || options_.height % 2 != 0)
        return false;

    const size_t total = static_cast<size_t>(options_.width) * options_.height * 3u / 2u;

    if (!dst) {
        if (yuv_buffer_.size() != total) yuv_buffer_.assign(total, 0);
        dst = yuv_buffer_.data();
    }

    const video::ConversionOptions copts{
        .width       = options_.width,
        .height      = options_.height,
        .format      = video::EncoderPixelFormat::YUV420P,
        .apply_gamma = options_.color_transform.apply_gamma,
        .color_matrix = 0,
        .use_cache   = true,
    };

    return conv_svc_.convert_to_buffer(fb, copts, dst, total);
}

bool FfmpegPipeEncoder::convert_framebuffer_to_nv12(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height)
        return false;
    if (options_.width % 2 != 0 || options_.height % 2 != 0)
        return false;

    const size_t total = static_cast<size_t>(options_.width) * options_.height * 3u / 2u;

    if (!dst) {
        if (yuv_buffer_.size() != total) yuv_buffer_.assign(total, 0);
        dst = yuv_buffer_.data();
    }

    const video::ConversionOptions copts{
        .width       = options_.width,
        .height      = options_.height,
        .format      = video::EncoderPixelFormat::NV12,
        .apply_gamma = options_.color_transform.apply_gamma,
        .color_matrix = 0,
        .use_cache   = true,
    };

    return conv_svc_.convert_to_buffer(fb, copts, dst, total);
}

} // namespace chronon3d::cli
