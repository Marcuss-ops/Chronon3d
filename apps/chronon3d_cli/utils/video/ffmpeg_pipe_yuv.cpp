#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/video/frame_converter.hpp>
#include <chronon3d/video/direct_yuv_lut.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <array>
#include <tbb/parallel_for.h>

namespace chronon3d::cli {

// ── Per-format conversion methods ────────────────────────────────────────────
// These are THIN FORWARDING WRAPPERS.  All conversion logic lives in
// chronon3d::video (frame_converter.cpp + direct_yuv_converter*.cpp).
// DO NOT add float→YUV logic here — use video::convert_frame_tight instead.
// The encoder methods exist for backwards-compatibility with external callers
// and the io_uring path which uses pre-allocated ring buffers.

bool FfmpegPipeEncoder::convert_framebuffer_to_rgba(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height)
        return false;

    // RGBA path: keep the existing sRGB LUT path for correctness; the
    // frame_converter RGB24 drop-alpha variant is only for libx264rgb.
    if (!dst) {
        const size_t req = static_cast<size_t>(options_.width)
                         * static_cast<size_t>(options_.height) * 4u;
        if (rgba_buffer_.size() != req) rgba_buffer_.assign(req, 0);
    }
    uint8_t* dst_ptr = dst ? dst : rgba_buffer_.data();
    const auto& transform = options_.color_transform;

    if (transform.apply_gamma &&
        (transform.output == color::ColorSpace::SRGB ||
         transform.output == color::ColorSpace::Rec709)) {
        // Use the 64KB integer LUT (g_srgb_lut[65536]) shared with the
        // direct YUV converter.  Saves ~2 float comparisons + 1 float
        // multiply per channel vs the old float-based clamping+LUT path.
        tbb::parallel_for(0, options_.height, [&](int y) {
            const Color* src_row = fb.pixels_row(y);
            uint8_t* dr = dst_ptr + static_cast<size_t>(y) * options_.width * 4u;
            for (int x = 0; x < options_.width; ++x) {
                const Color& c = src_row[x];
                dr[x*4+0] = video::linear_to_srgb8_fast(c.r);
                dr[x*4+1] = video::linear_to_srgb8_fast(c.g);
                dr[x*4+2] = video::linear_to_srgb8_fast(c.b);
                dr[x*4+3] = video::linear_to_srgb8_fast(c.a);
            }
        });
    } else {
        tbb::parallel_for(0, options_.height, [&](int y) {
            const Color* src_row = fb.pixels_row(y);
            uint8_t* dr = dst_ptr + static_cast<size_t>(y) * options_.width * 4u;
            for (int x = 0; x < options_.width; ++x) {
                const auto rgb = color::linear_to_output_rgb8(src_row[x], transform);
                dr[x*4+0] = rgb.r;
                dr[x*4+1] = rgb.g;
                dr[x*4+2] = rgb.b;
                dr[x*4+3] = Color::linear_to_srgb8(src_row[x].a);
            }
        });
    }
    return true;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_yuv420p(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height)
        return false;
    if (options_.width % 2 != 0 || options_.height % 2 != 0)
        return false;

    const size_t y_size  = static_cast<size_t>(options_.width) * options_.height;
    const size_t uv_size = y_size / 4u;

    if (!dst) {
        const size_t req = y_size + uv_size * 2;
        if (yuv_buffer_.size() != req) yuv_buffer_.assign(req, 0);
        dst = yuv_buffer_.data();
    }

    const auto res = video::convert_frame_tight(
        fb,
        dst,                  // Y
        dst + y_size,         // U
        dst + y_size + uv_size, // V
        nullptr,              // UV (not used for YUV420P)
        options_.width, options_.height,
        video::EncoderPixelFormat::YUV420P,
        options_.color_transform.apply_gamma);
    return res.success;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_nv12(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height)
        return false;
    if (options_.width % 2 != 0 || options_.height % 2 != 0)
        return false;

    const size_t y_size  = static_cast<size_t>(options_.width) * options_.height;
    const size_t uv_size = y_size / 2u;

    if (!dst) {
        const size_t req = y_size + uv_size;
        if (yuv_buffer_.size() != req) yuv_buffer_.assign(req, 0);
        dst = yuv_buffer_.data();
    }

    const auto res = video::convert_frame_tight(
        fb,
        dst,           // Y
        nullptr,       // U (not used for NV12)
        nullptr,       // V (not used for NV12)
        dst + y_size,  // UV interleaved
        options_.width, options_.height,
        video::EncoderPixelFormat::NV12,
        options_.color_transform.apply_gamma);
    return res.success;
}

} // namespace chronon3d::cli
