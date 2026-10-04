#pragma once

#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace chronon3d::graphics {

enum class Field2DGenerator : std::uint8_t {
    Perlin, Simplex, Fractal, Cellular, Grid, Checker, Dots, Stripes,
    Rings, Linear, Radial, Conic, Burst
};

enum class Field2DOperatorKind : std::uint8_t {
    Remap, Threshold, Smoothstep, Invert, Warp,
    Add, Subtract, Multiply, Min, Max, Abs, Power, Clamp,
    Translate, Scale, Rotate, Repeat, DomainWarp
};

struct Field2DOperator {
    Field2DOperatorKind kind{Field2DOperatorKind::Invert};
    float from{0.0f};
    float to{1.0f};
    float edge{0.5f};
    float softness{0.1f};
    float amount{0.05f};
    float scalar{0.0f};
    float scalar_b{1.0f};
    Vec2 vec{0.0f, 0.0f};
    float angle{0.0f};
};

/// 2D coordinate transformation basis for fields (translate, scale, rotate, shear).
struct FieldTransform2D {
    Vec2 translation{0.0f, 0.0f};
    Vec2 scale{1.0f, 1.0f};
    float rotation_radians{0.0f};
    Vec2 shear{0.0f, 0.0f};
    Vec2 pivot{0.5f, 0.5f};

    [[nodiscard]] Vec2 apply(Vec2 p) const noexcept {
        Vec2 pt = p - pivot;
        const float sx = (std::abs(scale.x) > 1.0e-7f) ? scale.x : 1.0f;
        const float sy = (std::abs(scale.y) > 1.0e-7f) ? scale.y : 1.0f;
        pt.x *= sx;
        pt.y *= sy;
        const float x_orig = pt.x;
        pt.x += pt.y * shear.x;
        pt.y += x_orig * shear.y;
        if (rotation_radians != 0.0f) {
            const float c = std::cos(rotation_radians);
            const float s = std::sin(rotation_radians);
            pt = Vec2{pt.x * c - pt.y * s, pt.x * s + pt.y * c};
        }
        return pt + pivot + translation;
    }
};

/// Deterministic, allocation-free scalar field in normalized coordinates.
/// All randomness is a pure function of seed and integer lattice coordinates.
struct Field2D {
    Field2DGenerator generator{Field2DGenerator::Perlin};
    std::uint32_t seed{0};
    float frequency{4.0f};
    std::uint32_t octaves{4};
    float persistence{0.5f};
    float lacunarity{2.0f};
    std::vector<Field2DOperator> operators;
    FieldTransform2D transform{};

    /// Return the field translated by normalized units per second at the
    /// supplied absolute time. Equal inputs produce an identical field,
    /// independent of the order in which frames are requested.
    [[nodiscard]] Field2D drifted(double time_seconds, Vec2 velocity) const {
        if (!std::isfinite(time_seconds) || std::abs(time_seconds) > 1.0e9 ||
            !std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
            std::abs(velocity.x) > 100.0f || std::abs(velocity.y) > 100.0f)
            throw std::invalid_argument("Field2D drift inputs are outside supported bounds");
        const double dx = static_cast<double>(velocity.x) * time_seconds;
        const double dy = static_cast<double>(velocity.y) * time_seconds;
        if (!std::isfinite(dx) || !std::isfinite(dy) ||
            std::abs(dx) > 1.0e6 || std::abs(dy) > 1.0e6)
            throw std::invalid_argument("Field2D drift exceeds supported coordinates");
        Field2D result = *this;
        result.transform.translation.x += static_cast<float>(dx);
        result.transform.translation.y += static_cast<float>(dy);
        return result;
    }

    [[nodiscard]] float sample(Vec2 point) const noexcept {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            std::abs(point.x) > 1.0e6f || std::abs(point.y) > 1.0e6f ||
            !std::isfinite(frequency) || frequency <= 0.0f ||
            std::abs(point.x * frequency) > 1.0e8f ||
            std::abs(point.y * frequency) > 1.0e8f)
            return 0.0f;
        if (generator == Field2DGenerator::Fractal &&
            (octaves == 0 || octaves > 16 || !std::isfinite(persistence) ||
             persistence < 0.0f || persistence > 1.0f ||
             !std::isfinite(lacunarity) || lacunarity <= 0.0f || lacunarity > 8.0f))
            return 0.0f;
        if (generator == Field2DGenerator::Burst && (octaves < 3 || octaves > 16))
            return 0.0f;

