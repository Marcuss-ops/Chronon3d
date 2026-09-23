#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// render_plan/color_utils.hpp — shared hex-color parsing for layer styles
//
// ONE parser for every style-consuming path in render-plan compilation. The
// text materializer (visual_preset_materializer.cpp) and the subtitle track
// (render_plan_compiler.cpp SubtitleTrack case) share this exact function so
// a custom fill color is lowered identically on both paths — no per-path
// drift in hex parsing or alpha handling.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace chronon3d::render_plan {

/// Canonical IEC 61966-2-1 transfer functions for display-referred sRGB data.
[[nodiscard]] inline float srgb_to_linear(float value) noexcept {
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float linear_to_srgb(float value) noexcept {
    value = std::max(0.0f, value);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

/// Parse a CSS/sRGB "#RRGGBB" (exactly 7 chars) into Chronon's canonical
/// linear working-domain Color at the given alpha. The conversion happens at
/// the authoring/lowering boundary, once, before the value reaches Vulkan,
/// CUDA or the software renderer. Returns std::nullopt for any other shape —
/// the caller keeps its own default color. Mirrors the CSS-style hex contract
/// of the layer `style` block (LayerStylePlan::fill / ShadowStyle::color).
inline std::optional<chronon3d::Color> parse_hex_color(
    std::string_view value, float alpha = 1.0f) {
    if (value.size() != 7 || value.front() != '#') return std::nullopt;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const int r1 = hex(value[1]), r2 = hex(value[2]);
    const int g1 = hex(value[3]), g2 = hex(value[4]);
    const int b1 = hex(value[5]), b2 = hex(value[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
        return std::nullopt;
    return chronon3d::Color{
        srgb_to_linear(static_cast<float>(r1 * 16 + r2) / 255.0f),
        srgb_to_linear(static_cast<float>(g1 * 16 + g2) / 255.0f),
        srgb_to_linear(static_cast<float>(b1 * 16 + b2) / 255.0f),
        std::clamp(alpha, 0.0f, 1.0f)};
}

} // namespace chronon3d::render_plan
