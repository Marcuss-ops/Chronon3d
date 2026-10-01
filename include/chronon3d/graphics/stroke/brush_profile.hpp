#pragma once

#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace chronon3d::graphics {

/// Brush profile governing artistic stroke rasterization, grain, jitter, and bleed.
/// Powers highlighters, felt pens, markers, chalk/pencil, and scribble underlines
/// through a single unified primitive.
struct BrushProfile {
    float edge_softness{0.15f};
    float grain_strength{0.0f};
    float grain_scale{1.0f};

    float width_jitter{0.0f};
    float opacity_jitter{0.0f};

    float bleed_radius{0.0f};
    float edge_density{1.0f};

    std::uint32_t seed{0};
    std::optional<Field2D> texture{std::nullopt};

    /// Evaluates stroke cross-section alpha at normalized distance d in [0, 1]
    /// (where 0 is path center line, 1 is nominal stroke edge) and path parameter t in [0, 1].
    [[nodiscard]] float sample_alpha(float distance_normalized, float t_along_path,
                                     Vec2 coordinate_px = Vec2{0.0f, 0.0f}) const noexcept {
        if (!std::isfinite(distance_normalized) || !std::isfinite(t_along_path))
            return 0.0f;

        float d = std::abs(distance_normalized);

        // Width jitter modulation along path
        if (width_jitter > 0.001f) {
            const float jitter_freq = 12.0f;
            const float hash_val = std::sin(t_along_path * jitter_freq * 6.2831853f + static_cast<float>(seed));
            const float width_factor = 1.0f + hash_val * width_jitter * 0.5f;
            if (width_factor > 1.0e-4f) {
                d /= width_factor;
            }
        }

        // Nominal edge profile
        float alpha = 1.0f;
        const float softness = std::clamp(edge_softness, 0.001f, 1.0f);
        if (d > (1.0f - softness)) {
            const float norm_t = (d - (1.0f - softness)) / softness;
            const float clamped_t = std::clamp(norm_t, 0.0f, 1.0f);
            alpha = 1.0f - (clamped_t * clamped_t * (3.0f - 2.0f * clamped_t));
        } else if (d > 1.0f) {
            // Bleed zone outside nominal edge
            if (bleed_radius > 0.001f && d < (1.0f + bleed_radius)) {
                const float bleed_t = (d - 1.0f) / bleed_radius;
                alpha = (1.0f - bleed_t) * 0.2f;
            } else {
                return 0.0f;
            }
        }

        // Edge density accumulation (e.g. marker pooling along stroke borders)
        if (edge_density > 1.001f && d > 0.6f && d <= 1.0f) {
            const float edge_t = (d - 0.6f) / 0.4f;
            alpha *= (1.0f + (edge_density - 1.0f) * edge_t);
        }

        // Opacity jitter along path
        if (opacity_jitter > 0.001f) {
            const float op_hash = std::cos(t_along_path * 23.0f * 6.2831853f + static_cast<float>(seed ^ 0x9e3779b9U));
            alpha *= std::clamp(1.0f - opacity_jitter * 0.5f + op_hash * opacity_jitter * 0.5f, 0.0f, 1.0f);
        }

        // Paper grain / texture modulation
        if (grain_strength > 0.001f) {
            float grain_val = 0.5f;
            if (texture.has_value()) {
                grain_val = texture->sample(coordinate_px * (grain_scale * 0.05f));
            } else {
                // Procedural micro-grain
                const float gx = std::sin(coordinate_px.x * grain_scale * 0.73f + static_cast<float>(seed));
                const float gy = std::cos(coordinate_px.y * grain_scale * 0.91f + static_cast<float>(seed ^ 0x85ebca6bU));
                grain_val = 0.5f + 0.5f * (gx * gy);
            }
            alpha *= (1.0f - grain_strength * (1.0f - grain_val));
        }

        return std::clamp(alpha, 0.0f, 1.0f);
    }

    // ── Canonical Brush Presets ──

    [[nodiscard]] static BrushProfile highlighter(std::uint32_t seed = 42) noexcept {
        BrushProfile p;
        p.edge_softness = 0.25f;
        p.grain_strength = 0.12f;
        p.grain_scale = 1.2f;
        p.width_jitter = 0.08f;
        p.opacity_jitter = 0.05f;
        p.bleed_radius = 0.04f;
        p.edge_density = 1.35f; // marker edge accumulation
        p.seed = seed;
        return p;
    }

    [[nodiscard]] static BrushProfile felt_pen(std::uint32_t seed = 101) noexcept {
        BrushProfile p;
        p.edge_softness = 0.12f;
        p.grain_strength = 0.08f;
        p.grain_scale = 0.8f;
        p.width_jitter = 0.04f;
        p.opacity_jitter = 0.03f;
        p.bleed_radius = 0.02f;
        p.edge_density = 1.10f;
        p.seed = seed;
        return p;
    }

    [[nodiscard]] static BrushProfile pencil_chalk(std::uint32_t seed = 777) noexcept {
        BrushProfile p;
        p.edge_softness = 0.40f;
        p.grain_strength = 0.45f; // heavy chalk dust grain
        p.grain_scale = 2.5f;
        p.width_jitter = 0.18f;
        p.opacity_jitter = 0.22f;
        p.bleed_radius = 0.0f;
        p.edge_density = 0.90f;
        p.seed = seed;
        return p;
    }

    [[nodiscard]] static BrushProfile scribble(std::uint32_t seed = 1337) noexcept {
        BrushProfile p;
        p.edge_softness = 0.18f;
        p.grain_strength = 0.15f;
        p.grain_scale = 1.0f;
        p.width_jitter = 0.25f; // high variation
        p.opacity_jitter = 0.15f;
        p.bleed_radius = 0.03f;
        p.edge_density = 1.15f;
        p.seed = seed;
        return p;
    }
};

} // namespace chronon3d::graphics
