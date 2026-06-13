// ---------------------------------------------------------------------------
// radial_blur.cpp — Scalar Radial Blur implementation (A6)
// ---------------------------------------------------------------------------
//
// Algorithm (combined zoom + spin):
//
//   For each pixel, N taps are placed symmetrically around the pixel
//   along both radial (zoom) and tangential (spin) directions.
//
//   Zoom taps:  pixel + (pixel - center) × scale_i
//     where scale_i ∈ [-s, +s] with s = amount / 2, symmetric around 0
//
//   Spin taps:  pixel rotated around center by ±angle_i
//     where angle_i ∈ [-a, +a] with a = amount degrees, symmetric
//
//   Combined: each tap is the sum of zoom offset + spin offset.
//   Uniform weighting: each tap weight = 1/N.
//
//   The tap pattern is symmetric so the centroid is preserved and
//   the blur does not shift the image.
//
//   The pixel at the exact center always samples itself, making the
//   centre invariant.

#include "radial_blur.hpp"
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

/// Generate N symmetric taps for a combined zoom+spin radial blur.
///
/// `amount` controls the overall blur intensity — it scales both the
/// zoom extent and spin angle.
/// `cx_px, cy_px` — center coordinates in pixels.
/// `px, py` — pixel coordinates (for zoom direction).
///
/// Returns exactly `samples` taps.
[[nodiscard]] static std::vector<Tap> generate_radial_taps(
    float cx_px, float cy_px,
    float px, float py,
    float amount, int samples)
{
    if (amount <= 0.0f || samples <= 1) {
        return {{0.0f, 0.0f, 1.0f}};
    }

    const int n = std::max(2, samples);
    std::vector<Tap> taps;
    taps.reserve(n);

    // Direction from center to pixel (zoom axis)
    const float dx_c = px - cx_px;
    const float dy_c = py - cy_px;

    // For zoom: scale centered around 1.0, symmetric around 0
    //   scale_i = 1 + u * amount, so scale_i - 1 = -(scale_(N-1-i) - 1)
    //
    // For spin: max angle in radians = amount degrees → radians
    const float max_angle_rad = amount * (std::numbers::pi_v<float> / 180.0f);

    const float inv_n = 1.0f / static_cast<float>(n);

    for (int i = 0; i < n; ++i) {
        // Parametric position u in [0, 1] → maps to [-1, +1]
        const float u = (static_cast<float>(i) + 0.5f) * inv_n * 2.0f - 1.0f;

        // ── Zoom offset: scale centered around 1.0 ──
        // tap_pos = center + (pixel - center) * (1 + u * amount)
        // offset  = (pixel - center) * u * amount
        const float zoom_off_x = dx_c * u * amount;
        const float zoom_off_y = dy_c * u * amount;

        // ── Spin offset: rotate (dx_c, dy_c) by angle and subtract ──
        const float angle = u * max_angle_rad;
        const float cos_a = std::cos(angle);
        const float sin_a = std::sin(angle);
        // Rotated position: original pixel rotated around center by angle
        const float rot_x = cx_px + dx_c * cos_a - dy_c * sin_a;
        const float rot_y = cy_px + dx_c * sin_a + dy_c * cos_a;
        const float spin_off_x = rot_x - px;
        const float spin_off_y = rot_y - py;

        Tap tap;
        tap.dx = zoom_off_x + spin_off_x;
        tap.dy = zoom_off_y + spin_off_y;
        tap.weight = inv_n;
        taps.push_back(tap);
    }

    return taps;
}

// =============================================================================
// Main apply function
// =============================================================================

void apply_radial_blur(
    Framebuffer& fb,
    float center_x, float center_y,
    float amount,
    int samples,
    const std::optional<raster::BBox>& clip)
{
    if (amount <= 0.0f || samples <= 1) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    if (y0 >= y1 || x0 >= x1) return;

    // Convert normalized centre to pixel coordinates
    const float cx_px = center_x * static_cast<float>(w);
    const float cy_px = center_y * static_cast<float>(h);

    const int n = std::max(2, samples);

    // Pre-generate tap patterns for each pixel? That would be expensive.
    // Instead, we generate taps per pixel since they depend on the
    // direction from the pixel to the centre.
    auto temp = std::make_unique<Framebuffer>(w, h);
    temp->blit(fb, 0, 0);

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        const float py = static_cast<float>(y);
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x);

            // Fast path: exact centre → pixel samples itself (identity)
            const float dx_c = px - cx_px;
            const float dy_c = py - cy_px;
            if (std::abs(dx_c) < 0.001f && std::abs(dy_c) < 0.001f) {
                dst_row[x] = temp->get_pixel(x, y);
                continue;
            }

            auto taps = generate_radial_taps(cx_px, cy_px, px, py, amount, n);

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (const auto& tap : taps) {
                const float sx = px + tap.dx;
                const float sy = py + tap.dy;
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
