#pragma once

#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace chronon3d::graphics {

/// Pressure samples are normalized by path distance and linearly interpolated.
struct BrushPressurePoint {
    float position{0.0f};
    float pressure{1.0f};
};

/// Profile data shared by all brush strokes; stroke width remains the path's
/// authored base diameter. All values are deterministic functions of profile,
/// path progress, and world-independent pixel coordinates.
struct BrushDab {
    Vec2 position{0.0f, 0.0f};
    float progress{0.0f};
    float pressure{1.0f};
    float angle{0.0f};
};

struct BrushProfile {
    float size{0.0f}; // diameter in pixels; zero inherits PathStroke.width
    float hardness{0.85f};
    float opacity{1.0f};
    float flow{1.0f};
    float spacing{0.1f}; // fraction of brush diameter per dab
    float roundness{1.0f};
    float angle{0.0f}; // radians
    float taper_start{1.0f};
    float taper_end{1.0f};
    float pressure_width{1.0f};
    float pressure_opacity{1.0f};
    float opacity_start{1.0f};
    float opacity_end{1.0f};
    float bleed_radius{0.0f};
    float edge_density{1.0f};
    float scatter{0.0f};
    float edge_noise{0.0f};
    float angle_jitter{0.0f};

    // Retained artistic controls from the original profile contract.
    float edge_softness{0.15f};
    float grain_strength{0.0f};
    float grain_scale{1.0f};
    float width_jitter{0.0f};
    float opacity_jitter{0.0f};
    std::uint32_t seed{0};
    std::optional<Field2D> texture{std::nullopt};
    std::optional<GradientDefinition> along_path_gradient{std::nullopt};
    std::vector<BrushPressurePoint> pressure_curve;

