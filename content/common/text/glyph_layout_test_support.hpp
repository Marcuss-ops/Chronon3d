#pragma once

// ── glyph_layout_test_support.hpp — test/internal hooks for ShapedGlyphLine ──
//
// These functions are intentionally NOT part of the public production API.
// They exist only so tests can inspect the cached GlyphRun and verify the
// single-shape-call contract without exposing that surface in the public
// glyph_layout.hpp header.

#include "content/common/text/glyph_layout.hpp"

namespace chronon3d::content::text_reveal::test_support {

// Reset the file-static shape-call counter to zero.
void reset_shape_call_counter() noexcept;

// Return the current value of the file-static shape-call counter.
int get_shape_call_count() noexcept;

// Access the cached GlyphRun held by a ShapedGlyphLine.
// Requires friend access; declared here, defined in glyph_layout.cpp.
[[nodiscard]] const std::optional<GlyphRun>& get_raw_run(const ShapedGlyphLine& line) noexcept;

} // namespace chronon3d::content::text_reveal::test_support