        point = transform.apply(point);
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return 0.0f;

        const auto base = [&](Vec2 p) {
            return sample_generator(p);
        };
        float value = base(point);
        for (std::size_t i = 0; i < operators.size(); ++i) {
            const auto& op = operators[i];
            switch (op.kind) {
                case Field2DOperatorKind::Remap: {
                    const float width = op.to - op.from;
                    value = std::abs(width) > 1.0e-7f
                        ? std::clamp((value - op.from) / width, 0.0f, 1.0f) : 0.0f;
                    break;
                }
                case Field2DOperatorKind::Threshold:
                    value = value >= op.edge ? 1.0f : 0.0f;
                    break;
                case Field2DOperatorKind::Smoothstep: {
                    const float width = std::max(op.softness, 1.0e-7f);
                    const float t = std::clamp((value - (op.edge - width * 0.5f)) / width,
                                               0.0f, 1.0f);
                    value = t * t * (3.0f - 2.0f * t);
                    break;
                }
                case Field2DOperatorKind::Invert:
                    value = 1.0f - value;
                    break;
                case Field2DOperatorKind::Add:
                    value += op.scalar;
                    break;
                case Field2DOperatorKind::Subtract:
                    value -= op.scalar;
                    break;
                case Field2DOperatorKind::Multiply:
                    value *= op.scalar;
                    break;
                case Field2DOperatorKind::Min:
                    value = std::min(value, op.scalar);
                    break;
                case Field2DOperatorKind::Max:
                    value = std::max(value, op.scalar);
                    break;
                case Field2DOperatorKind::Abs:
                    value = std::abs(value);
                    break;
                case Field2DOperatorKind::Power:
                    value = (value > 0.0f && op.scalar > 0.0f) ? std::pow(value, op.scalar) : (value <= 0.0f ? 0.0f : 1.0f);
                    break;
                case Field2DOperatorKind::Clamp:
                    value = std::clamp(value, op.scalar, op.scalar_b);
                    break;
                case Field2DOperatorKind::Translate: {
                    point += op.vec;
                    value = base(point);
                    for (std::size_t prior = 0; prior < i; ++prior)
                        value = apply_scalar_operator(value, operators[prior]);
                    break;
                }
                case Field2DOperatorKind::Scale: {
                    if (std::abs(op.vec.x) > 1.0e-7f) point.x *= op.vec.x;
                    if (std::abs(op.vec.y) > 1.0e-7f) point.y *= op.vec.y;
                    value = base(point);
                    for (std::size_t prior = 0; prior < i; ++prior)
                        value = apply_scalar_operator(value, operators[prior]);
                    break;
                }
                case Field2DOperatorKind::Rotate: {
                    const float c = std::cos(op.angle);
                    const float s = std::sin(op.angle);
                    const Vec2 p = point - Vec2{0.5f, 0.5f};
                    point = Vec2{p.x * c - p.y * s, p.x * s + p.y * c} + Vec2{0.5f, 0.5f};
                    value = base(point);
                    for (std::size_t prior = 0; prior < i; ++prior)
                        value = apply_scalar_operator(value, operators[prior]);
                    break;
                }
                case Field2DOperatorKind::Repeat: {
                    if (op.vec.x > 1.0e-7f) point.x = std::fmod(std::abs(point.x), op.vec.x);
                    if (op.vec.y > 1.0e-7f) point.y = std::fmod(std::abs(point.y), op.vec.y);
                    value = base(point);
                    for (std::size_t prior = 0; prior < i; ++prior)
                        value = apply_scalar_operator(value, operators[prior]);
                    break;
                }
                case Field2DOperatorKind::Warp:
                case Field2DOperatorKind::DomainWarp: {
                    const float x = lattice_noise(point * frequency + Vec2{17.13f, 3.71f}, seed ^ 0x68bc21ebU);
                    const float y = lattice_noise(point * frequency + Vec2{5.37f, 29.41f}, seed ^ 0x02e5be93U);
                    point += Vec2{x - 0.5f, y - 0.5f} * (2.0f * op.amount);
                    value = base(point);
                    for (std::size_t prior = 0; prior < i; ++prior)
                        value = apply_scalar_operator(value, operators[prior]);
                    break;
                }
            }
            if (!std::isfinite(value) || !std::isfinite(point.x) ||
                !std::isfinite(point.y) || std::abs(point.x) > 1.0e6f ||
                std::abs(point.y) > 1.0e6f ||
                std::abs(point.x * frequency) > 1.0e8f ||
                std::abs(point.y * frequency) > 1.0e8f)
                return 0.0f;
        }
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    }

