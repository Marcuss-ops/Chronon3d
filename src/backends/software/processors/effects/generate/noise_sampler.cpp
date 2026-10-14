#include "noise_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::renderer {
namespace {

// Deterministic Squirrel3 hash primitive
inline uint32_t squirrel3(int32_t position, uint32_t seed) noexcept {
    constexpr uint32_t BIT_NOISE1 = 0xB5297A4D;
    constexpr uint32_t BIT_NOISE2 = 0x68E31DA4;
    constexpr uint32_t BIT_NOISE3 = 0x1B56C4E9;
    uint32_t mangled = static_cast<uint32_t>(position);
    mangled *= BIT_NOISE1;
    mangled += seed;
    mangled ^= (mangled >> 8);
    mangled += BIT_NOISE2;
    mangled ^= (mangled << 8);
    mangled *= BIT_NOISE3;
    mangled ^= (mangled >> 8);
    return mangled;
}

inline float hash2d(int32_t x, int32_t y, uint32_t seed) noexcept {
    const uint32_t n = static_cast<uint32_t>(x) + static_cast<uint32_t>(y) * 0x9E3779B9u;
    return static_cast<float>(squirrel3(static_cast<int32_t>(n), seed) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// ── Value Noise 2D ────────────────────────────────────────────────────────────
float value_noise_2d(float x, float y, uint32_t seed) noexcept {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    // Quintic Hermite interpolant: 6t^5 - 15t^4 + 10t^3
    const float sx = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    const float sy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

    const float a = std::lerp(hash2d(ix,     iy,     seed), hash2d(ix + 1, iy,     seed), sx);
    const float b = std::lerp(hash2d(ix,     iy + 1, seed), hash2d(ix + 1, iy + 1, seed), sx);
    return std::lerp(a, b, sy);
}

// ── Simplex Noise 2D ──────────────────────────────────────────────────────────
float simplex_noise_2d(float x, float y, uint32_t seed) noexcept {
    constexpr float F2 = 0.366025403784439f; // 0.5 * (sqrt(3) - 1)
    constexpr float G2 = 0.211324865405187f; // (3 - sqrt(3)) / 6

    const float s = (x + y) * F2;
    const int i = static_cast<int>(std::floor(x + s));
    const int j = static_cast<int>(std::floor(y + s));
    const float t = static_cast<float>(i + j) * G2;
    const float X0 = static_cast<float>(i) - t;
    const float Y0 = static_cast<float>(j) - t;
    const float x0 = x - X0;
    const float y0 = y - Y0;

    int i1 = 0, j1 = 0;
    if (x0 > y0) { i1 = 1; j1 = 0; }
    else         { i1 = 0; j1 = 1; }

    const float x1 = x0 - static_cast<float>(i1) + G2;
    const float y1 = y0 - static_cast<float>(j1) + G2;
    const float x2 = x0 - 1.0f + 2.0f * G2;
    const float y2 = y0 - 1.0f + 2.0f * G2;

    const auto grad = [](int32_t cx, int32_t cy, uint32_t s_seed, float px, float py) noexcept -> float {
        const uint32_t h = squirrel3(cx + cy * 0x9E3779B9, s_seed) & 7u;
        float gx = 1.0f, gy = 0.0f;
        switch (h) {
            case 0: gx =  1.0f; gy =  1.0f; break;
            case 1: gx = -1.0f; gy =  1.0f; break;
            case 2: gx =  1.0f; gy = -1.0f; break;
            case 3: gx = -1.0f; gy = -1.0f; break;
            case 4: gx =  1.0f; gy =  0.0f; break;
            case 5: gx = -1.0f; gy =  0.0f; break;
            case 6: gx =  0.0f; gy =  1.0f; break;
            default:gx =  0.0f; gy = -1.0f; break;
        }
        return gx * px + gy * py;
    };

    float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;
    float t0 = 0.5f - x0 * x0 - y0 * y0;
    if (t0 > 0.0f) { t0 *= t0; n0 = t0 * t0 * grad(i, j, seed, x0, y0); }

    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 > 0.0f) { t1 *= t1; n1 = t1 * t1 * grad(i + i1, j + j1, seed, x1, y1); }

    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 > 0.0f) { t2 *= t2; n2 = t2 * t2 * grad(i + 1, j + 1, seed, x2, y2); }

    // Simplex noise result is in [-1, 1], scale and remap to [0, 1]
    return std::clamp((n0 + n1 + n2) * 35.0f * 0.5f + 0.5f, 0.0f, 1.0f);
}

// ── Worley / Cellular Noise 2D ───────────────────────────────────────────────
float worley_noise_2d(float x, float y, uint32_t seed) noexcept {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    float min_dist = 2.0f;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int cx = ix + dx;
            const int cy = iy + dy;
            const float ox = hash2d(cx, cy, seed);
            const float oy = hash2d(cx, cy, seed ^ 0x68BC21EBu);
            const float px = static_cast<float>(cx) + ox;
            const float py = static_cast<float>(cy) + oy;
            const float dist = std::hypot(x - px, y - py);
            min_dist = std::min(min_dist, dist);
        }
    }
    return std::clamp(min_dist, 0.0f, 1.0f);
}

