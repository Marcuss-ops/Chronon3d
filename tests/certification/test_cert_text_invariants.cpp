// ═══════════════════════════════════════════════════════════════════════════
// tests/certification/test_cert_text_invariants.cpp
//
// Layout invariants regression locks for content/common/text/glyph_layout.hpp.
// `layout_glyphs` is the canonical single-source-of-truth throw on shape failure
// (per ADR-020 §fail-loud path; the previous `build_text_reveal_line:` prefix
// was refactored away to ensure tests assert against the SSOT function).
//
//   - layout_glyphs throws std::runtime_error on empty shaping (SSOT for
//     font resolution / AssetResolver errors — was a silent `return {}`).
//   - Diagnostic message preserves font_path + font_size + text snippet.
//     Prefix changed from "build_text_reveal_line:" to "layout_glyphs:" per
//     refactor(text): layout_glyphs single-source-of-truth throw on shape
//     failure (atomic commit on main, doc-sync SHA c1035372 workstream).
//
//   - Point 8 (refactor(text): consolidate shape_text via ShapedGlyphLine):
//     layout_glyphs is now a thin wrapper over shape_glyph_line() with
//     the fail-loud throw preserved verbatim.  These tests assert against
//     the SSOT post-Point-8 contract — wrap-pass is a regression of the
//     fail-loud path (e.g. if shape_glyph_line's fail-soft contract
//     bleeds into layout_glyphs, or if layout_glyphs drops the throw).
//
// Coverage intent: lock the fail-loud behavior so future refactors don't
// silently regress to a silent-empty return that masks font resolution bugs.
// See code-reviewer issue #4 (asymmetric error handling between
// layout_glyphs and build_text_reveal_line).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include "content/common/text/glyph_layout.hpp"

#include <stdexcept>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

TEST_CASE("layout_glyphs throws std::runtime_error on non-existent font path") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec bad_spec{"assets/fonts/NonExistentFont.ttf", "Unknown", 400};

    CHECK_THROWS_AS(
        chronon3d::content::text_reveal::layout_glyphs(
            "Hello", 72.0f, bad_spec, 4.0f, 0.0f, engine
        ),
        std::runtime_error
    );
}

TEST_CASE("layout_glyphs throw message carries font_path + text snippet") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec bad_spec{"assets/fonts/NonExistentFont.ttf", "Unknown", 400};

    try {
        chronon3d::content::text_reveal::layout_glyphs(
            "Hello", 72.0f, bad_spec, 4.0f, 0.0f, engine
        );
        FAIL("layout_glyphs did not throw on non-existent font path");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        // SSOT prefix from layout_glyphs (NOT build_text_reveal_line — refactored away).
        CHECK(msg.find("layout_glyphs") != std::string::npos);
        // Diagnostic fields preserved.
        CHECK(msg.find("font_path='assets/fonts/NonExistentFont.ttf'") != std::string::npos);
        CHECK(msg.find("font_size=72") != std::string::npos);
        CHECK(msg.find("text='Hello'") != std::string::npos);
        // Remediation hint preserved.
        CHECK(msg.find("AssetResolver") != std::string::npos);
    }
}
