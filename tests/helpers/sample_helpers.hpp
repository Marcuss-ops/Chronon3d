#pragma once

// ── Sample helpers for framebuffer pixel assertions ──────────────────
//
// Utility functions for common sampling patterns used in integration
// tests that verify rendered pixel output.
//
// Functions:
//   sample_center(fb, half)  — average colour in a (2*half+1)^2 kernel
//   sample_left(fb, x_off)   — average along vertical strip at x = x_off
//   sample_right(fb, x_off)  — average along vertical strip at x = width - x_off
//
// All values are in linear colour space (the native framebuffer format).

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::test {

/// Average colour in a square region centred on the framebuffer.
/// Kernel half-size defaults to 8 ⇒ 17×17 pixel sample.
inline Color sample_center(const Framebuffer& fb, int half = 8) {
    const int cx = fb.width() / 2;
    const int cy = fb.height() / 2;
    float r = 0, g = 0, b = 0, a = 0;
    int n = 0;
    for (int dy = -half; dy <= half; ++dy)
        for (int dx = -half; dx <= half; ++dx) {
            Color c = fb.get_pixel(cx + dx, cy + dy);
            r += c.r; g += c.g; b += c.b; a += c.a;
            ++n;
        }
    return {r / n, g / n, b / n, a / n};
}

/// Average colour along a 9-pixel vertical strip at x = x_off (left side).
inline Color sample_left(const Framebuffer& fb, int x_off = 15) {
    const int cy = fb.height() / 2;
    float r = 0, g = 0, b = 0, a = 0;
    int n = 0;
    for (int dy = -4; dy <= 4; ++dy) {
        Color c = fb.get_pixel(x_off, cy + dy);
        r += c.r; g += c.g; b += c.b; a += c.a;
        ++n;
    }
    return {r / n, g / n, b / n, a / n};
}

/// Average colour along a 9-pixel vertical strip at x = width - x_off (right side).
inline Color sample_right(const Framebuffer& fb, int x_off = 15) {
    const int cx = fb.width() - x_off;
    const int cy = fb.height() / 2;
    float r = 0, g = 0, b = 0, a = 0;
    int n = 0;
    for (int dy = -4; dy <= 4; ++dy) {
        Color c = fb.get_pixel(cx, cy + dy);
        r += c.r; g += c.g; b += c.b; a += c.a;
        ++n;
    }
    return {r / n, g / n, b / n, a / n};
}

} // namespace chronon3d::test
