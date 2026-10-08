#pragma once

// RoughenEdges kernel (pure C++, no Vulkan dependency).
//
// Perturbs only the BOUNDARY band of a premultiplied RGBA float buffer: a
// pixel is on the boundary when one of its 4-neighbours has a different alpha.
// Interior pixels (fully opaque or fully empty neighbourhoods) are copied
// unchanged, so this is an edge roughening, not a whole-image warp.
//
// Boundary pixels take the premultiplied sample found at a noise-displaced
// position (nearest sample, so the result is exact and deterministic).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::effects::distort {

struct RoughenEdgesKernelParams {
    float amount{0.0f};        // maximum boundary displacement, in pixels
    float scale{8.0f};         // noise cell size, in pixels
    std::uint32_t octaves{1};  // fBm octaves, clamped to [1, 4]
    float evolution{0.0f};     // noise phase along time (offsets the lattice)
    std::uint32_t seed{0};
};

namespace roughen_detail {

[[nodiscard]] inline std::uint32_t lattice_hash(std::int32_t x, std::int32_t y,
                                                std::uint32_t seed) noexcept {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x27d4eb2du) ^
                      (static_cast<std::uint32_t>(y) * 0x165667b1u);
    h ^= h >> 15; h *= 0x2c1b3c6du;
    h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

[[nodiscard]] inline float lattice_value(std::int32_t x, std::int32_t y,
                                         std::uint32_t seed) noexcept {
    return static_cast<float>(lattice_hash(x, y, seed)) *
               (2.0f / 4294967295.0f) - 1.0f;
}

// Smooth value noise in [-1, 1].
[[nodiscard]] inline float value_noise(float x, float y, std::uint32_t seed) noexcept {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const auto ix = static_cast<std::int32_t>(fx);
    const auto iy = static_cast<std::int32_t>(fy);
    float tx = x - fx;
    float ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float v00 = lattice_value(ix, iy, seed);
    const float v10 = lattice_value(ix + 1, iy, seed);
    const float v01 = lattice_value(ix, iy + 1, seed);
    const float v11 = lattice_value(ix + 1, iy + 1, seed);
    const float top = v00 + (v10 - v00) * tx;
    const float bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

[[nodiscard]] inline float fbm(float x, float y, std::uint32_t octaves,
                               std::uint32_t seed) noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float frequency = 1.0f;
    for (std::uint32_t o = 0; o < octaves; ++o) {
        sum += amplitude * value_noise(x * frequency, y * frequency, seed + o);
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

} // namespace roughen_detail

/// Roughen the alpha boundary of a premultiplied RGBA float buffer in place.
/// `rgba` must hold width*height*4 floats in row-major order.
inline void roughen_edges_rgba(std::span<float> rgba, int width, int height,
                               const RoughenEdgesKernelParams& params) {
    if (width <= 0 || height <= 0 || params.amount <= 0.0f) return;
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgba.size() < pixels * 4) return;
    const float scale = params.scale > 0.0f ? params.scale : 1.0f;
    const std::uint32_t octaves = std::clamp<std::uint32_t>(params.octaves, 1u, 4u);

    const std::vector<float> source(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(pixels * 4));
    const auto index = [width](int x, int y) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)) * 4;
    };
    const auto alpha_at = [&](int x, int y) {
        return source[index(std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1)) + 3];
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float a = alpha_at(x, y);
            const bool boundary =
                std::fabs(alpha_at(x - 1, y) - a) > 1.0e-4f ||
                std::fabs(alpha_at(x + 1, y) - a) > 1.0e-4f ||
                std::fabs(alpha_at(x, y - 1) - a) > 1.0e-4f ||
                std::fabs(alpha_at(x, y + 1) - a) > 1.0e-4f;
            if (!boundary) continue;

            const float px = static_cast<float>(x) / scale;
            const float py = static_cast<float>(y) / scale;
            const float dx = params.amount * roughen_detail::fbm(px + params.evolution, py, octaves, params.seed);
            const float dy = params.amount * roughen_detail::fbm(px, py + 1000.0f + params.evolution, octaves, params.seed + 101u);
            const int sx = std::clamp(static_cast<int>(std::lround(static_cast<float>(x) + dx)), 0, width - 1);
            const int sy = std::clamp(static_cast<int>(std::lround(static_cast<float>(y) + dy)), 0, height - 1);

            const std::size_t dst = index(x, y);
            const std::size_t src = index(sx, sy);
            for (int c = 0; c < 4; ++c) rgba[dst + static_cast<std::size_t>(c)] = source[src + static_cast<std::size_t>(c)];
        }
    }
}

} // namespace chronon3d::effects::distort
