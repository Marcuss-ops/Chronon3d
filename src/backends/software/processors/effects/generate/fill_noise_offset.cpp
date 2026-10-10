// ---------------------------------------------------------------------------
// fill_noise_offset.cpp — Fill, Noise, Offset scalar processors
// ---------------------------------------------------------------------------

#include "fill_noise_offset.hpp"
#include <chronon3d/core/hash/hash_primitives.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/backends/software/sampling/sampler2d.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace chronon3d::renderer {

// =============================================================================
// Fill — replaces or mixes the pixel colour while preserving alpha
// =============================================================================

void apply_fill(Framebuffer& fb, const Color& color, float amount,
                bool replace_mode,
                const std::optional<raster::BBox>& clip) {
    if (amount <= 0.0f) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    const Color fill_color = color;  // straight RGBA from params

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            auto srgb = color::unpremultiply_rgb(c);

            if (replace_mode) {
                srgb.r = fill_color.r;
                srgb.g = fill_color.g;
                srgb.b = fill_color.b;
            } else {
                // Mix: lerp straight RGB toward fill colour
                const float t = amount;
                srgb.r = srgb.r * (1.0f - t) + fill_color.r * t;
                srgb.g = srgb.g * (1.0f - t) + fill_color.g * t;
                srgb.b = srgb.b * (1.0f - t) + fill_color.b * t;
            }

            row[x] = color::premultiply_rgb(srgb, c.a);
        }
    }
}

// =============================================================================
// Noise — deterministic per-pixel hash, thread-safe
// =============================================================================
//
// Hash function: splitmix64-inspired bit mixing, purely derived from
// (x, y, seed, frame) — no global RNG state.

namespace {

[[nodiscard]] inline uint32_t pixel_hash(uint32_t x, uint32_t y,
                                          uint32_t seed,
                                          uint32_t frame) noexcept {
    uint64_t h = static_cast<uint64_t>(x);
    h = h * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(y);
    h = h * 0xBF58476D1CE4E5B9ULL + static_cast<uint64_t>(seed);
    h = h * 0x94D049BB133111EBULL + static_cast<uint64_t>(frame);

    return static_cast<uint32_t>(core::hash::splitmix64_finalize(h));
}

/// Convert a hash to a float in [-1, 1].
[[nodiscard]] inline float hash_to_noise(uint32_t hash) noexcept {
    // Use upper 24 bits for mantissa, map [0, 2^24-1] → [-1, 1]
    constexpr float inv_range = 2.0f / 16777215.0f;  // 2^24 - 1
    return static_cast<float>(hash >> 8) * inv_range - 1.0f;
}

} // anonymous namespace

void apply_noise(Framebuffer& fb, float amount, uint32_t seed,
                 bool animated, bool rgb_mode, uint32_t frame,
                 const std::optional<raster::BBox>& clip, float size,
                 float shadow_response, float midtone_response,
                 float highlight_response) {
    if (amount <= 0.0f) return;
    if (!std::isfinite(size) || size <= 1.0f) size = 1.0f;
    if (!std::isfinite(shadow_response) || !std::isfinite(midtone_response) ||
        !std::isfinite(highlight_response))
        return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    const uint32_t f = animated ? frame : 0;

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            auto srgb = color::unpremultiply_rgb(c);
            const float luma = std::clamp(
                0.2126f * srgb.r + 0.7152f * srgb.g + 0.0722f * srgb.b,
                0.0f, 1.0f);
            const float tone_response = luma <= 0.5f
                ? shadow_response + (midtone_response - shadow_response) * (luma * 2.0f)
                : midtone_response + (highlight_response - midtone_response) *
                    ((luma - 0.5f) * 2.0f);
            const float scaled_amount = amount * tone_response;
            const auto noise_x = static_cast<uint32_t>(std::floor(
                static_cast<float>(x) / size));
            const auto noise_y = static_cast<uint32_t>(std::floor(
                static_cast<float>(y) / size));

            if (rgb_mode) {
                const uint32_t h_r = pixel_hash(noise_x * 3 + 0, noise_y, seed, f);
                const uint32_t h_g = pixel_hash(noise_x * 3 + 1, noise_y, seed, f);
                const uint32_t h_b = pixel_hash(noise_x * 3 + 2, noise_y, seed, f);
                srgb.r += hash_to_noise(h_r) * scaled_amount;
                srgb.g += hash_to_noise(h_g) * scaled_amount;
                srgb.b += hash_to_noise(h_b) * scaled_amount;
            } else {
                const uint32_t h = pixel_hash(noise_x, noise_y, seed, f);
                const float n = hash_to_noise(h) * scaled_amount;
                srgb.r += n;
                srgb.g += n;
                srgb.b += n;
            }

            row[x] = color::premultiply_rgb(srgb, c.a);
        }
    }
}

// =============================================================================
// Offset — pixel shift with edge modes
// =============================================================================

void apply_offset(Framebuffer& fb, float dx, float dy,
                  chronon3d::sampling::EdgeMode edge_mode,
                  chronon3d::sampling::SampleFilter filter,
                  ::chronon3d::EffectScratchResources& scratch,
                  const std::optional<raster::BBox>& clip) {
    if (dx == 0.0f && dy == 0.0f) return;

    const int w = fb.width(), h = fb.height();

    // Determine the processing region.
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }

    // Create a temporary copy of the entire framebuffer so that sampling
    // reads from the pre-offset state regardless of the processing region.
    // The scratch workspace is REQUIRED (reference): the previous null branch
    // allocated a heap framebuffer per call, i.e. per effect per frame.
    scratch.ensure_size(w, h);
    Framebuffer& temp = *scratch.framebuffer;
    temp.blit(fb, 0, 0);
    const chronon3d::sampling::Sampler2D sampler(temp, edge_mode);

    // ── Integer fast path: nearest-neighbour with edge modes ────────────
    const bool is_integer = (dx == std::floor(dx) && dy == std::floor(dy));
    if (is_integer && filter == chronon3d::sampling::SampleFilter::Nearest) {
        const int idx = static_cast<int>(dx);
        const int idy = static_cast<int>(dy);

        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                // dst(x) = src(x - dx), clamped at edges.
                row[x] = sampler.nearest(static_cast<float>(x - idx),
                                          static_cast<float>(y - idy));
            }
        }
        return;
    }

    // ── Subpixel / bilinear path ──────────────────────────────────────
    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            // dst(x) = src(x - dx): sample from the source at
            // (x - dx, y - dy) so that a positive offset shifts the
            // source rightward / downward.
            const float sx = static_cast<float>(x) - dx;
            const float sy = static_cast<float>(y) - dy;

            if (filter == chronon3d::sampling::SampleFilter::Nearest) {
                row[x] = sampler.nearest(sx, sy);
            } else {
                row[x] = sampler.bilinear(sx, sy);
            }
        }
    }
}

} // namespace chronon3d::renderer
