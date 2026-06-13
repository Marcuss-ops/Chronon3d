// ---------------------------------------------------------------------------
// directional_blur.cpp — Scalar Directional Blur implementation (A5)
// ---------------------------------------------------------------------------
//
// Algorithm:
//   1. Generate N symmetric taps along the direction (angle) spanning
//      [-length/2, +length/2].
//   2. Each tap has a uniform weight; sum of weights = 1.0.
//   3. For each pixel, sample the framebuffer at each tap position using
//      bilinear sampling with EdgeMode::Clamp and accumulate.
//   4. Fast paths for 0° (horizontal) and 90° (vertical) use box-filter
//      along a single axis for efficiency.
//
// Bounds: Expand by ceil(|cos(angle)| * length / 2) + 1 horizontally,
//         ceil(|sin(angle)| * length / 2) + 1 vertically.

#include "directional_blur.hpp"
#include <chronon3d/backends/software/sampling/sampler2d.hpp>
#include <chronon3d/backends/software/sampling/edge_mode.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

namespace chronon3d::renderer {

// =============================================================================
// Tap generation
// =============================================================================

struct Tap {
    float dx;          // signed offset in X (pixels)
    float dy;          // signed offset in Y (pixels)
    float weight;      // sample weight (sum = 1.0)
};

/// Generate a set of symmetric taps for a directional blur.
/// Returns N taps spanning [-length/2, +length/2] along the given angle.
[[nodiscard]] static std::vector<Tap> generate_taps(
    float angle_degrees, float length, int samples)
{
    if (length <= 0.0f || samples <= 1) {
        // Identity: single tap at origin with weight 1.0
        return {{0.0f, 0.0f, 1.0f}};
    }

    const float angle_rad = angle_degrees * (std::numbers::pi_v<float> / 180.0f);
    const float cos_a = std::cos(angle_rad);
    const float sin_a = std::sin(angle_rad);

    // Use samples if provided (> 1), else auto-derive from length.
    const int n = (samples > 1) ? samples : std::max(2, static_cast<int>(std::ceil(length / 2.0f)));

    std::vector<Tap> taps;
    taps.reserve(n);

    const float half_len = length * 0.5f;
    const float inv_n = 1.0f / static_cast<float>(n);

    for (int i = 0; i < n; ++i) {
        // Parametric position u in [0, 1] → maps to [-half_len, +half_len]
        const float u = (static_cast<float>(i) + 0.5f) * inv_n;  // centre of tap
        const float dist = -half_len + u * length;               // signed distance

        Tap tap;
        tap.dx = dist * cos_a;
        tap.dy = dist * sin_a;
        tap.weight = inv_n;  // uniform weighting
        taps.push_back(tap);
    }

    return taps;
}

// =============================================================================
// Bounds computation
// =============================================================================

std::pair<int, int> directional_blur_margins(float angle_degrees, float length)
{
    if (length <= 0.0f) return {0, 0};

    const float angle_rad = angle_degrees * (std::numbers::pi_v<float> / 180.0f);
    const float half = length * 0.5f;

    const int margin_x = static_cast<int>(std::ceil(std::abs(std::cos(angle_rad)) * half)) + 1;
    const int margin_y = static_cast<int>(std::ceil(std::abs(std::sin(angle_rad)) * half)) + 1;

    return {margin_x, margin_y};
}

// =============================================================================
// Fast path: horizontal blur (angle = 0°)
// =============================================================================

static void apply_directional_blur_horizontal(
    Framebuffer& fb, float length, int samples,
    const std::optional<raster::BBox>& clip)
{
    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    if (length <= 0.0f || y0 >= y1 || x0 >= x1) return;

    const int n = (samples > 1) ? samples : std::max(2, static_cast<int>(std::ceil(length / 2.0f)));
    const float half_len = length * 0.5f;
    const float inv_n = 1.0f / static_cast<float>(n);

    auto temp = std::make_unique<Framebuffer>(w, h);
    temp->blit(fb, 0, 0);

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (int s = 0; s < n; ++s) {
                const float u = (static_cast<float>(s) + 0.5f) * inv_n;
                const float sx = static_cast<float>(x) - half_len + u * length;
                const float sy = static_cast<float>(y);
                const Color c = temp->sample_bilinear(sx, sy);
                r += c.r * inv_n;
                g += c.g * inv_n;
                b += c.b * inv_n;
                a += c.a * inv_n;
            }
            dst_row[x] = Color{r, g, b, a};
        }
    }
}

// =============================================================================
// Fast path: vertical blur (angle = 90°)
// =============================================================================

static void apply_directional_blur_vertical(
    Framebuffer& fb, float length, int samples,
    const std::optional<raster::BBox>& clip)
{
    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    if (length <= 0.0f || y0 >= y1 || x0 >= x1) return;

    const int n = (samples > 1) ? samples : std::max(2, static_cast<int>(std::ceil(length / 2.0f)));
    const float half_len = length * 0.5f;
    const float inv_n = 1.0f / static_cast<float>(n);

    auto temp = std::make_unique<Framebuffer>(w, h);
    temp->blit(fb, 0, 0);

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (int s = 0; s < n; ++s) {
                const float u = (static_cast<float>(s) + 0.5f) * inv_n;
                const float sx = static_cast<float>(x);
                const float sy = static_cast<float>(y) - half_len + u * length;
                const Color c = temp->sample_bilinear(sx, sy);
                r += c.r * inv_n;
                g += c.g * inv_n;
                b += c.b * inv_n;
                a += c.a * inv_n;
            }
            dst_row[x] = Color{r, g, b, a};
        }
    }
}

// =============================================================================
// Generic directional blur (any angle)
// =============================================================================

void apply_directional_blur(
    Framebuffer& fb,
    float angle_degrees,
    float length,
    int samples,
    const std::optional<raster::BBox>& clip)
{
    if (length <= 0.0f) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    if (y0 >= y1 || x0 >= x1) return;

    // ── Fast path: horizontal ──
    if (std::abs(angle_degrees) < 0.001f ||
        std::abs(angle_degrees - 180.0f) < 0.001f ||
        std::abs(angle_degrees - 360.0f) < 0.001f) {
        apply_directional_blur_horizontal(fb, length, samples, clip);
        return;
    }

    // Fast path: vertical
    constexpr float eps = 0.001f;
    if (std::abs(angle_degrees - 90.0f) < eps ||
        std::abs(angle_degrees - 270.0f) < eps) {
        apply_directional_blur_vertical(fb, length, samples, clip);
        return;
    }

    // ── Generic angle path ──
    auto taps = generate_taps(angle_degrees, length, samples);
    if (taps.size() <= 1) return;  // identity

    auto temp = std::make_unique<Framebuffer>(w, h);
    temp->blit(fb, 0, 0);

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (const auto& tap : taps) {
                const float sx = static_cast<float>(x) + tap.dx;
                const float sy = static_cast<float>(y) + tap.dy;
                const Color c = temp->sample_bilinear(sx, sy);
                r += c.r * tap.weight;
                g += c.g * tap.weight;
                b += c.b * tap.weight;
                a += c.a * tap.weight;
            }
            dst_row[x] = Color{r, g, b, a};
        }
    }
}

} // namespace chronon3d::renderer
