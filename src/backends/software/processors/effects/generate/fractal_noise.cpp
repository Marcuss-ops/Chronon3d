#include "fractal_noise.hpp"

#include <chronon3d/animation/effects/wiggle.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::renderer {
namespace {

float lattice_noise(int x, int y, uint32_t seed) {
    // Reuse the deterministic Squirrel3 noise primitive. The odd multiplier
    // decorrelates rows while keeping the mapping stable across platforms.
    const uint32_t index = static_cast<uint32_t>(x) +
                           static_cast<uint32_t>(y) * 0x9E3779B9u;
    return detail::hash_noise(static_cast<int32_t>(index), seed);
}

float smooth_value_noise(float x, float y, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = std::lerp(lattice_noise(ix, iy, seed),
                              lattice_noise(ix + 1, iy, seed), sx);
    const float b = std::lerp(lattice_noise(ix, iy + 1, seed),
                              lattice_noise(ix + 1, iy + 1, seed), sx);
    return std::lerp(a, b, sy);
}

float fractal(float x, float y, const FractalNoiseParams& p, uint32_t channel_seed) {
    const uint32_t octaves = std::clamp(p.octaves, 1u, 16u);
    float frequency = std::max(0.0f, p.frequency);
    float amplitude = 1.0f;
    float sum = 0.0f;
    float weight = 0.0f;
    for (uint32_t octave = 0; octave < octaves; ++octave) {
        sum += smooth_value_noise(x * frequency, y * frequency,
                                  p.seed + channel_seed + octave * 0x9E3779B9u) * amplitude;
        weight += amplitude;
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    float value = weight > 0.0f ? sum / weight : 0.0f;
    value = (value * p.contrast + p.brightness) * p.amplitude;
    return std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f);
}

} // namespace

float evaluate_smooth_value_noise(float x,float y,std::uint32_t seed) {
    return smooth_value_noise(x,y,seed);
}

void apply_fractal_noise(Framebuffer& fb, const FractalNoiseParams& params,
                         float time_seconds, const std::optional<raster::BBox>& clip) {
    const int width = fb.width();
    const int height = fb.height();
    int x0 = 0, x1 = width, y0 = 0, y1 = height;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, width); x1 = std::clamp(clip->x1, 0, width);
        y0 = std::clamp(clip->y0, 0, height); y1 = std::clamp(clip->y1, 0, height);
    }
    const float drift = params.evolution * time_seconds;
    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x) + drift;
            const float py = static_cast<float>(y) + drift * 0.6180339f;
            if (params.color_mode == NoiseColorMode::RGB) {
                row[x] = Color{fractal(px, py, params, 0u),
                               fractal(px, py, params, 0x68BC21EBu),
                               fractal(px, py, params, 0x02E5BE93u), 1.0f};
            } else {
                const float value = fractal(px, py, params, 0u);
                row[x] = Color{value, value, value, 1.0f};
            }
        }
    }
}

} // namespace chronon3d::renderer
