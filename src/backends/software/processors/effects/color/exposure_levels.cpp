// ---------------------------------------------------------------------------
// exposure_levels.cpp — Exposure + Levels scalar processors
// ---------------------------------------------------------------------------
//
// Reference scalar implementations. These serve as the norm — any SIMD
// variant must produce bit-identical (or within 1e-5) results.
//
// Exposure is implemented as a thin wrapper over ComposeOp::Multiply
// (ComposeColorOp abstraction).

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/effects/compose_color_op.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

// =============================================================================
// Exposure
// =============================================================================
// Multiplies RGB by 2^stops using ComposeOp::Multiply. Alpha is preserved.

void apply_exposure(Framebuffer& fb, float stops,
                    const std::optional<raster::BBox>& clip) {
    if (stops == 0.0f) return;  // identity

    const float mult = std::exp2(stops);
    color::StraightRgb blend{mult, mult, mult};
    apply_compose_to_buffer(fb, ComposeOp::Multiply, blend, clip);
}

// =============================================================================
// Levels
// =============================================================================

namespace detail {

[[nodiscard]] inline float apply_levels_channel(float x,
                                                  float input_black,
                                                  float input_white,
                                                  float gamma,
                                                  float output_black,
                                                  float output_white) noexcept {
    const float range = std::max(input_white - input_black, 1.0e-6f);
    float n = (x - input_black) / range;

    // Signed extrapolation: preserves negative values and HDR
    const float shaped = (n >= 0.0f)
        ? std::pow(n, 1.0f / std::max(gamma, 1.0e-6f))
        : -std::pow(-n, 1.0f / std::max(gamma, 1.0e-6f));

    return output_black + shaped * (output_white - output_black);
}

} // namespace detail

void apply_levels(Framebuffer& fb,
                  float master_in_black, float master_in_white,
                  float master_gamma,
                  float master_out_black, float master_out_white,
                  float red_in_black, float red_in_white,
                  float red_gamma,
                  float red_out_black, float red_out_white,
                  float green_in_black, float green_in_white,
                  float green_gamma,
                  float green_out_black, float green_out_white,
                  float blue_in_black, float blue_in_white,
                  float blue_gamma,
                  float blue_out_black, float blue_out_white,
                  const std::optional<raster::BBox>& clip) {

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    using detail::apply_levels_channel;

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            auto srgb = color::unpremultiply_rgb(c);

            // Apply per-channel levels first
            float r = apply_levels_channel(srgb.r,
                                           red_in_black, red_in_white, red_gamma,
                                           red_out_black, red_out_white);
            float g = apply_levels_channel(srgb.g,
                                           green_in_black, green_in_white, green_gamma,
                                           green_out_black, green_out_white);
            float b = apply_levels_channel(srgb.b,
                                           blue_in_black, blue_in_white, blue_gamma,
                                           blue_out_black, blue_out_white);

            // Apply master levels
            r = apply_levels_channel(r,
                                     master_in_black, master_in_white, master_gamma,
                                     master_out_black, master_out_white);
            g = apply_levels_channel(g,
                                     master_in_black, master_in_white, master_gamma,
                                     master_out_black, master_out_white);
            b = apply_levels_channel(b,
                                     master_in_black, master_in_white, master_gamma,
                                     master_out_black, master_out_white);

            row[x] = color::premultiply_rgb({r, g, b}, c.a);
        }
    }
}

} // namespace chronon3d::renderer
