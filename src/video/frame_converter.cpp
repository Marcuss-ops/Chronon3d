#include <chronon3d/video/frame_converter.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace chronon3d::video {

// ── Helpers ─────────────────────────────────────────────────────────────────

static inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

// ── YUV420P ─────────────────────────────────────────────────────────────────

static ConvertFrameResult convert_rgba_to_yuv420p(const ConvertFrameRequest& req) {
    if (!req.dst_y || !req.dst_u || !req.dst_v) {
        throw std::invalid_argument("convert_frame(YUV420P): dst_y/u/v must not be null");
    }
    if (req.width % 2 != 0 || req.height % 2 != 0) {
        return ConvertFrameResult{.success = false};
    }

    const uint64_t t0 = now_ns();

    // The SIMD kernel handles luma + chroma in one pass over the row range.
    // We keep the same TBB grain-size as the original encoder (8 row-pairs)
    // to avoid spawning excessive tasks for 1080p.
    tbb::parallel_for(
        tbb::blocked_range<int>(0, req.height / 2, 8),
        [&](const tbb::blocked_range<int>& range) {
            const int y_start = range.begin() * 2;
            const int y_end   = range.end()   * 2;
            simd::convert_f32_rgba_to_yuv420p_simd_rows(
                req.dst_y, req.dst_u, req.dst_v,
                req.src.data(),
                req.width, req.height, req.src.allocated_width(),
                y_start, y_end,
                req.apply_gamma);
        });

    return ConvertFrameResult{
        .success        = true,
        .used_simd      = true,
        .conversion_ns  = now_ns() - t0,
    };
}

// ── NV12 ────────────────────────────────────────────────────────────────────

static ConvertFrameResult convert_rgba_to_nv12(const ConvertFrameRequest& req) {
    if (!req.dst_y || !req.dst_uv) {
        throw std::invalid_argument("convert_frame(NV12): dst_y/uv must not be null");
    }
    if (req.width % 2 != 0 || req.height % 2 != 0) {
        return ConvertFrameResult{.success = false};
    }

    const uint64_t t0 = now_ns();

    tbb::parallel_for(
        tbb::blocked_range<int>(0, req.height / 2, 8),
        [&](const tbb::blocked_range<int>& range) {
            const int y_start = range.begin() * 2;
            const int y_end   = range.end()   * 2;
            simd::convert_f32_rgba_to_nv12_simd_rows(
                req.dst_y, req.dst_uv,
                req.src.data(),
                req.width, req.height, req.src.allocated_width(),
                y_start, y_end,
                req.apply_gamma);
        });

    return ConvertFrameResult{
        .success       = true,
        .used_simd     = true,
        .conversion_ns = now_ns() - t0,
    };
}

// ── RGB24 ────────────────────────────────────────────────────────────────────
// Scalar — libx264rgb is a rare path and does not justify a SIMD kernel yet.

static ConvertFrameResult convert_rgba_to_rgb24(const ConvertFrameRequest& req) {
    if (!req.dst_y) {
        throw std::invalid_argument("convert_frame(RGB24): dst_y (packed output) must not be null");
    }

    const uint64_t t0 = now_ns();
    const Color* src  = req.src.data();

    color::OutputTransformOptions out_opts;
    out_opts.apply_gamma = req.apply_gamma;
    out_opts.output = color::ColorSpace::SRGB;

    tbb::parallel_for(0, req.height, [&](int y) {
        const Color* row     = src + static_cast<size_t>(y) * req.src.allocated_width();
        uint8_t*     dst_row = req.dst_y + static_cast<size_t>(y) * req.width * 3;
        for (int x = 0; x < req.width; ++x) {
            const Color& c = row[x];
            const auto rgb = color::linear_to_output_rgb8(c, out_opts);
            dst_row[x * 3 + 0] = rgb.r;
            dst_row[x * 3 + 1] = rgb.g;
            dst_row[x * 3 + 2] = rgb.b;
        }
    });

    return ConvertFrameResult{
        .success       = true,
        .used_simd     = false,  // scalar
        .conversion_ns = now_ns() - t0,
    };
}

// ── Public dispatcher ────────────────────────────────────────────────────────

ConvertFrameResult convert_frame(const ConvertFrameRequest& req) {
    switch (req.format) {
        case EncoderPixelFormat::YUV420P: return convert_rgba_to_yuv420p(req);
        case EncoderPixelFormat::NV12:    return convert_rgba_to_nv12(req);
        case EncoderPixelFormat::RGB24:   return convert_rgba_to_rgb24(req);
    }
    throw std::invalid_argument("convert_frame: unknown EncoderPixelFormat");
}

ConvertFrameResult convert_frame_tight(
    const Framebuffer& src,
    uint8_t* dst_y, uint8_t* dst_u, uint8_t* dst_v, uint8_t* dst_uv,
    int width, int height,
    EncoderPixelFormat format,
    bool apply_gamma)
{
    ConvertFrameRequest req{
        .src            = src,
        .dst_y          = dst_y,
        .dst_u          = dst_u,
        .dst_v          = dst_v,
        .dst_uv         = dst_uv,
        .dst_stride_y   = width,
        .dst_stride_uv  = width,          // NV12: interleaved pairs → full width
        .dst_stride_u   = width / 2,
        .dst_stride_v   = width / 2,
        .color_matrix   = 0,
        .width          = width,
        .height         = height,
        .format         = format,
        .apply_gamma    = apply_gamma,
    };
    return convert_frame(req);
}

} // namespace chronon3d::video
