#pragma once

#include <chronon3d/effects/effect_params.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace chronon3d::effects {

/// Evaluate the software Keying effect's subject coverage for one straight,
/// linear-light pixel. This is shared by compositing and the CLI seed-matte
/// generator so both paths use the same threshold, softness, clip and choke.
[[nodiscard]] inline std::array<float, 2> keying_components(
    Color straight_linear_rgb, int x, int y, int width, int height,
    const KeyingParams& params) noexcept {
    const float r = straight_linear_rgb.r;
    const float g = straight_linear_rgb.g;
    const float b = straight_linear_rgb.b;
    float measure = 0.0f;
    if (params.mode == KeyingMode::Chroma) {
        const float dr = r - params.key_color.r;
        const float dg = g - params.key_color.g;
        const float db = b - params.key_color.b;
        measure = std::sqrt(dr * dr + dg * dg + db * db) * 0.57735026919f;
    } else if (params.mode == KeyingMode::Luma) {
        measure = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
    float keep = 0.0f;
    if (params.softness <= 0.0f)
        keep = measure >= params.threshold ? 1.0f : 0.0f;
    else {
        const float t = std::clamp(
            (measure - params.threshold) / params.softness, 0.0f, 1.0f);
        keep = t * t * (3.0f - 2.0f * t);
    }
    keep = std::clamp((keep - params.clip_black) /
        std::max(params.clip_white - params.clip_black, 1.0e-6f), 0.0f, 1.0f);
    if (params.choke > 0.0f)
        keep = std::clamp((keep - params.choke) / (1.0f - params.choke), 0.0f, 1.0f);

    float garbage = 1.0f;
    if (params.garbage_w > 0.0f && params.garbage_h > 0.0f) {
        const float u = (static_cast<float>(x) + 0.5f) /
            static_cast<float>(std::max(width, 1));
        const float v = (static_cast<float>(y) + 0.5f) /
            static_cast<float>(std::max(height, 1));
        const float dx = std::min(u - params.garbage_x,
            params.garbage_x + params.garbage_w - u);
        const float dy = std::min(v - params.garbage_y,
            params.garbage_y + params.garbage_h - v);
        const float distance = std::min(dx, dy);
        garbage = params.garbage_feather > 0.0f
            ? std::clamp(distance / params.garbage_feather, 0.0f, 1.0f)
            : (distance >= 0.0f ? 1.0f : 0.0f);
    }
    return {keep, garbage};
}

[[nodiscard]] inline float keying_coverage(
    Color straight_linear_rgba, int x, int y, int width, int height,
    const KeyingParams& params) noexcept {
    const auto [keep, garbage] = keying_components(
        straight_linear_rgba, x, y, width, height, params);
    return std::clamp(straight_linear_rgba.a, 0.0f, 1.0f) * keep * garbage;
}

} // namespace chronon3d::effects
