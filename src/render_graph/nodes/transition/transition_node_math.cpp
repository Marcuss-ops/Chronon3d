#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <cmath>
#include <algorithm>

#include <chronon3d/render_graph/nodes/transition_node_math.hpp>

namespace chronon3d::graph {

namespace transition_math {

inline float noise_hash(float x, float y) {
    float val = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
    return val - std::floor(val);
}

inline float value_noise_2d(float x, float y) {
    float ix = std::floor(x);
    float iy = std::floor(y);
    float fx = x - ix;
    float fy = y - iy;

    float a = noise_hash(ix, iy);
    float b = noise_hash(ix + 1.0f, iy);
    float c = noise_hash(ix, iy + 1.0f);
    float d = noise_hash(ix + 1.0f, iy + 1.0f);

    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    return (a * (1.0f - ux) + b * ux) * (1.0f - uy) +
           (c * (1.0f - ux) + d * ux) * uy;
}

float procedural_remotion_mask(float u, float v, float t, float seed) {
    float p = (t < 0.5f) ? std::pow(t * 2.0f, 1.3f) : ((1.0f - t) * 2.0f);
    p = smoothstep01(p);

    float n1 = value_noise_2d(u * 2.5f + seed, v * 2.5f + t * 0.3f);
    float n2 = value_noise_2d(u * 4.0f - seed * 1.3f, v * 4.0f + t * 0.5f);
    float n3 = value_noise_2d(u * 1.7f + seed * 0.5f, v * 1.7f + t * 1.0f);

    float leak = std::pow(n1 * 0.6f + n2 * 0.3f + n3 * 0.1f, 2.0f);

    float dx = u - 0.5f;
    float dy = (v - 0.5f) * 0.5625f;
    float dist = std::sqrt(dx * dx + dy * dy);

    float edge_factor = std::clamp((dist - 0.05f) / 0.6f, 0.0f, 1.0f);
    float vignette = edge_factor * edge_factor * (3.0f - 2.0f * edge_factor);

    return leak * vignette * p * 2.0f;
}

static void remotion_pattern(float u, float v, float seed, float t,
                             float& out_brightness, float& out_blend, float& out_pattern) {
    float x = u * 0.8f;
    float y = v * 0.8f;
    x += std::sin(seed * 1.61803f) * 5.0f;
    y += std::cos(seed * 2.71828f) * 5.0f;

    for (int i = 1; i < 5; ++i) {
        float fi = static_cast<float>(i);
        float phase = seed * 0.7f * fi;
        float nx = x + (0.6f / fi) * std::cos(fi * y + t * 0.7f + 0.3f * fi + phase) + 20.0f;
        float ny = y + (0.6f / fi) * std::cos(fi * x + t * 0.7f + 0.3f * static_cast<float>(i + 10) + phase) - 20.0f + 15.0f;
        x = nx;
        y = ny;
    }

    float v1 = 0.5f * std::sin(2.0f * x) + 0.5f;
    float v2 = 0.5f * std::sin(2.0f * y) + 0.5f;
    out_blend = std::sin(x + y) * 0.5f + 0.5f;
    out_brightness = v1 * 0.5f + v2 * 0.5f;
    out_pattern = out_brightness * 0.6f + out_blend * 0.4f;
}

float remotion_mask(float u, float v, float t, float speed, float direction) {
    float aspect = 0.5625f;
    float refScale = 1.92f;
    float motion_scale = std::max(0.1f, speed);

    float speed_factor = 1.5f * motion_scale;
    float evolve_progress = std::clamp(t * speed_factor, 0.0f, 1.0f);
    float evolve = std::pow(evolve_progress, 1.4f);
    evolve = smoothstep01(evolve);

    float retract = smoothstep01(std::clamp(t * speed_factor - 1.2f, 0.0f, 1.0f));

    float su = u * refScale;
    float sv = v * refScale * aspect;

    float b1, bl1, pv1;
    remotion_pattern(su, sv, direction, evolve * 3.14159265f * motion_scale, b1, bl1, pv1);

    float threshA = 1.0f - evolve;
    float revealAlpha = std::clamp((pv1 - threshA) / 0.3f, 0.0f, 1.0f);
    revealAlpha = revealAlpha * revealAlpha * (3.0f - 2.0f * revealAlpha);

    float maxU = refScale;
    float maxV = refScale * aspect;
    float ru = maxU - su;
    float rv = maxV - sv;

    float b2, bl2, pv2;
    remotion_pattern(ru, rv, direction + 42.0f, retract * 3.14159265f * motion_scale, b2, bl2, pv2);

    float threshB = 1.0f - retract;
    float eraseAlpha = std::clamp((pv2 - threshB) / 0.3f, 0.0f, 1.0f);
    eraseAlpha = eraseAlpha * eraseAlpha * (3.0f - 2.0f * eraseAlpha);

    float mask = std::clamp(revealAlpha * (1.0f - eraseAlpha), 0.0f, 1.0f);

    float global_fade = std::pow(std::clamp(t * 2.0f, 0.0f, 1.0f), 1.2f);
    return mask * global_fade;
}

void evaluate_remotion_color(float u, float v, float t, float speed, float direction, float angle,
                             float& out_r, float& out_g, float& out_b) {
    float aspect = 0.5625f;
    float refScale = 1.92f;
    float motion_scale = std::max(0.1f, speed);
    float evolve = std::min(1.0f, t * 2.4f * motion_scale);

    float su = u * refScale;
    float sv = v * refScale * aspect;

    float brightness, blend, pattern;
    remotion_pattern(su, sv, direction, evolve * 3.14159265f * motion_scale, brightness, blend, pattern);

    float base_r = 1.0f;
    float base_g = 0.93f + (0.62f - 0.93f) * blend;
    float base_b = 0.12f + (0.03f - 0.12f) * blend;

    float brightness_factor = 0.65f + 0.55f * brightness;
    base_r *= brightness_factor;
    base_g *= brightness_factor;
    base_b *= brightness_factor;

    float angle_rad = angle * 3.14159265f / 180.0f;
    float cosA = std::cos(angle_rad);
    float sinA = std::sin(angle_rad);
    float inv3 = 1.0f / 3.0f;
    float sqrt3 = 0.57735f;

    float m00 = cosA + (1.0f - cosA) * inv3;
    float m01 = (1.0f - cosA) * inv3 - sinA * sqrt3;
    float m02 = (1.0f - cosA) * inv3 + sinA * sqrt3;
    float m10 = (1.0f - cosA) * inv3 + sinA * sqrt3;
    float m11 = cosA + (1.0f - cosA) * inv3;
    float m12 = (1.0f - cosA) * inv3 - sinA * sqrt3;
    float m20 = (1.0f - cosA) * inv3 - sinA * sqrt3;
    float m21 = (1.0f - cosA) * inv3 + sinA * sqrt3;
    float m22 = cosA + (1.0f - cosA) * inv3;

    out_r = std::clamp(m00 * base_r + m01 * base_g + m02 * base_b, 0.0f, 1.0f);
    out_g = std::clamp(m10 * base_r + m11 * base_g + m12 * base_b, 0.0f, 1.0f);
    out_b = std::clamp(m20 * base_r + m21 * base_g + m22 * base_b, 0.0f, 1.0f);
}

} // namespace transition_math

} // namespace chronon3d::graph
