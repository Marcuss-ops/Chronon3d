#include <chronon3d/media/frame_conversion/direct_yuv_converter.hpp>
#include <chronon3d/media/frame_conversion/direct_yuv_lut.hpp>
#include "internal/yuv_conversion_internal.hpp"

#include <cstdint>

namespace chronon3d::video {

// ============================================================================
//  sRGB gamma LUT — 8-bit linear→sRGB lookup table.
//  Defined at namespace scope (NOT anonymous) so the HWY SIMD variant in
//  direct_yuv_converter_hwy.cpp can link against it via the extern
//  declaration in direct_yuv_lut.hpp.
// ============================================================================

alignas(64) uint8_t g_srgb_lut[65536];
alignas(64) int32_t g_srgb_lut_u32[65536];
bool g_srgb_lut_ready = false;

namespace {
struct SrgbLutInit {
    SrgbLutInit() {
        for (int i = 0; i < 65536; ++i) {
            const float v = static_cast<float>(i) / 65535.0f;
            const uint8_t val = Color::linear_to_srgb8(v);
            g_srgb_lut[i] = val;
            g_srgb_lut_u32[i] = val;
        }
        g_srgb_lut_ready = true;
    }
};
static SrgbLutInit s_lut_init;
} // anonymous namespace

// ============================================================================
//  LUT is populated once at static init — still used by the Highway path.
//  The scalar/TBB fallback functions (convert_to_yuv420p_parallel,
//  convert_to_nv12_parallel) are removed in PR3.
// ============================================================================

// ============================================================================
//  Public API — dispatch: try HWY SIMD first, no scalar fallback.
//  (Highway failure is handled by the caller — convert_frame falls back
//   to swscale when convert_framebuffer_to_yuv_direct returns !success.)
// ============================================================================

DirectYuvResult convert_framebuffer_to_yuv_direct(const DirectYuvRequest& req) {
    if (req.width <= 0 || req.height <= 0) return DirectYuvResult{};
    if (req.width % 2 != 0 || req.height % 2 != 0) return DirectYuvResult{};

    if (req.matrix == YuvMatrix::BT2020) {
        return DirectYuvResult{
            .success = false,
            .backend = FrameConversionBackend::Unavailable,
            .error   = ConversionError::UnsupportedMatrix,
        };
    }

    switch (req.format) {
        case EncoderPixelFormat::YUV420P:
            return convert_to_yuv420p_hwy(req);
        case EncoderPixelFormat::NV12:
            return convert_to_nv12_hwy(req);
        default:
            return DirectYuvResult{
                .success = false,
                .backend = FrameConversionBackend::Unavailable,
                .error   = ConversionError::UnsupportedFormat,
            };
    }
}

} // namespace chronon3d::video