private:
    [[nodiscard]] static float fade(float value) noexcept {
        return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
    }

    [[nodiscard]] static std::uint32_t hash(std::int32_t x, std::int32_t y,
                                            std::uint32_t seed_value) noexcept {
        std::uint64_t value = static_cast<std::uint64_t>(seed_value) ^
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) ^
            static_cast<std::uint32_t>(y);
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return static_cast<std::uint32_t>(value >> 32);
    }

    [[nodiscard]] static float unit(std::uint32_t value) noexcept {
        return static_cast<float>(value >> 8) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] static float lattice_noise(Vec2 p, std::uint32_t seed_value) noexcept {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
            std::abs(p.x) > 1.0e8f || std::abs(p.y) > 1.0e8f)
            return 0.5f;
        const auto ix = static_cast<std::int32_t>(std::floor(p.x));
        const auto iy = static_cast<std::int32_t>(std::floor(p.y));
        const float x = p.x - static_cast<float>(ix);
        const float y = p.y - static_cast<float>(iy);
        const float sx = fade(x), sy = fade(y);
        const float a = unit(hash(ix, iy, seed_value));
        const float b = unit(hash(ix + 1, iy, seed_value));
        const float c = unit(hash(ix, iy + 1, seed_value));
        const float d = unit(hash(ix + 1, iy + 1, seed_value));
        return std::lerp(std::lerp(a, b, sx), std::lerp(c, d, sx), sy);
    }

    [[nodiscard]] static float perlin(Vec2 p, std::uint32_t seed_value) noexcept {
        constexpr std::array<Vec2, 8> gradients{{
            {1, 0}, {-1, 0}, {0, 1}, {0, -1},
            {0.70710678f, 0.70710678f}, {-0.70710678f, 0.70710678f},
            {0.70710678f, -0.70710678f}, {-0.70710678f, -0.70710678f}}};
        const auto ix = static_cast<std::int32_t>(std::floor(p.x));
        const auto iy = static_cast<std::int32_t>(std::floor(p.y));
        const float x = p.x - static_cast<float>(ix), y = p.y - static_cast<float>(iy);
        const auto dot = [&](std::int32_t dx, std::int32_t dy, float ox, float oy) {
            const auto& g = gradients[hash(ix + dx, iy + dy, seed_value) & 7U];
            return g.x * (x - ox) + g.y * (y - oy);
        };
        const float sx = fade(x), sy = fade(y);
        const float a = std::lerp(dot(0, 0, 0, 0), dot(1, 0, 1, 0), sx);
        const float b = std::lerp(dot(0, 1, 0, 1), dot(1, 1, 1, 1), sx);
        return std::clamp(0.5f + 0.70710678f * std::lerp(a, b, sy), 0.0f, 1.0f);
    }

    [[nodiscard]] static float simplex(Vec2 p, std::uint32_t seed_value) noexcept {
        constexpr float f2 = 0.3660254037844386f;
        constexpr float g2 = 0.2113248654051871f;
        constexpr std::array<Vec2, 8> gradients{{
            {1, 0}, {-1, 0}, {0, 1}, {0, -1},
            {0.70710678f, 0.70710678f}, {-0.70710678f, 0.70710678f},
            {0.70710678f, -0.70710678f}, {-0.70710678f, -0.70710678f}}};
        const float skew = (p.x + p.y) * f2;
        const auto i = static_cast<std::int32_t>(std::floor(p.x + skew));
        const auto j = static_cast<std::int32_t>(std::floor(p.y + skew));
        const float unskew = static_cast<float>(i + j) * g2;
        const float x0 = p.x - (static_cast<float>(i) - unskew);
        const float y0 = p.y - (static_cast<float>(j) - unskew);
        const std::int32_t i1 = x0 > y0 ? 1 : 0;
        const std::int32_t j1 = x0 > y0 ? 0 : 1;
        const float x1 = x0 - static_cast<float>(i1) + g2;
        const float y1 = y0 - static_cast<float>(j1) + g2;
        const float x2 = x0 - 1.0f + 2.0f * g2;
        const float y2 = y0 - 1.0f + 2.0f * g2;
        const auto contribution = [&](std::int32_t gx, std::int32_t gy,
                                      float x, float y) {
            const float attenuation = 0.5f - x * x - y * y;
            if (attenuation <= 0.0f) return 0.0f;
            const auto& gradient = gradients[hash(gx, gy, seed_value) & 7U];
            const float squared = attenuation * attenuation;
            return squared * squared * (gradient.x * x + gradient.y * y);
        };
        const float value = 70.0f * (contribution(i, j, x0, y0) +
            contribution(i + i1, j + j1, x1, y1) + contribution(i + 1, j + 1, x2, y2));
        return std::clamp(0.5f + 0.5f * value, 0.0f, 1.0f);
    }

    [[nodiscard]] static float apply_scalar_operator(float value,
                                                     const Field2DOperator& op) noexcept {
        switch (op.kind) {
            case Field2DOperatorKind::Remap: {
                const float width = op.to - op.from;
                return std::abs(width) > 1.0e-7f
                    ? std::clamp((value - op.from) / width, 0.0f, 1.0f) : 0.0f;
            }
            case Field2DOperatorKind::Threshold: return value >= op.edge ? 1.0f : 0.0f;
            case Field2DOperatorKind::Smoothstep: {
                const float width = std::max(op.softness, 1.0e-7f);
                const float t = std::clamp((value - (op.edge - width * 0.5f)) / width,
                                           0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            }
            case Field2DOperatorKind::Invert: return 1.0f - value;
            case Field2DOperatorKind::Add: return value + op.scalar;
            case Field2DOperatorKind::Subtract: return value - op.scalar;
            case Field2DOperatorKind::Multiply: return value * op.scalar;
            case Field2DOperatorKind::Min: return std::min(value, op.scalar);
            case Field2DOperatorKind::Max: return std::max(value, op.scalar);
            case Field2DOperatorKind::Abs: return std::abs(value);
            case Field2DOperatorKind::Power: return (value > 0.0f && op.scalar > 0.0f) ? std::pow(value, op.scalar) : (value <= 0.0f ? 0.0f : 1.0f);
            case Field2DOperatorKind::Clamp: return std::clamp(value, op.scalar, op.scalar_b);
            case Field2DOperatorKind::Warp:
            case Field2DOperatorKind::Translate:
            case Field2DOperatorKind::Scale:
            case Field2DOperatorKind::Rotate:
            case Field2DOperatorKind::Repeat:
            case Field2DOperatorKind::DomainWarp:
                return value;
        }
        return value;
    }

    [[nodiscard]] float sample_generator(Vec2 point) const noexcept {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            std::abs(point.x) > 1.0e6f || std::abs(point.y) > 1.0e6f ||
            !std::isfinite(frequency) || frequency <= 0.0f ||
            std::abs(point.x * frequency) > 1.0e8f ||
            std::abs(point.y * frequency) > 1.0e8f)
            return 0.0f;
        Vec2 p = point * frequency;
        switch (generator) {
            case Field2DGenerator::Perlin:
                return perlin(p, seed);
            case Field2DGenerator::Simplex:
                return simplex(p, seed);
            case Field2DGenerator::Fractal: {
                float amplitude = 1.0f, total = 0.0f, weight = 0.0f;
                for (std::uint32_t octave = 0; octave < octaves; ++octave) {
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                        std::abs(p.x) > 1.0e8f || std::abs(p.y) > 1.0e8f)
                        return 0.5f;
                    total += (perlin(p, seed + octave * 0x9e3779b9U) - 0.5f) * amplitude;
                    weight += amplitude;
                    amplitude *= persistence;
                    p *= lacunarity;
                }
                return weight > 0.0f ? std::clamp(0.5f + total / weight, 0.0f, 1.0f) : 0.5f;
            }
            case Field2DGenerator::Cellular: {
                const auto ix = static_cast<std::int32_t>(std::floor(p.x));
                const auto iy = static_cast<std::int32_t>(std::floor(p.y));
                float nearest = 2.0f;
                for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
                    const float fx = static_cast<float>(ix + x) + unit(hash(ix + x, iy + y, seed));
                    const float fy = static_cast<float>(iy + y) + unit(hash(ix + x, iy + y, seed ^ 0xa511e9b3U));
                    nearest = std::min(nearest, glm::length(p - Vec2{fx, fy}));
                }
                return std::clamp(nearest * 0.75f, 0.0f, 1.0f);
            }
            case Field2DGenerator::Grid:
                return (std::abs(p.x - std::round(p.x)) < 0.08f ||
                        std::abs(p.y - std::round(p.y)) < 0.08f) ? 1.0f : 0.0f;
            case Field2DGenerator::Checker:
                return (static_cast<std::int32_t>(std::floor(p.x)) +
                        static_cast<std::int32_t>(std::floor(p.y))) & 1 ? 1.0f : 0.0f;
            case Field2DGenerator::Dots: {
                const Vec2 cell{p.x - std::floor(p.x) - 0.5f,
                                 p.y - std::floor(p.y) - 0.5f};
                return glm::length(cell) < 0.28f ? 1.0f : 0.0f;
            }
            case Field2DGenerator::Stripes:
                return std::abs(p.x - std::floor(p.x) - 0.5f) < 0.2f ? 1.0f : 0.0f;
            case Field2DGenerator::Rings: {
                const float phase = glm::length((point - Vec2{0.5f}) * frequency);
                return 0.5f + 0.5f * std::cos(phase * 6.28318530718f);
            }
            case Field2DGenerator::Linear:
                return std::clamp(point.x, 0.0f, 1.0f);
            case Field2DGenerator::Radial:
                return std::clamp(1.0f - glm::length((point - Vec2{0.5f}) * 2.0f), 0.0f, 1.0f);
            case Field2DGenerator::Conic: {
                const Vec2 d = point - Vec2{0.5f};
                float a = std::atan2(d.y, d.x) * (1.0f / 6.28318530718f) + 0.5f;
                return a - std::floor(a);
            }
            case Field2DGenerator::Burst: {
                const Vec2 d = point - Vec2{0.5f};
                const float angle = std::atan2(d.y, d.x);
                const float radius = glm::length(d) * 2.0f;
                const float lobes = static_cast<float>(std::clamp(octaves, 3U, 16U));
                const float radial_falloff = std::clamp(1.0f - radius, 0.0f, 1.0f);
                const float angular = 0.5f + 0.5f * std::cos(angle * lobes);
                return std::clamp(angular * angular * radial_falloff, 0.0f, 1.0f);
            }
        }
        return 0.0f;
    }
};

