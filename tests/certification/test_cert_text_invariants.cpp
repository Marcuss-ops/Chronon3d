// ═══════════════════════════════════════════════════════════════════════════
// tests/certification/test_cert_text_invariants.cpp
//
// Layout invariants regression locks for content/common/text/glyph_layout.hpp.
// The canonical primitive owns both success and shaping-failure behavior;
// callers inspect the optional result rather than selecting an adapter.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include "content/common/text/glyph_layout.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

TEST_CASE("shape_glyph_line returns empty on non-existent font path") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec bad_spec{"assets/fonts/NonExistentFont.ttf", "Unknown", 400};

    const auto shaped = chronon3d::content::text_reveal::shape_glyph_line(
        "Hello", 72.0f, bad_spec, 4.0f, 0.0f, engine);
    CHECK_FALSE(shaped.has_value());
}
