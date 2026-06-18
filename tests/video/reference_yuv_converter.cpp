// ============================================================================
// reference_yuv_converter.cpp — Single-threaded scalar YUV oracle (PR3).
//
// This is a deliberately simple, single-threaded reference.  It mirrors the
// core logic of the removed TBB parallel converter but without tbb::parallel_for,
// without profiling/timing, and without the dispatcher.  Used ONLY for
// correctness comparison — never in production.
//
// The gamma LUT (g_srgb_lut, declared in direct_yuv_lut.hpp) is populated
// by the production code in direct_yuv_converter.cpp via SrgbLutInit.
// ============================================================================

#include "reference_yuv_converter.hpp"

namespace chronon3d::video {

DirectYuvResult reference_convert_to_yuv420p(const DirectYuvRequest& req) {
    const int w = req.width;
    const int h = req.height;

    if (w <= 0 || h <= 0 || w % 2 != 0 || h % 2 != 0)
        return DirectYuvResult{};
    if (!req.planes.y || !req.planes.u || !req.planes.v)
        return DirectYuvResult{};

    const Framebuffer& src       = req.src;
    const int          src_stride = src.allocated_width();
    const Color*       src_data   = src.data();
    const auto&        coeffs     = get_coeffs(req.matrix);
    const bool         apply_gamma = req.apply_gamma;

    const int stride_y = req.planes.stride_y ? req.planes.stride_y : w;
    const int stride_u = req.planes.stride_u ? req.planes.stride_u : (w / 2);
    const int stride_v = req.planes.stride_v ? req.planes.stride_v : (w / 2);

    for (int block = 0; block < h / 2; ++block) {
        const int y = block * 2;

        const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
        const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

        uint8_t* y0 = req.planes.y + static_cast<size_t>(y) * stride_y;
        uint8_t* y1 = req.planes.y + static_cast<size_t>(y + 1) * stride_y;
        uint8_t* u_row = req.planes.u + static_cast<size_t>(block) * stride_u;
        uint8_t* v_row = req.planes.v + static_cast<size_t>(block) * stride_v;

        for (int x = 0; x < w; x += 2) {
            auto to8 = [&](float v) -> uint8_t {
                return apply_gamma
                    ? linear_to_srgb8_fast(v)
                    : static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            const uint8_t r00 = to8(src_row0[x].r),     g00 = to8(src_row0[x].g),     b00 = to8(src_row0[x].b);
            const uint8_t r01 = to8(src_row0[x+1].r),   g01 = to8(src_row0[x+1].g),   b01 = to8(src_row0[x+1].b);
            const uint8_t r10 = to8(src_row1[x].r),     g10 = to8(src_row1[x].g),     b10 = to8(src_row1[x].b);
            const uint8_t r11 = to8(src_row1[x+1].r),   g11 = to8(src_row1[x+1].g),   b11 = to8(src_row1[x+1].b);

            const YuvPixel yuv00 = rgb8_to_yuv(r00, g00, b00, coeffs);
            const YuvPixel yuv01 = rgb8_to_yuv(r01, g01, b01, coeffs);
            const YuvPixel yuv10 = rgb8_to_yuv(r10, g10, b10, coeffs);
            const YuvPixel yuv11 = rgb8_to_yuv(r11, g11, b11, coeffs);

            y0[x]     = yuv00.y;
            y0[x + 1] = yuv01.y;
            y1[x]     = yuv10.y;
            y1[x + 1] = yuv11.y;

            const int ux = x / 2;
            u_row[ux] = static_cast<uint8_t>(
                (static_cast<unsigned>(yuv00.u) + yuv01.u + yuv10.u + yuv11.u + 2) / 4);
            v_row[ux] = static_cast<uint8_t>(
                (static_cast<unsigned>(yuv00.v) + yuv01.v + yuv10.v + yuv11.v + 2) / 4);
        }
    }

    return DirectYuvResult{
        .success = true,
        .backend = FrameConversionBackend::Unavailable,
        .error   = ConversionError::None,
    };
}

DirectYuvResult reference_convert_to_nv12(const DirectYuvRequest& req) {
    const int w = req.width;
    const int h = req.height;

    if (w <= 0 || h <= 0 || w % 2 != 0 || h % 2 != 0)
        return DirectYuvResult{
            .success = false, .backend = FrameConversionBackend::Unavailable,
            .error = ConversionError::OddDims,
        };
    if (!req.planes.y || !req.planes.uv)
        return DirectYuvResult{
            .success = false, .backend = FrameConversionBackend::Unavailable,
            .error = ConversionError::NullPointer,
        };

    const Framebuffer& src       = req.src;
    const int          src_stride = src.allocated_width();
    const Color*       src_data   = src.data();
    const auto&        coeffs     = get_coeffs(req.matrix);
    const bool         apply_gamma = req.apply_gamma;

    const int stride_y  = req.planes.stride_y  ? req.planes.stride_y  : w;
    const int stride_uv = req.planes.stride_uv ? req.planes.stride_uv : w;

    for (int block = 0; block < h / 2; ++block) {
        const int y = block * 2;

        const Color* src_row0 = src_data + static_cast<size_t>(y) * src_stride;
        const Color* src_row1 = src_data + static_cast<size_t>(y + 1) * src_stride;

        uint8_t* y0 = req.planes.y + static_cast<size_t>(y) * stride_y;
        uint8_t* y1 = req.planes.y + static_cast<size_t>(y + 1) * stride_y;
        uint8_t* uv_row = req.planes.uv + static_cast<size_t>(block) * stride_uv;

        for (int x = 0; x < w; x += 2) {
            auto to8 = [&](float v) -> uint8_t {
                return apply_gamma
                    ? linear_to_srgb8_fast(v)
                    : static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            const uint8_t r00 = to8(src_row0[x].r),     g00 = to8(src_row0[x].g),     b00 = to8(src_row0[x].b);
            const uint8_t r01 = to8(src_row0[x+1].r),   g01 = to8(src_row0[x+1].g),   b01 = to8(src_row0[x+1].b);
            const uint8_t r10 = to8(src_row1[x].r),     g10 = to8(src_row1[x].g),     b10 = to8(src_row1[x].b);
            const uint8_t r11 = to8(src_row1[x+1].r),   g11 = to8(src_row1[x+1].g),   b11 = to8(src_row1[x+1].b);

            const YuvPixel yuv00 = rgb8_to_yuv(r00, g00, b00, coeffs);
            const YuvPixel yuv01 = rgb8_to_yuv(r01, g01, b01, coeffs);
            const YuvPixel yuv10 = rgb8_to_yuv(r10, g10, b10, coeffs);
            const YuvPixel yuv11 = rgb8_to_yuv(r11, g11, b11, coeffs);

            y0[x]     = yuv00.y;
            y0[x + 1] = yuv01.y;
            y1[x]     = yuv10.y;
            y1[x + 1] = yuv11.y;

            const int uvx = x;
            uv_row[uvx]     = static_cast<uint8_t>(
                (static_cast<unsigned>(yuv00.u) + yuv01.u + yuv10.u + yuv11.u + 2) / 4);
            uv_row[uvx + 1] = static_cast<uint8_t>(
                (static_cast<unsigned>(yuv00.v) + yuv01.v + yuv10.v + yuv11.v + 2) / 4);
        }
    }

    return DirectYuvResult{
        .success = true,
        .backend = FrameConversionBackend::Unavailable,
        .error   = ConversionError::None,
    };
}

} // namespace chronon3d::video