[[nodiscard]] inline std::uint64_t field2d_fingerprint(const Field2D& field) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 0x100000001b3ULL;
    };
    mix(static_cast<std::uint8_t>(field.generator));
    mix(field.seed);
    mix(std::bit_cast<std::uint32_t>(field.frequency));
    mix(field.octaves);
    mix(std::bit_cast<std::uint32_t>(field.persistence));
    mix(std::bit_cast<std::uint32_t>(field.lacunarity));
    mix(std::bit_cast<std::uint32_t>(field.transform.translation.x));
    mix(std::bit_cast<std::uint32_t>(field.transform.translation.y));
    mix(std::bit_cast<std::uint32_t>(field.transform.scale.x));
    mix(std::bit_cast<std::uint32_t>(field.transform.scale.y));
    mix(std::bit_cast<std::uint32_t>(field.transform.rotation_radians));
    mix(std::bit_cast<std::uint32_t>(field.transform.shear.x));
    mix(std::bit_cast<std::uint32_t>(field.transform.shear.y));
    for (const auto& op : field.operators) {
        mix(static_cast<std::uint8_t>(op.kind));
        mix(std::bit_cast<std::uint32_t>(op.from));
        mix(std::bit_cast<std::uint32_t>(op.to));
        mix(std::bit_cast<std::uint32_t>(op.edge));
        mix(std::bit_cast<std::uint32_t>(op.softness));
        mix(std::bit_cast<std::uint32_t>(op.amount));
        mix(std::bit_cast<std::uint32_t>(op.scalar));
        mix(std::bit_cast<std::uint32_t>(op.scalar_b));
        mix(std::bit_cast<std::uint32_t>(op.vec.x));
        mix(std::bit_cast<std::uint32_t>(op.vec.y));
        mix(std::bit_cast<std::uint32_t>(op.angle));
    }
    mix(field.operators.size());
    return hash;
}

} // namespace chronon3d::graphics
