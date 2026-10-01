#pragma once

#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::graphics {

/// 2D Signed Distance Field (SDF) mathematics and operators.
/// Powers edge glow, rims, outlines, and smooth shape blending.
struct DistanceField2D {
    // ── Primitives ──

    [[nodiscard]] static float circle(Vec2 point, Vec2 center, float radius) noexcept {
        return glm::length(point - center) - radius;
    }

    [[nodiscard]] static float box(Vec2 point, Vec2 center, Vec2 half_extents) noexcept {
        const Vec2 d = glm::abs(point - center) - half_extents;
        return glm::length(glm::max(d, Vec2{0.0f, 0.0f})) +
               std::min(std::max(d.x, d.y), 0.0f);
    }

    [[nodiscard]] static float rounded_box(Vec2 point, Vec2 center, Vec2 half_extents, float radius) noexcept {
        const Vec2 d = glm::abs(point - center) - half_extents + Vec2{radius, radius};
        return glm::length(glm::max(d, Vec2{0.0f, 0.0f})) +
               std::min(std::max(d.x, d.y), 0.0f) - radius;
    }

    [[nodiscard]] static float segment(Vec2 point, Vec2 a, Vec2 b) noexcept {
        const Vec2 pa = point - a;
        const Vec2 ba = b - a;
        const float len2 = glm::dot(ba, ba);
        const float h = len2 > 1.0e-7f ? std::clamp(glm::dot(pa, ba) / len2, 0.0f, 1.0f) : 0.0f;
        return glm::length(pa - ba * h);
    }

    // ── CSG and Operators ──

    [[nodiscard]] static float expand(float dist, float radius) noexcept {
        return dist - radius;
    }

    [[nodiscard]] static float contract(float dist, float radius) noexcept {
        return dist + radius;
    }

    [[nodiscard]] static float union_op(float d1, float d2) noexcept {
        return std::min(d1, d2);
    }

    [[nodiscard]] static float subtract_op(float d1, float d2) noexcept {
        return std::max(d1, -d2);
    }

    [[nodiscard]] static float intersect_op(float d1, float d2) noexcept {
        return std::max(d1, d2);
    }

    /// Polynomial smooth minimum for organic shape fusion (e.g. metaball typography)
    [[nodiscard]] static float smooth_union(float d1, float d2, float k = 0.1f) noexcept {
        if (k <= 1.0e-6f) return std::min(d1, d2);
        const float h = std::clamp(0.5f + 0.5f * (d2 - d1) / k, 0.0f, 1.0f);
        return std::lerp(d2, d1, h) - k * h * (1.0f - h);
    }

    /// Polynomial smooth subtraction
    [[nodiscard]] static float smooth_subtract(float d1, float d2, float k = 0.1f) noexcept {
        if (k <= 1.0e-6f) return std::max(d1, -d2);
        const float h = std::clamp(0.5f - 0.5f * (d2 + d1) / k, 0.0f, 1.0f);
        return std::lerp(d1, -d2, h) + k * h * (1.0f - h);
    }

    // ── Edge Shading Operators ──

    /// Generates outline alpha from distance field: 1 inside stroke, 0 elsewhere
    [[nodiscard]] static float outline(float dist, float width, float softness = 1.0f) noexcept {
        const float d = std::abs(dist) - width * 0.5f;
        const float s = std::max(softness, 1.0e-4f);
        return 1.0f - std::clamp((d + s * 0.5f) / s, 0.0f, 1.0f);
    }

    /// Generates rim lighting at the inner boundary of the shape
    [[nodiscard]] static float rim(float dist, float width, float softness = 1.0f) noexcept {
        if (dist > 0.0f) return 0.0f; // outside shape
        const float inner_d = -dist; // positive inside
        const float s = std::max(softness, 1.0e-4f);
        const float edge_alpha = 1.0f - std::clamp(dist / s, -1.0f, 0.0f); // anti-aliased edge
        const float profile = std::clamp((inner_d) / std::max(width, 1.0e-4f), 0.0f, 1.0f);
        const float falloff = 1.0f - profile;
        return edge_alpha * falloff;
    }

    /// Generates exponential edge glow radiating outside
    [[nodiscard]] static float edge_glow(float dist, float radius, float intensity = 1.0f) noexcept {
        if (dist <= 0.0f) return intensity; // full intensity inside
        const float r = std::max(radius, 1.0e-4f);
        return intensity * std::exp(-dist / r);
    }
};

} // namespace chronon3d::graphics
