#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chronon3d::graphics {

/// A single control point inside a GradientMesh.
struct GradientMeshNode {
    Vec2 position{0.0f, 0.0f};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    float weight{1.0f};
};

/// Fluid multi-node gradient mesh evaluator.
/// Powers organic gradients, aurora backgrounds, and fluid backdrops
/// without needing specialized procedural generators.
struct GradientMesh {
    std::vector<GradientMeshNode> nodes;

    /// Evaluates interpolated color at normalized coordinate uv in [0, 1].
    [[nodiscard]] Color sample(Vec2 uv) const noexcept {
        if (nodes.empty())
            return Color{0.0f, 0.0f, 0.0f, 0.0f};
        if (nodes.size() == 1)
            return nodes[0].color;

        if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
            return nodes[0].color;

        // Modified Shepard / Inverse Distance Weighting with smooth kernel
        float total_weight = 0.0f;
        float r_acc = 0.0f, g_acc = 0.0f, b_acc = 0.0f, a_acc = 0.0f;

        for (const auto& node : nodes) {
            const Vec2 d = uv - node.position;
            const float dist2 = glm::dot(d, d);
            if (dist2 < 1.0e-7f) {
                // Exact match at control node
                return node.color;
            }

            // Smooth inverse quadratic with node weight
            const float w = std::max(node.weight, 1.0e-4f) / (dist2 * std::sqrt(dist2));
            total_weight += w;
            r_acc += node.color.r * w;
            g_acc += node.color.g * w;
            b_acc += node.color.b * w;
            a_acc += node.color.a * w;
        }

        if (total_weight <= 1.0e-7f)
            return nodes[0].color;

        const float inv_w = 1.0f / total_weight;
        return Color{
            std::clamp(r_acc * inv_w, 0.0f, 1.0f),
            std::clamp(g_acc * inv_w, 0.0f, 1.0f),
            std::clamp(b_acc * inv_w, 0.0f, 1.0f),
            std::clamp(a_acc * inv_w, 0.0f, 1.0f)
        };
    }

    /// Canonical 4-corner mesh
    [[nodiscard]] static GradientMesh four_corner(
        Color top_left, Color top_right, Color bottom_left, Color bottom_right) {
        GradientMesh mesh;
        mesh.nodes = {
            {Vec2{0.0f, 0.0f}, top_left, 1.0f},
            {Vec2{1.0f, 0.0f}, top_right, 1.0f},
            {Vec2{0.0f, 1.0f}, bottom_left, 1.0f},
            {Vec2{1.0f, 1.0f}, bottom_right, 1.0f},
        };
        return mesh;
    }

    /// Canonical fluid Aurora mesh with organic internal control points
    [[nodiscard]] static GradientMesh aurora_preset(float phase = 0.0f) {
        const float c1 = std::sin(phase * 0.7f) * 0.15f;
        const float s1 = std::cos(phase * 0.9f) * 0.15f;
        const float c2 = std::cos(phase * 0.5f) * 0.20f;
        const float s2 = std::sin(phase * 0.8f) * 0.20f;

        GradientMesh mesh;
        mesh.nodes = {
            // Corners (dark cosmic deep tones)
            {Vec2{0.0f, 0.0f}, Color{0.02f, 0.05f, 0.15f, 1.0f}, 0.8f},
            {Vec2{1.0f, 0.0f}, Color{0.05f, 0.02f, 0.18f, 1.0f}, 0.8f},
            {Vec2{0.0f, 1.0f}, Color{0.01f, 0.08f, 0.12f, 1.0f}, 0.8f},
            {Vec2{1.0f, 1.0f}, Color{0.08f, 0.03f, 0.14f, 1.0f}, 0.8f},

            // Interior glowing ribbons (emerald green, electric cyan, magenta, violet)
            {Vec2{0.35f + c1, 0.40f + s1}, Color{0.05f, 0.88f, 0.55f, 1.0f}, 1.8f}, // emerald
            {Vec2{0.65f - c2, 0.45f + s2}, Color{0.15f, 0.45f, 0.95f, 1.0f}, 1.6f}, // electric cyan/blue
            {Vec2{0.50f + s1, 0.75f - c1}, Color{0.82f, 0.15f, 0.65f, 1.0f}, 1.5f}, // magenta
            {Vec2{0.20f + s2, 0.60f + c2}, Color{0.45f, 0.10f, 0.80f, 1.0f}, 1.2f}, // violet
        };
        return mesh;
    }
};

} // namespace chronon3d::graphics
