#include "banded_grain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::renderer {
namespace {

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

inline float sample_band_noise(int32_t x, int32_t y, uint32_t seed) noexcept {
    const uint32_t n = static_cast<uint32_t>(x) + static_cast<uint32_t>(y) * 0x9E3779B9u;
    return static_cast<float>(squirrel3(static_cast<int32_t>(n), seed) & 0x00FFFFFFu) * (1.0f / 16777216.0f) - 0.5f;
}

} // namespace

void apply_banded_grain(Framebuffer& fb, const BandedGrainParams& params,
                        float time_seconds,
                        const std::optional<raster::BBox>& clip) {
    const int width = fb.width();
    const int height = fb.height();
    int x0 = 0, x1 = width, y0 = 0, y1 = height;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, width); x1 = std::clamp(clip->x1, 0, width);
        y0 = std::clamp(clip->y0, 0, height); y1 = std::clamp(clip->y1, 0, height);
    }

    // Default multi-scale bands if none specified
    std::vector<GrainBand> effective_bands = params.bands;
    if (effective_bands.empty()) {
        effective_bands = {
            GrainBand{.scale = 1.0f,  .amplitude = 0.04f,  .octave = 0}, // micro
            GrainBand{.scale = 2.0f,  .amplitude = 0.03f,  .octave = 1}, // fine
            GrainBand{.scale = 4.0f,  .amplitude = 0.02f,  .octave = 2}, // medium
            GrainBand{.scale = 8.0f,  .amplitude = 0.015f, .octave = 3}, // coarse
            GrainBand{.scale = 16.0f, .amplitude = 0.01f,  .octave = 4}  // macro
        };
    }

    const float temporal_drift = params.speed * time_seconds * 100.0f;
    const bool is_rgb = (params.color_mode == NoiseColorMode::RGB);

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color& c = row[x];
            const float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;

            // Tonal luminance response
            const float shadow_w    = (1.0f - luma) * params.shadow_response;
            const float midtone_w   = (1.0f - 2.0f * std::abs(luma - 0.5f)) * params.midtone_response;
            const float highlight_w = luma * params.highlight_response;
            const float response    = std::clamp((shadow_w + midtone_w + highlight_w) * 0.5f, 0.0f, 2.0f);

            if (is_rgb) {
                float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;
                for (const auto& band : effective_bands) {
                    const float s = std::max(0.5f, band.scale);
                    const auto bx = static_cast<int32_t>(std::floor((static_cast<float>(x) + temporal_drift) / s));
                    const auto by = static_cast<int32_t>(std::floor((static_cast<float>(y) + temporal_drift * 0.618f) / s));
                    const uint32_t bseed = params.seed + band.octave * 0x9E3779B9u;

                    sum_r += sample_band_noise(bx, by, bseed)               * band.amplitude;
                    sum_g += sample_band_noise(bx, by, bseed ^ 0x68BC21EBu) * band.amplitude;
                    sum_b += sample_band_noise(bx, by, bseed ^ 0x02E5BE93u) * band.amplitude;
                }
                c.r = std::clamp(c.r + sum_r * params.intensity * response, 0.0f, 1.0f);
                c.g = std::clamp(c.g + sum_g * params.intensity * response, 0.0f, 1.0f);
                c.b = std::clamp(c.b + sum_b * params.intensity * response, 0.0f, 1.0f);
            } else {
                float sum = 0.0f;
                for (const auto& band : effective_bands) {
                    const float s = std::max(0.5f, band.scale);
                    const auto bx = static_cast<int32_t>(std::floor((static_cast<float>(x) + temporal_drift) / s));
                    const auto by = static_cast<int32_t>(std::floor((static_cast<float>(y) + temporal_drift * 0.618f) / s));
                    const uint32_t bseed = params.seed + band.octave * 0x9E3779B9u;

                    sum += sample_band_noise(bx, by, bseed) * band.amplitude;
                }
                const float d = sum * params.intensity * response;
                c.r = std::clamp(c.r + d, 0.0f, 1.0f);
                c.g = std::clamp(c.g + d, 0.0f, 1.0f);
                c.b = std::clamp(c.b + d, 0.0f, 1.0f);
            }
        }
    }
}

} // namespace chronon3d::renderer
