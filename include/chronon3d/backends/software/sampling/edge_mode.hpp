#pragma once

// ── EdgeMode ────────────────────────────────────────────────────────────────
//
// Defines how texture sampling behaves when coordinates fall outside the
// framebuffer bounds.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::sampling {

enum class SampleFilter : uint8_t {
    Nearest,
    Bilinear
};

enum class EdgeMode : uint8_t {
    Transparent,   // Out-of-bounds → {0,0,0,0}  (premultiplied transparent)
    Clamp,         // Clamp to nearest edge pixel
    Wrap,          // Tile the texture
    Mirror,        // Mirror the texture
};

// ── Coordinate mapping helpers ──────────────────────────────────────────────

/// Clamp coordinate to [0, size-1].
inline int clamp_coord(int i, int size) noexcept {
    return std::clamp(i, 0, size - 1);
}

/// Wrap (tile) coordinate: i % size with proper negative handling.
inline int wrap_coord(int i, int size) noexcept {
    const int r = i % size;
    return r < 0 ? r + size : r;
}

/// Mirror coordinate: reflect at edges.
/// size=3 → 0,1,2,1,0,1,2,...
inline int mirror_coord(int i, int size) noexcept {
    if (size <= 1) return 0;
    const int period = 2 * size - 2;
    int p = i % period;
    if (p < 0) p += period;
    return p < size ? p : period - p;
}

} // namespace chronon3d::sampling