// ── Blue Noise (Tileable Weyl-sequence hash) ──────────────────────────────────
float blue_noise_2d(float x, float y, uint32_t seed) noexcept {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float h = hash2d(ix, iy, seed);
    // 2D Weyl sequence mapping: golden ratio fractions decorrelate low frequencies
    constexpr float a1 = 0.7548776662466927f;
    constexpr float a2 = 0.5698402909980532f;
    float val = h + static_cast<float>(ix) * a1 + static_cast<float>(iy) * a2;
    val = val - std::floor(val);
    return std::clamp(val, 0.0f, 1.0f);
}

} // namespace

float evaluate_noise_sample(float x, float y, const NoiseSamplerParams& params,
                            uint32_t channel_seed, float time_seconds) noexcept {
    const uint32_t combined_seed = params.seed ^ channel_seed;
    const float drift = params.evolution * time_seconds;

    float px = x + drift;
    float py = y + drift * 0.6180339887f;

    // Optional Domain Warp
    if (params.domain_warp > 0.0f) {
        const float warp_scale = params.domain_warp * 30.0f;
        const float wx = simplex_noise_2d(px * 0.005f + 13.7f, py * 0.005f + 4.9f, combined_seed ^ 0x51E2D3Bu) * 2.0f - 1.0f;
        const float wy = simplex_noise_2d(px * 0.005f + 8.1f,  py * 0.005f + 19.3f, combined_seed ^ 0x9A48C21u) * 2.0f - 1.0f;
        px += wx * warp_scale;
        py += wy * warp_scale;
    }

    const uint32_t oct = std::clamp(params.octaves, 1u, 16u);
    const float lac = (params.lacunarity > 0.1f) ? params.lacunarity : 2.0f;
    const float gn = (params.gain > 0.0f && params.gain <= 1.0f) ? params.gain : 0.5f;

    float freq = std::max(params.frequency, 0.0001f);
    float amp = 1.0f;
    float sum = 0.0f;
    float total_weight = 0.0f;

    for (uint32_t o = 0; o < oct; ++o) {
        const uint32_t octave_seed = combined_seed + o * 0x9E3779B9u;
        float sample = 0.0f;

        switch (params.type) {
            case NoiseSamplerType::Simplex:
                sample = simplex_noise_2d(px * freq, py * freq, octave_seed);
                break;
            case NoiseSamplerType::Worley:
                sample = worley_noise_2d(px * freq, py * freq, octave_seed);
                break;
            case NoiseSamplerType::Ridged: {
                const float s = simplex_noise_2d(px * freq, py * freq, octave_seed) * 2.0f - 1.0f;
                float ridge = 1.0f - std::abs(s);
                ridge = ridge * ridge;
                sample = ridge;
                break;
            }
            case NoiseSamplerType::Value:
                sample = value_noise_2d(px * freq, py * freq, octave_seed);
                break;
            case NoiseSamplerType::Blue:
                sample = blue_noise_2d(px * freq, py * freq, octave_seed);
                break;
        }

        sum += sample * amp;
        total_weight += amp;
        freq *= lac;
        amp *= gn;
    }

    float value = (total_weight > 0.0f) ? (sum / total_weight) : 0.0f;

    // Contrast & Brightness
    value = (value - 0.5f) * params.contrast + 0.5f + params.brightness;

    // Remap
    if (params.remap_max > params.remap_min) {
        value = (value - params.remap_min) / (params.remap_max - params.remap_min);
    }

    // Quantize
    if (params.quantize_steps > 0) {
        const float steps = static_cast<float>(params.quantize_steps);
        value = std::round(value * steps) / steps;
    }

    return std::clamp(value, 0.0f, 1.0f);
}

void apply_noise_sampler(Framebuffer& fb, const NoiseSamplerParams& params,
                         float time_seconds,
                         const std::optional<raster::BBox>& clip) {
    const int width = fb.width();
    const int height = fb.height();
    int x0 = 0, x1 = width, y0 = 0, y1 = height;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, width); x1 = std::clamp(clip->x1, 0, width);
        y0 = std::clamp(clip->y0, 0, height); y1 = std::clamp(clip->y1, 0, height);
    }

    const bool is_rgb = (params.color_mode == NoiseSamplerColorMode::RGB);

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x);
            const float py = static_cast<float>(y);

            if (is_rgb) {
                const float r = evaluate_noise_sample(px, py, params, 0u,          time_seconds);
                const float g = evaluate_noise_sample(px, py, params, 0x68BC21EBu, time_seconds);
                const float b = evaluate_noise_sample(px, py, params, 0x02E5BE93u, time_seconds);
                row[x] = Color{r, g, b, 1.0f};
            } else {
                const float v = evaluate_noise_sample(px, py, params, 0u, time_seconds);
                row[x] = Color{v, v, v, 1.0f};
            }
        }
    }
}

} // namespace chronon3d::renderer