    [[nodiscard]] bool valid() const noexcept {
        const auto unit = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; };
        if (!std::isfinite(size) || size < 0.0f || size > 1024.0f ||
            !unit(hardness) || !unit(opacity) || !unit(flow) ||
            !std::isfinite(spacing) || spacing < 0.01f || spacing > 1.0f ||
            !unit(roundness) || roundness <= 0.0f || !std::isfinite(angle) || !unit(angle_jitter) ||
            !unit(taper_start) || !unit(taper_end) || !unit(opacity_start) || !unit(opacity_end) ||
            !std::isfinite(pressure_width) || pressure_width < 0.0f || pressure_width > 4.0f ||
            !std::isfinite(pressure_opacity) || pressure_opacity < 0.0f || pressure_opacity > 4.0f ||
            !unit(scatter) || !unit(edge_noise) || !unit(edge_softness) ||
            !unit(grain_strength) || !std::isfinite(grain_scale) || grain_scale <= 0.0f || grain_scale > 4096.0f ||
            !unit(width_jitter) || !unit(opacity_jitter) ||
            !std::isfinite(bleed_radius) || bleed_radius < 0.0f || bleed_radius > 1.0f ||
            !std::isfinite(edge_density) || edge_density < 0.0f || edge_density > 4.0f ||
            pressure_curve.size() > 256 || (texture && texture->operators.size() > 8))
            return false;
        float previous = -1.0f;
        for (const auto& point : pressure_curve) {
            if (!unit(point.position) || !unit(point.pressure) || point.position <= previous)
                return false;
            previous = point.position;
        }
        if (along_path_gradient) {
            const auto& gradient = *along_path_gradient;
            if (gradient.type != GradientType::Linear || gradient.color_stops.size() < 2 ||
                !std::isfinite(gradient.start.x) || !std::isfinite(gradient.start.y) ||
                !std::isfinite(gradient.end.x) || !std::isfinite(gradient.end.y) ||
                !std::isfinite(gradient.center.x) || !std::isfinite(gradient.center.y) ||
                !std::isfinite(gradient.radius) || gradient.radius <= 0.0f ||
                !std::isfinite(gradient.angle) || gradient.color_stops.size() > 256 ||
                gradient.opacity_stops.size() > 256 ||
                static_cast<std::uint8_t>(gradient.spread) >
                    static_cast<std::uint8_t>(GradientSpread::Reflect))
                return false;
            float last_position = -1.0f;
            for (const auto& stop : gradient.color_stops) {
                if (!unit(stop.position) || stop.position < last_position ||
                    !unit(stop.color.r) || !unit(stop.color.g) ||
                    !unit(stop.color.b) || !unit(stop.color.a)) return false;
                last_position = stop.position;
            }
            last_position = -1.0f;
            for (const auto& stop : gradient.opacity_stops) {
                if (!unit(stop.position) || stop.position < last_position ||
                    !unit(stop.opacity)) return false;
                last_position = stop.position;
            }
        }
        if (texture && (texture->operators.size() > 8 ||
            static_cast<std::uint8_t>(texture->generator) >
                static_cast<std::uint8_t>(Field2DGenerator::Conic) ||
            !std::isfinite(texture->frequency) || texture->frequency <= 0.0f ||
            texture->frequency > 4096.0f || !std::isfinite(texture->persistence) ||
            texture->persistence < 0.0f || texture->persistence > 1.0f ||
            !std::isfinite(texture->lacunarity) || texture->lacunarity <= 0.0f ||
            texture->lacunarity > 8.0f ||
            (texture->generator == Field2DGenerator::Fractal &&
             (texture->octaves == 0 || texture->octaves > 16)) ||
            !std::isfinite(texture->transform.translation.x) ||
            !std::isfinite(texture->transform.translation.y) ||
            !std::isfinite(texture->transform.scale.x) ||
            !std::isfinite(texture->transform.scale.y) ||
            !std::isfinite(texture->transform.rotation_radians) ||
            !std::isfinite(texture->transform.shear.x) ||
            !std::isfinite(texture->transform.shear.y) ||
            !std::isfinite(texture->transform.pivot.x) ||
            !std::isfinite(texture->transform.pivot.y)))
            return false;
        if (texture) {
            for (const auto& op : texture->operators) {
                if (static_cast<std::uint8_t>(op.kind) >
                        static_cast<std::uint8_t>(Field2DOperatorKind::DomainWarp) ||
                    !std::isfinite(op.from) || !std::isfinite(op.to) ||
                    !std::isfinite(op.edge) || !std::isfinite(op.softness) ||
                    !std::isfinite(op.amount) || !std::isfinite(op.scalar) ||
                    !std::isfinite(op.scalar_b) || !std::isfinite(op.vec.x) ||
                    !std::isfinite(op.vec.y) || !std::isfinite(op.angle))
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] float pressure_at(float progress) const noexcept {
        if (pressure_curve.empty() || !std::isfinite(progress)) return 1.0f;
        const float t = std::clamp(progress, 0.0f, 1.0f);
        if (t <= pressure_curve.front().position) return pressure_curve.front().pressure;
        if (t >= pressure_curve.back().position) return pressure_curve.back().pressure;
        for (std::size_t i = 1; i < pressure_curve.size(); ++i) {
            const auto& b = pressure_curve[i];
            if (t <= b.position) {
                const auto& a = pressure_curve[i - 1];
                const float u = (t - a.position) / (b.position - a.position);
                return std::lerp(a.pressure, b.pressure, u);
            }
        }
        return 1.0f;
    }

    [[nodiscard]] static float brush_taper(float progress, float start, float end) noexcept {
        if (!std::isfinite(progress) || !std::isfinite(start) || !std::isfinite(end)) return 0.0f;
        const float t = std::clamp(progress, 0.0f, 1.0f);
        float factor = 1.0f;
        if (start < 1.0f && t < 0.25f) factor *= start + (1.0f - start) * (t / 0.25f);
        if (end < 1.0f && t > 0.75f) factor *= 1.0f - (1.0f - end) * ((t - 0.75f) / 0.25f);
        return std::clamp(factor, 0.0f, 1.0f);
    }

    [[nodiscard]] static std::size_t dab_count(float path_length, float diameter,
                                               float spacing_value) noexcept {
        if (!std::isfinite(path_length) || !std::isfinite(diameter) ||
            !std::isfinite(spacing_value) || path_length < 0.0f || diameter <= 0.0f ||
            spacing_value <= 0.0f)
            return 0;
        const double step = std::max(0.25, static_cast<double>(diameter) * spacing_value);
        const double intervals = std::ceil(static_cast<double>(path_length) / step);
        if (!std::isfinite(intervals) || intervals >= 10000000.0) return 0;
        return static_cast<std::size_t>(intervals) + 1;
    }

    /// Falloff is full-strength up to hardness, then smoothly reaches zero at
    /// the nominal edge. Returned alpha includes opacity and pressure response;
    /// flow is deliberately left to dab compositing.
    [[nodiscard]] float sample_alpha(float distance_normalized, float t_along_path,
                                     Vec2 coordinate_px = Vec2{0.0f, 0.0f},
                                     float pressure = 1.0f) const noexcept {
        if (!std::isfinite(distance_normalized) || !std::isfinite(t_along_path) ||
            !std::isfinite(pressure) || !std::isfinite(coordinate_px.x) ||
            !std::isfinite(coordinate_px.y)) return 0.0f;
        const float p = std::clamp(pressure, 0.0f, 1.0f);
        float d = std::abs(distance_normalized);
        const float width_factor = std::max(1.0e-4f,
            std::pow(p, std::max(0.0f, pressure_width)));
        if (width_jitter > 0.001f) {
            const float noise = std::sin(t_along_path * 75.3982237f + static_cast<float>(seed));
            d /= width_factor * std::max(0.01f, 1.0f + noise * width_jitter * 0.5f);
        } else {
            d /= width_factor;
        }
        if (edge_noise > 0.0f) {
            const float noise = std::sin(coordinate_px.x * 12.9898f +
                coordinate_px.y * 78.233f + static_cast<float>(seed) * 0.017f);
            d += (noise * 0.5f) * edge_noise * 0.2f;
        }

        // edge_softness is the legacy companion to hardness: honor the
        // softer of the two controls so existing profiles remain expressive.
        const float hard_edge = edge_softness > 0.0f
            ? std::min(hardness, 1.0f - edge_softness) : hardness;
        float alpha = 0.0f;
        if (hard_edge >= 1.0f) {
            alpha = d <= 1.0f ? 1.0f : 0.0f;
        } else if (d <= hard_edge) {
            alpha = 1.0f;
        } else if (d < 1.0f) {
            const float u = (d - hard_edge) / (1.0f - hard_edge);
            alpha = 1.0f - u * u * (3.0f - 2.0f * u);
        } else if (bleed_radius > 0.0f && d < 1.0f + bleed_radius) {
            alpha = 0.2f * (1.0f - (d - 1.0f) / bleed_radius);
        }
        if (edge_density > 1.0f && d > 0.6f && d <= 1.0f)
            alpha *= 1.0f + (edge_density - 1.0f) * ((d - 0.6f) / 0.4f);
        if (opacity_jitter > 0.001f) {
            const float noise = std::cos(t_along_path * 144.5133f +
                static_cast<float>(seed ^ 0x9e3779b9U));
            alpha *= std::clamp(1.0f - opacity_jitter * 0.5f + noise * opacity_jitter * 0.5f,
                                0.0f, 1.0f);
        }
        if (grain_strength > 0.001f) {
            float grain = 0.5f;
            if (texture) {
                grain = texture->sample(coordinate_px * (grain_scale * 0.05f));
            } else {
                const float gx = std::sin(coordinate_px.x * grain_scale * 0.73f + static_cast<float>(seed));
                const float gy = std::cos(coordinate_px.y * grain_scale * 0.91f +
                    static_cast<float>(seed ^ 0x85ebca6bU));
                grain = 0.5f + 0.5f * gx * gy;
            }
            alpha *= 1.0f - grain_strength * (1.0f - grain);
        }
        const float pressure_alpha = std::pow(p, std::max(0.0f, pressure_opacity));
        const float opacity_taper = std::lerp(opacity_start, opacity_end, std::clamp(t_along_path, 0.0f, 1.0f));
        return std::clamp(alpha * opacity * pressure_alpha * opacity_taper, 0.0f, 1.0f);
    }

    [[nodiscard]] bool requires_dabs() const noexcept {
        return grain_strength > 0.0f || width_jitter > 0.0f || opacity_jitter > 0.0f ||
               scatter > 0.0f || edge_noise > 0.0f || flow < 1.0f ||
               opacity < 1.0f || edge_density != 1.0f || bleed_radius > 0.0f ||
               roundness < 1.0f || angle != 0.0f || angle_jitter > 0.0f ||
               !pressure_curve.empty();
    }

    /// Conservative radial extent in pixels, including every supported edge
    /// expansion. The caller adds the rasterizer's antialiasing margin.
    [[nodiscard]] float maximum_extent_radius(float fallback_diameter) const noexcept {
        const float diameter = size > 0.0f ? size : fallback_diameter;
        if (!std::isfinite(diameter) || diameter <= 0.0f) return 0.0f;
        const float radius = diameter * 0.5f;
        const float minimum_width_factor = std::max(
            0.5f, 1.0f - 0.5f * std::clamp(width_jitter, 0.0f, 1.0f));
        return radius * (1.0f + std::max(0.0f, bleed_radius) +
                         0.1f * std::max(0.0f, edge_noise)) /
                         minimum_width_factor +
               radius * std::max(0.0f, scatter);
    }

    [[nodiscard]] float accumulate_flow(float dab_alpha, std::uint32_t deposits) const noexcept {
        if (!std::isfinite(dab_alpha) || deposits == 0) return 0.0f;
        const float deposit = std::clamp(dab_alpha, 0.0f, 1.0f) * std::clamp(flow, 0.0f, 1.0f);
        return std::min(std::clamp(opacity, 0.0f, 1.0f),
            1.0f - std::pow(1.0f - deposit, static_cast<float>(deposits)));
    }

    [[nodiscard]] std::uint64_t fingerprint() const noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value; hash *= 0x100000001b3ULL;
        };
        const auto add_float = [&](float value) { mix(std::bit_cast<std::uint32_t>(value)); };
        mix(valid());
        add_float(size); add_float(hardness); add_float(opacity); add_float(flow);
        add_float(spacing); add_float(roundness); add_float(angle);
        add_float(taper_start); add_float(taper_end); add_float(pressure_width);
        add_float(pressure_opacity); add_float(opacity_start); add_float(opacity_end);
        add_float(scatter); add_float(edge_noise);
        add_float(angle_jitter); add_float(edge_softness); add_float(grain_strength);
        add_float(grain_scale); add_float(width_jitter); add_float(opacity_jitter);
        add_float(bleed_radius); add_float(edge_density); mix(seed);
        // The legacy Field2D fingerprint predates its transform/operator tail;
        // add those fields here so distinct brush textures cannot alias.
        mix(texture.has_value());
        if (texture) {
            mix(field2d_fingerprint(*texture));
            mix(static_cast<std::uint8_t>(texture->generator));
            mix(texture->seed);
            add_float(texture->frequency);
            mix(texture->octaves);
            add_float(texture->persistence);
            add_float(texture->lacunarity);
            add_float(texture->transform.translation.x);
            add_float(texture->transform.translation.y);
            add_float(texture->transform.scale.x);
            add_float(texture->transform.scale.y);
            add_float(texture->transform.rotation_radians);
            add_float(texture->transform.shear.x);
            add_float(texture->transform.shear.y);
            add_float(texture->transform.pivot.x);
            add_float(texture->transform.pivot.y);
            mix(texture->operators.size());
            for (const auto& op : texture->operators) {
                mix(static_cast<std::uint8_t>(op.kind));
                add_float(op.from); add_float(op.to); add_float(op.edge);
                add_float(op.softness); add_float(op.amount); add_float(op.scalar);
                add_float(op.scalar_b); add_float(op.vec.x); add_float(op.vec.y);
                add_float(op.angle);
            }
        }
        mix(pressure_curve.size());
        for (const auto& point : pressure_curve) {
            add_float(point.position); add_float(point.pressure);
        }
        mix(along_path_gradient.has_value());
        if (along_path_gradient) {
            mix(static_cast<std::uint8_t>(along_path_gradient->type));
            mix(static_cast<std::uint8_t>(along_path_gradient->spread));
            add_float(along_path_gradient->start.x); add_float(along_path_gradient->start.y);
            add_float(along_path_gradient->end.x); add_float(along_path_gradient->end.y);
            add_float(along_path_gradient->center.x); add_float(along_path_gradient->center.y);
            add_float(along_path_gradient->radius); add_float(along_path_gradient->angle);
            mix(along_path_gradient->color_stops.size());
            for (const auto& stop : along_path_gradient->color_stops) {
                add_float(stop.position); add_float(stop.color.r); add_float(stop.color.g);
                add_float(stop.color.b); add_float(stop.color.a);
            }
            mix(along_path_gradient->opacity_stops.size());
            for (const auto& stop : along_path_gradient->opacity_stops) {
                add_float(stop.position); add_float(stop.opacity);
            }
        }
        return hash;
    }

    [[nodiscard]] std::vector<BrushDab> sample_dabs(
        const std::vector<Vec2>& points, float path_length, float base_diameter) const {
        std::vector<BrushDab> dabs;
        if (points.size() < 2 || !std::isfinite(path_length) || path_length <= 0.0f ||
            !std::isfinite(base_diameter) || base_diameter <= 0.0f ||
            !valid()) return dabs;
        for (const auto& point : points)
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) return dabs;
        const float diameter = size > 0.0f ? size : base_diameter;
        if (!std::isfinite(diameter) || diameter <= 0.0f) return dabs;
        const double step = std::max(0.25, static_cast<double>(diameter) * spacing);
        if (!std::isfinite(step) || step <= 0.0) return dabs;
        std::vector<float> cumulative(points.size(), 0.0f);
        for (std::size_t i = 1; i < points.size(); ++i) {
            const Vec2 delta = points[i] - points[i - 1];
            cumulative[i] = cumulative[i - 1] + std::sqrt(delta.x * delta.x + delta.y * delta.y);
        }
        const float actual_length = cumulative.back();
        if (!std::isfinite(actual_length) || actual_length <= 0.0f ||
            std::abs(actual_length - path_length) >
                std::max(1.0e-3f, actual_length * 1.0e-4f)) return dabs;
        const auto expected_count = dab_count(actual_length, diameter, spacing);
        if (expected_count == 0 || expected_count > 1000000) return dabs;
        dabs.reserve(expected_count);
        for (std::size_t i = 0; i < expected_count; ++i) {
            const float distance = i + 1 == expected_count ? actual_length :
                std::min(actual_length, static_cast<float>(
                    std::min(static_cast<double>(actual_length), static_cast<double>(i) * step)));
            const auto right = std::lower_bound(cumulative.begin(), cumulative.end(), distance);
            const std::size_t index = static_cast<std::size_t>(right - cumulative.begin());
            Vec2 position = points.back();
            if (index == 0) position = points.front();
            else if (index < points.size()) {
                const float span = cumulative[index] - cumulative[index - 1];
                const float u = span > 0.0f ? (distance - cumulative[index - 1]) / span : 0.0f;
                position = points[index - 1] + (points[index] - points[index - 1]) * u;
            }
            const float t = distance / actual_length;
            const float hash = random_unit(static_cast<std::uint32_t>(i) ^ seed);
            const float angle_hash = random_unit(static_cast<std::uint32_t>(i) ^ seed ^ 0xa511e9b3U);
            if (scatter > 0.0f) {
                const float radius = scatter * diameter * 0.5f;
                const float theta = hash * 6.28318530718f;
                const float amount = std::sqrt(angle_hash) * radius;
                position += Vec2{std::cos(theta), std::sin(theta)} * amount;
            }
            const float pressure = pressure_at(t);
            dabs.push_back({position, t, pressure,
                angle + (angle_hash * 2.0f - 1.0f) * angle_jitter * 3.14159265359f});
        }
        return dabs;
    }

    [[nodiscard]] float sample_dab_alpha(Vec2 offset, float radius,
                                         float progress, float pressure,
                                         float dab_angle = 0.0f,
                                         Vec2 coordinate_px = Vec2{0.0f, 0.0f}) const noexcept {
        const float angle_value = dab_angle;
        const float c = std::cos(angle_value), s = std::sin(angle_value);
        const Vec2 rotated{offset.x * c + offset.y * s,
                           -offset.x * s + offset.y * c};
        const float minor = std::max(1.0e-4f, roundness);
        const float distance = std::sqrt(rotated.x * rotated.x +
            (rotated.y / minor) * (rotated.y / minor));
        const float d = radius > 0.0f ? distance / radius : 2.0f;
        return sample_alpha(d, progress, coordinate_px, pressure);
    }

