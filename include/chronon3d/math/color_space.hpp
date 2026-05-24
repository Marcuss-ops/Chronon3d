#pragma once

// Color-space and alpha-mode conversion utilities.
//
// Internal renderer convention:
//   - pixels stored in LinearSRGB
//   - alpha stored as straight (not premultiplied) during compositing
//   - PNG/video output: convert LinearSRGB -> sRGB before writing
//
// Source images on disk are typically sRGB + straight alpha.
// Call convert_source_to_linear() when loading a texture.
// Call convert_linear_to_output() before encoding to PNG/video.

#include <chronon3d/math/color.hpp>

namespace chronon3d {

// ---------------------------------------------------------------------------
// Single-channel helpers
// ---------------------------------------------------------------------------

// sRGB gamma -> linear (IEC 61966-2-1 precise)
inline float srgb_to_linear(float c) {
    return (c <= 0.04045f) ? (c / 12.92f)
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Linear -> sRGB gamma
inline float linear_to_srgb(float c) {
    return (c <= 0.0031308f) ? (c * 12.92f)
                             : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

// ---------------------------------------------------------------------------
// Color-level convenience (alpha is unchanged by gamma conversion)
// ---------------------------------------------------------------------------

inline Color color_srgb_to_linear(Color c) { return c.to_linear(); }
inline Color color_linear_to_srgb(Color c) { return c.to_srgb();   }

inline Color premultiply(Color c)   { return c.premultiplied();   }
inline Color unpremultiply(Color c) { return c.unpremultiplied(); }

// ---------------------------------------------------------------------------
// Pipeline shortcuts
// ---------------------------------------------------------------------------

// Call when loading a source image (sRGB straight alpha) for use in renderer.
// Result: LinearSRGB straight alpha.
inline Color source_to_linear(Color c) {
    return color_srgb_to_linear(c);
}

// Call before writing a pixel to PNG / video output.
// Input: LinearSRGB straight alpha.
// Result: sRGB straight alpha (clamped).
inline Color linear_to_output(Color c) {
    return color_linear_to_srgb(c).clamped();
}

} // namespace chronon3d
