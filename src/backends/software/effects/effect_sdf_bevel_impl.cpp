// ---------------------------------------------------------------------------
// effect_sdf_bevel_impl.cpp — SDF bevel with dynamic lighting and shadow
//
// Rebuilds a signed distance field from the layer alpha, derives a surface
// normal from its gradient, and shades a bevel + physical rim.  A projected
// shadow is generated from the same silhouette and composited behind the
// content before the bevel is applied.  Because EffectStackNode expands the
// ROI by effect_spatial_halo(SdfBevelParams), the shadow can fall outside the
// original layer bounds while the effect still operates in local coordinates.
// ---------------------------------------------------------------------------

#include "render_effects_processor.hpp"
#include "effect_helpers.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace chronon3d {
namespace renderer {
namespace {

constexpr float k_distance_inf = 1.0e20f;

// 1D squared Euclidean distance transform (Felzenszwalb & Huttenlocher).
// Infinite sites are skipped so rows/columns that contain no seed contribute
// no candidates instead of producing NaNs.
void distance_transform_1d(const float* f, float* d, int n) {
    if (n <= 0) return;
    std::vector<int> v(static_cast<size_t>(n));
    std::vector<float> z(static_cast<size_t>(n) + 1);
    int k = -1;

    for (int q = 0; q < n; ++q) {
        if (!std::isfinite(f[q])) continue;
        if (k < 0) {
            k = 0;
            v[0] = q;
            z[0] = -k_distance_inf;
            z[1] = k_distance_inf;
            continue;
        }
        float s = ((f[q] + static_cast<float>(q) * q) -
                   (f[v[k]] + static_cast<float>(v[k]) * v[k])) /
                  (2.0f * static_cast<float>(q - v[k]));
        while (s <= z[k]) {
            --k;
            if (k < 0) break;
            s = ((f[q] + static_cast<float>(q) * q) -
                 (f[v[k]] + static_cast<float>(v[k]) * v[k])) /
                (2.0f * static_cast<float>(q - v[k]));
        }
        if (k < 0) {
            k = 0;
            v[0] = q;
            z[0] = -k_distance_inf;
            z[1] = k_distance_inf;
        } else {
            ++k;
            v[k] = q;
            z[k] = s;
            z[k + 1] = k_distance_inf;
        }
    }

    if (k < 0) {
        for (int q = 0; q < n; ++q) d[q] = k_distance_inf;
        return;
    }

    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<float>(q)) ++k;
        const float dq = static_cast<float>(q - v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

void distance_transform_2d(std::vector<float>& field, int w, int h) {
    std::vector<float> line(static_cast<size_t>(std::max(w, h)));
    std::vector<float> out(static_cast<size_t>(std::max(w, h)));

    for (int y = 0; y < h; ++y) {
        float* row = field.data() + static_cast<size_t>(y) * w;
        distance_transform_1d(row, out.data(), w);
        std::copy(out.begin(), out.begin() + w, row);
    }

    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            line[y] = field[static_cast<size_t>(y) * w + x];
        }
        distance_transform_1d(line.data(), out.data(), h);
        for (int y = 0; y < h; ++y) {
            field[static_cast<size_t>(y) * w + x] = out[y];
        }
    }
}

inline float smoothstep01(float x) {
    const float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void apply_sdf_bevel_effect(
    Framebuffer& fb,
    const SdfBevelParams& p,
    float time_seconds,
    bool /*diagnostics_enabled*/
) {
    const i32 w = fb.width();
    const i32 h = fb.height();
    if (w <= 0 || h <= 0) return;

    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<float> to_inside(count, k_distance_inf);
    std::vector<float> to_outside(count, k_distance_inf);

    bool has_content = false;
    for (i32 y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        for (i32 x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            if (row[x].a > 0.5f) {
                to_inside[i] = 0.0f;
                has_content = true;
            } else {
                to_outside[i] = 0.0f;
            }
        }
    }
    if (!has_content) return;

    // Negative inside the silhouette, positive outside.
    distance_transform_2d(to_inside, w, h);
    distance_transform_2d(to_outside, w, h);
    std::vector<float> sdf(count);
    for (size_t i = 0; i < count; ++i) {
        sdf[i] = std::sqrt(to_inside[i]) - std::sqrt(to_outside[i]);
    }

    const float azimuth = (p.light_angle +
                           p.light_orbit_speed * time_seconds) *
                          (std::numbers::pi_v<float> / 180.0f);
    const float elevation = std::clamp(p.light_elevation, 0.0f, 89.0f) *
                            (std::numbers::pi_v<float> / 180.0f);
    const float lx = std::cos(azimuth) * std::cos(elevation);
    const float ly = std::sin(azimuth) * std::cos(elevation);
    const float lz = std::sin(elevation);

    // Cast shadow first, using the silhouette before the bevel touches it.
    if (p.shadow_opacity > 0.0f && p.shadow_distance > 0.0f) {
        const float planar = std::sqrt(lx * lx + ly * ly);
        const float dx = planar > 1.0e-4f ? -lx / planar : 0.0f;
        const float dy = planar > 1.0e-4f ? -ly / planar : 1.0f;
        DropShadowParams shadow;
        shadow.offset = {dx * p.shadow_distance, dy * p.shadow_distance};
        shadow.radius = std::max(0.0f, p.shadow_softness);
        shadow.color = p.shadow_color;
        shadow.color.a = std::clamp(
            p.shadow_color.a * p.shadow_opacity, 0.0f, 1.0f);
        apply_shadow_effect(fb, shadow, std::nullopt, false);
    }

    const float bevel_width = std::max(p.bevel_width, 0.001f);
    const float rim_width = std::max(p.rim_width, 0.0f);
    const float slope = std::clamp(
        p.bevel_height / bevel_width, 0.05f, 4.0f);
    const Color rim = p.rim_color;

    for (i32 y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            const float d = sdf[i];
            if (d >= 0.0f) continue; // outside silhouette (shadow may live here)

            // 1 on the silhouette, 0 at the inner edge of the bevel band and
            // everywhere on the flat card face.  Do not invert this: the SDF
            // is very negative deep inside, and an inverted factor would shade
            // the distance-field medial axis across the whole card.
            const float t = std::clamp(1.0f + d / bevel_width, 0.0f, 1.0f);
            const float edge = t;

            // Gradient of the SDF points outward.  The bevel normal rotates
            // from horizontal at the silhouette to straight up on the face.
            const float gx = (x > 0 && x + 1 < w)
                ? (sdf[i + 1] - sdf[i - 1]) * 0.5f : 0.0f;
            const float gy = (y > 0 && y + 1 < h)
                ? (sdf[i + w] - sdf[i - w]) * 0.5f : 0.0f;
            const float glen = std::sqrt(gx * gx + gy * gy);
            const float nx = glen > 1.0e-5f ? gx / glen : 0.0f;
            const float ny = glen > 1.0e-5f ? gy / glen : 0.0f;
            const float hx = nx * edge * slope;
            const float hy = ny * edge * slope;
            const float hz = 1.0f;
            const float hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
            const float sx = hx / hlen;
            const float sy = hy / hlen;
            const float sz = hz / hlen;

            const float ndl = std::clamp(sx * lx + sy * ly + sz * lz, 0.0f, 1.0f);
            const float diffuse = 0.55f + 0.55f * ndl;
            const float light_mult = 1.0f + (diffuse - 1.0f) * edge;
            const float shade_mult =
                1.0f - p.shade * edge * (1.0f - ndl);

            Color straight = row[x].unpremultiplied();
            straight.r = std::clamp(straight.r * light_mult * shade_mult, 0.0f, 8.0f);
            straight.g = std::clamp(straight.g * light_mult * shade_mult, 0.0f, 8.0f);
            straight.b = std::clamp(straight.b * light_mult * shade_mult, 0.0f, 8.0f);

            // Tight specular lobe on the bevelled edge.
            const float half_x = lx;
            const float half_y = ly;
            const float half_z = lz + 1.0f;
            const float half_len = std::sqrt(
                half_x * half_x + half_y * half_y + half_z * half_z);
            if (half_len > 0.0f) {
                const float ndh = std::clamp(
                    (sx * half_x + sy * half_y + sz * half_z) / half_len,
                    0.0f, 1.0f);
                const float spec = std::pow(ndh, 26.0f) *
                                   std::max(0.0f, p.highlight) * edge;
                straight.r += spec * 0.95f;
                straight.g += spec * 0.88f;
                straight.b += spec * 0.72f;
            }

            // Physical rim: strongest exactly at the silhouette and fading
            // inward over rim_width.  It is lit by the same normal.
            if (rim_width > 0.0f && p.rim_intensity > 0.0f) {
                const float rim_mask = smoothstep01(1.0f + d / rim_width);
                const float rim_light = 0.30f + 0.70f * ndl;
                const float rim_alpha = std::clamp(
                    rim_mask * p.rim_intensity * rim_light, 0.0f, 1.0f);
                straight.r = straight.r * (1.0f - rim_alpha) + rim.r * rim_alpha;
                straight.g = straight.g * (1.0f - rim_alpha) + rim.g * rim_alpha;
                straight.b = straight.b * (1.0f - rim_alpha) + rim.b * rim_alpha;
            }

            straight.r = std::clamp(straight.r, 0.0f, 1.0f);
            straight.g = std::clamp(straight.g, 0.0f, 1.0f);
            straight.b = std::clamp(straight.b, 0.0f, 1.0f);
            row[x] = straight.premultiplied();
        }
    }
}

} // namespace renderer
} // namespace chronon3d
