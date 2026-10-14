#pragma once

#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::graphics {

enum class VectorFieldType : std::uint8_t {
    LinearFlow,
    RadialVortex,
    CurlNoise,
    HeatwaveVertical
};

/// 2D Vector Field authority for fluid displacement, organic warp, and heatwave shimmer.
struct VectorField2D {
    VectorFieldType type{VectorFieldType::HeatwaveVertical};
    float strength{0.05f};
    float frequency{4.0f};
    float speed{1.0f};
    std::uint32_t seed{0};

    [[nodiscard]] Vec2 sample(Vec2 point, float time = 0.0f) const noexcept {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(time) ||
            !std::isfinite(strength) || !std::isfinite(frequency) || !std::isfinite(speed) ||
            frequency<=0.f || frequency>4096.f || std::abs(time)>1.e6f || std::abs(speed)>100.f)
            return Vec2{0.0f, 0.0f};

        switch (type) {
            case VectorFieldType::HeatwaveVertical: {
                // Upward convective heat drift with lateral turbulent shimmer
                const float y_drift = point.y * frequency - time * speed * 2.0f;
                const float x_shimmer = point.x * frequency * 1.5f + static_cast<float>(seed);

                const float lateral = std::sin(y_drift * 3.14159f + std::sin(x_shimmer * 2.0f));
                const float vertical = std::cos(y_drift * 2.0f) * 0.4f;

                return Vec2{lateral * strength, vertical * strength};
            }
            case VectorFieldType::LinearFlow: {
                const float angle = static_cast<float>(seed) * 0.1f;
                return Vec2{std::cos(angle), std::sin(angle)} * strength;
            }
            case VectorFieldType::RadialVortex: {
                const Vec2 diff = point - Vec2{0.5f, 0.5f};
                const float dist = glm::length(diff);
                if (dist < 1.0e-5f) return Vec2{0.0f, 0.0f};
                // Tangential vector
                const Vec2 tangent{-diff.y / dist, diff.x / dist};
                const float falloff = std::exp(-dist * frequency);
                return tangent * (strength * falloff);
            }
            case VectorFieldType::CurlNoise: {
                // Finite difference of scalar potential field
                Field2D pot;
                pot.generator = Field2DGenerator::Perlin;
                pot.frequency = frequency;
                pot.seed = seed;

                point += Vec2{time * speed * 0.02f, -time * speed * 0.01f};
                const float eps = 0.01f / frequency;
                const float p_y1 = pot.sample(point + Vec2{0.0f, eps});
                const float p_y0 = pot.sample(point - Vec2{0.0f, eps});
                const float p_x1 = pot.sample(point + Vec2{eps, 0.0f});
                const float p_x0 = pot.sample(point - Vec2{eps, 0.0f});

                const float dphi_dy = (p_y1 - p_y0) / (2.0f * eps);
                const float dphi_dx = (p_x1 - p_x0) / (2.0f * eps);

                // Curl in 2D is (dphi/dy, -dphi/dx)
                return Vec2{dphi_dy, -dphi_dx} * strength;
            }
        }
        return Vec2{0.0f, 0.0f};
    }
};

} // namespace chronon3d::graphics
