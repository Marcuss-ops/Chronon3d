#pragma once

// ---------------------------------------------------------------------------
// packed_backend.hpp — Float→RGBA8 direct quantization (TBB-parallel).
//
// PR5: Extracted from frame_converter.cpp into its own backend TU.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace chronon3d { class Framebuffer; }

namespace chronon3d::video::packed {

/// Convert float Framebuffer pixels to 8-bit RGBA (R-G-B-A interleaved).
/// Uses TBB parallel_for internally.  apply_gamma controls whether
/// Color::linear_to_srgb8 is applied.
void convert_fb_to_rgba8(const Framebuffer& src,
                         int width, int height,
                         bool apply_gamma,
                         uint8_t* rgba8);

}  // namespace chronon3d::video::packed
