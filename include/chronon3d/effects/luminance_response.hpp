#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace chronon3d {

enum class LuminanceBlendMode : std::uint8_t {
    Screen,
    Add,
    Over
};

/// Authority primitive for halation, anamorphic flares, selective glow, and highlight bleed.
/// Extracts pixels above luminance threshold, feathers with softness, blooms/blurs,
/// colorizes with tint, and composites via screen/add.
struct LuminanceResponseParams {
    float threshold{0.70f};      // [0, 1] luma trigger point
    float softness{0.20f};       // [0, 1] threshold transition ramp
    float radius{18.0f};         // [0, 256] bloom convolution radius in pixels
    Color tint{1.0f, 0.35f, 0.15f, 1.0f}; // Red/orange default for film halation
    float intensity{1.0f};       // [0, 8] gain multiplier
    LuminanceBlendMode blend{LuminanceBlendMode::Screen};

    void validate() const {
        if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f)
            throw std::invalid_argument("LuminanceResponse threshold must be in [0, 1]");
        if (!std::isfinite(softness) || softness < 0.0f || softness > 1.0f)
            throw std::invalid_argument("LuminanceResponse softness must be in [0, 1]");
        if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
            throw std::invalid_argument("LuminanceResponse radius must be in [0, 256]");
        if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f)
            throw std::invalid_argument("LuminanceResponse intensity must be in [0, 8]");
    }

    [[nodiscard]] static LuminanceResponseParams halation() noexcept {
        LuminanceResponseParams p;
        p.threshold = 0.75f;
        p.softness = 0.20f;
        p.radius = 22.0f;
        p.tint = Color{1.0f, 0.28f, 0.12f, 1.0f}; // warm reddish halation
        p.intensity = 1.2f;
        p.blend = LuminanceBlendMode::Screen;
        return p;
    }

    [[nodiscard]] static LuminanceResponseParams anamorphic_blue() noexcept {
        LuminanceResponseParams p;
        p.threshold = 0.82f;
        p.softness = 0.15f;
        p.radius = 48.0f;
        p.tint = Color{0.18f, 0.65f, 1.0f, 1.0f}; // classic sci-fi anamorphic streak
        p.intensity = 1.5f;
        p.blend = LuminanceBlendMode::Add;
        return p;
    }

    [[nodiscard]] static LuminanceResponseParams anamorphic_warm() noexcept {
        LuminanceResponseParams p;
        p.threshold = 0.80f;
        p.softness = 0.18f;
        p.radius = 44.0f;
        p.tint = Color{1.0f, 0.72f, 0.35f, 1.0f}; // golden vintage streak
        p.intensity = 1.4f;
        p.blend = LuminanceBlendMode::Add;
        return p;
    }

    [[nodiscard]] static LuminanceResponseParams selective_glow() noexcept {
        LuminanceResponseParams p;
        p.threshold = 0.60f;
        p.softness = 0.30f;
        p.radius = 28.0f;
        p.tint = Color{1.0f, 1.0f, 1.0f, 1.0f};
        p.intensity = 1.0f;
        p.blend = LuminanceBlendMode::Screen;
        return p;
    }
};

} // namespace chronon3d