private:
    [[nodiscard]] static std::uint32_t hash32(std::uint32_t value) noexcept {
        value ^= value >> 16; value *= 0x7feb352dU;
        value ^= value >> 15; value *= 0x846ca68bU;
        return value ^ (value >> 16);
    }
    [[nodiscard]] static float random_unit(std::uint32_t value) noexcept {
        return static_cast<float>(hash32(value) >> 8) * (1.0f / 16777216.0f);
    }

public:
    // ── Canonical Brush Presets ──

    [[nodiscard]] static BrushProfile highlighter(std::uint32_t seed = 42) noexcept {
        BrushProfile p;
        p.size = 50.0f;
        p.hardness = 0.35f;
        p.opacity = 0.35f;
        p.flow = 0.30f;
        p.spacing = 0.08f;
        p.roundness = 0.35f;
        p.angle = -0.13962634f;
        p.edge_softness = 0.65f;
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
        p.hardness = 0.88f;
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
        p.hardness = 0.60f;
        p.flow = 0.55f;
        p.spacing = 0.12f;
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
        p.hardness = 0.82f;
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

    [[nodiscard]] static BrushProfile hard_round(std::uint32_t seed = 1) noexcept {
        BrushProfile p;
        p.size = 8.0f;
        p.hardness = 1.0f;
        p.edge_softness = 0.0f;
        p.spacing = 0.25f;
        p.seed = seed;
        return p;
    }
    [[nodiscard]] static BrushProfile soft_round(std::uint32_t seed = 2) noexcept {
        BrushProfile p;
        p.size = 24.0f;
        p.hardness = 0.0f;
        p.edge_softness = 1.0f;
        p.spacing = 0.12f;
        p.seed = seed;
        return p;
    }
    [[nodiscard]] static BrushProfile marker(std::uint32_t seed = 3) noexcept {
        BrushProfile p;
        p.size = 22.0f;
        p.hardness = 0.75f;
        p.opacity = 0.90f;
        p.flow = 0.75f;
        p.spacing = 0.08f;
        p.roundness = 0.85f;
        p.edge_softness = 0.25f;
        p.grain_strength = 0.12f;
        p.edge_noise = 0.08f;
        p.seed = seed;
        return p;
    }
    [[nodiscard]] static BrushProfile chalk(std::uint32_t seed = 4) noexcept {
        BrushProfile p = pencil_chalk(seed);
        p.size = 16.0f;
        p.opacity = 0.85f;
        return p;
    }
    [[nodiscard]] static BrushProfile pencil(std::uint32_t seed = 5) noexcept {
        BrushProfile p = pencil_chalk(seed);
        p.size = 0.25f;
        p.hardness = 0.78f;
        p.edge_softness = 0.22f;
        p.grain_strength = 0.35f;
        p.grain_scale = 3.0f;
        p.flow = 0.4f;
        return p;
    }
    [[nodiscard]] static BrushProfile dry_paint(std::uint32_t seed = 6) noexcept {
        BrushProfile p;
        p.size = 20.0f;
        p.hardness = 0.68f;
        p.edge_softness = 0.32f;
        p.flow = 0.45f;
        p.spacing = 0.18f;
        p.grain_strength = 0.55f;
        p.width_jitter = 0.14f;
        p.opacity_jitter = 0.28f;
        p.edge_noise = 0.25f;
        p.seed = seed;
        return p;
    }
    [[nodiscard]] static BrushProfile ink(std::uint32_t seed = 7) noexcept {
        BrushProfile p = hard_round(seed);
        p.size = 14.0f;
        p.hardness = 0.95f;
        p.edge_softness = 0.05f;
        p.taper_start = 0.8f;
        p.pressure_curve = {{0.0f, 0.15f}, {0.2f, 1.0f},
                            {0.8f, 1.0f}, {1.0f, 0.1f}};
        return p;
    }
    [[nodiscard]] static BrushProfile calligraphy(std::uint32_t seed = 8) noexcept {
        BrushProfile p = ink(seed);
        p.roundness = 0.18f;
        p.angle = -0.78539816f;
        p.spacing = 0.08f;
        return p;
    }
    [[nodiscard]] static BrushProfile glow(std::uint32_t seed = 9) noexcept {
        BrushProfile p = soft_round(seed);
        p.size = 18.0f;
        p.hardness = 0.12f;
        p.edge_softness = 0.88f;
        p.opacity = 0.65f;
        p.flow = 0.35f;
        p.spacing = 0.08f;
        return p;
    }
    [[nodiscard]] static BrushProfile highlighter_brush(std::uint32_t seed = 42) noexcept {
        BrushProfile p = highlighter(seed); p.opacity = 0.35f; return p;
    }
    [[nodiscard]] static BrushProfile felt(std::uint32_t seed = 101) noexcept {
        return felt_pen(seed);
    }
    [[nodiscard]] static BrushProfile brush_hard_round() noexcept { return hard_round(); }
    [[nodiscard]] static BrushProfile brush_soft_round() noexcept { return soft_round(); }
    [[nodiscard]] static BrushProfile brush_marker() noexcept { return marker(); }
    [[nodiscard]] static BrushProfile brush_highlighter() noexcept { return highlighter_brush(); }
    [[nodiscard]] static BrushProfile brush_felt() noexcept { return felt(); }
    [[nodiscard]] static BrushProfile brush_chalk() noexcept { return chalk(); }
    [[nodiscard]] static BrushProfile brush_pencil() noexcept { return pencil(); }
    [[nodiscard]] static BrushProfile brush_dry_paint() noexcept { return dry_paint(); }
    [[nodiscard]] static BrushProfile brush_ink() noexcept { return ink(); }
    [[nodiscard]] static BrushProfile brush_calligraphy() noexcept { return calligraphy(); }
    [[nodiscard]] static BrushProfile brush_glow() noexcept { return glow(); }
    [[nodiscard]] static BrushProfile brush_scribble() noexcept { return scribble(); }
};

} // namespace chronon3d::graphics
