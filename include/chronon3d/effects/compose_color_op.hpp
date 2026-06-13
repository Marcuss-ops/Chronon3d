#pragma once

// ── compose_color_op.hpp — Composit color operations ───────────────────────
//
// ComposeColorOp generalises per-pixel colour compositing operations:
//   Multiply, Screen, Overlay, Add, Subtract, Difference.
//
// Each operation combines an input StraightRgb value with a blend
// colour (the "layer" or "effect colour") and returns the result in
// straight (non-premultiplied) linear RGB.
//
// Exposure is a thin wrapper: apply_exposure(stops) = ComposeOp::Multiply
// with blend colour = (2^stops, 2^stops, 2^stops).

#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <optional>
#include <algorithm>
#include <cmath>

namespace chronon3d {

/// Compositing operation type.
enum class ComposeOp : uint8_t {
    Multiply,   // src * blend       — used by Exposure
    Screen,     // 1 - (1-src)*(1-blend)
    Overlay,    // blend < 0.5 ? 2*src*blend : 1 - 2*(1-src)*(1-blend)
    Add,        // src + blend
    Subtract,   // src - blend
    Difference, // abs(src - blend)
    Divide,     // src / blend
    DarkenOnly, // min(src, blend)
    LightenOnly // max(src, blend)
};

/// Apply a single compositing operation to a straight-RGB value.
/// `src` is the input straight (non-premultiplied) RGB.
/// `blend` is the effect colour (straight RGB, no alpha).
/// Returns the straight-RGB result — no clamping is applied,
/// allowing HDR values to pass through.
[[nodiscard]] inline color::StraightRgb apply_compose_op(
    color::StraightRgb src, ComposeOp op, color::StraightRgb blend) noexcept
{
    switch (op) {
    case ComposeOp::Multiply:
        return {src.r * blend.r, src.g * blend.g, src.b * blend.b};

    case ComposeOp::Screen:
        return {
            1.0f - (1.0f - src.r) * (1.0f - blend.r),
            1.0f - (1.0f - src.g) * (1.0f - blend.g),
            1.0f - (1.0f - src.b) * (1.0f - blend.b)
        };

    case ComposeOp::Overlay: {
        auto ov = [](float s, float b) -> float {
            return b < 0.5f ? 2.0f * s * b : 1.0f - 2.0f * (1.0f - s) * (1.0f - b);
        };
        return {ov(src.r, blend.r), ov(src.g, blend.g), ov(src.b, blend.b)};
    }

    case ComposeOp::Add:
        return {src.r + blend.r, src.g + blend.g, src.b + blend.b};

    case ComposeOp::Subtract:
        return {src.r - blend.r, src.g - blend.g, src.b - blend.b};

    case ComposeOp::Difference:
        return {
            std::abs(src.r - blend.r),
            std::abs(src.g - blend.g),
            std::abs(src.b - blend.b)
        };

    case ComposeOp::Divide: {
        auto div = [](float s, float b) -> float {
            constexpr float eps = 1.0e-10f;
            return std::abs(b) < eps ? s : s / b;
        };
        return {div(src.r, blend.r), div(src.g, blend.g), div(src.b, blend.b)};
    }

    case ComposeOp::DarkenOnly:
        return {
            std::min(src.r, blend.r),
            std::min(src.g, blend.g),
            std::min(src.b, blend.b)
        };

    case ComposeOp::LightenOnly:
        return {
            std::max(src.r, blend.r),
            std::max(src.g, blend.g),
            std::max(src.b, blend.b)
        };

    default:
        return src;
    }
}

/// Apply a compositing operation to an entire Framebuffer region.
/// Follows the color contract: unpremultiply → compose → premultiply.
inline void apply_compose_to_buffer(Framebuffer& fb, ComposeOp op,
                                     color::StraightRgb blend,
                                     const std::optional<raster::BBox>& clip = std::nullopt)
{
    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            auto srgb = color::unpremultiply_rgb(c);
            srgb = apply_compose_op(srgb, op, blend);
            row[x] = color::premultiply_rgb(srgb, c.a);
        }
    }
}

} // namespace chronon3d
