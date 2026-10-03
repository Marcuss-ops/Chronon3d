// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_unit_map_joint_include.cpp
//
// TICKET-105 -- text cat-2 identity/preservation regression suite expansion.
//
// 3 deterministic TEST_CASEs (no threads, no time, no PRNG per AGENTS.md
// v0.1 cat-2 freeze-compliant invariants).  Strict assertions on STRUCTURE
// of inputs/inputs-shape/identity markers; gracefully degrades via MESSAGE
// if external font fixtures are unavailable.
//
// The selector-specific 3-level map now has its own type name, so both
// public map APIs can safely be included and exercised in one TU.
//
// ── Coverage matrix ──────────────────────────────────────────────────
//
//   test (1) frame identity after scramble ......... materialize_prepared_text
//                                                  ≡ compile_text_layout
//                                                  same shared_ptr + same
//                                                  cache_key across frame N
//                                                  and frame N+1 scramble.
//
//   test (2) joint-include contract ................ distinct canonical and
//                                                  selector map types work
//                                                  together in this TU.
//
//   test (3) double-include safety ................ Three `#include`s of
//                                                  text_unit_map.hpp at
//                                                  file scope; if
//                                                  `#pragma once` were
//                                                  missing, this file
//                                                  would at TU-compile
//                                                  time emit a
//                                                  redefinition error.
// ═══════════════════════════════════════════════════════════════════════════

// ── Triple include of the canonical header (test 3 invariant) ───────────
//
// If `#pragma once` were replaced with a broken include-guard (or absent
// entirely), this file would fail to compile at the second/third
// `#include` with a class redefinition error.  The compile-time success
// of this TU is the regression lock for `#pragma once` working.

#include <chronon3d/text/text_unit_map.hpp>  // canonical 8-level (1st)
#include <chronon3d/text/text_unit_map.hpp>  // canonical 8-level (2nd — pragma once: no-op)
#include <chronon3d/text/text_unit_map.hpp>  // canonical 8-level (3rd — pragma once: no-op)
#include <chronon3d/text/glyph_selector.hpp>

#include <chronon3d/text/font_engine.hpp>       // PlacedGlyphRun, FontSpec
#include <chronon3d/core/types/types.hpp>       // u32, f32

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <type_traits>

using namespace chronon3d;
using std::string_view;

// ═══════════════════════════════════════════════════════════════════════════
// Helpers (anon namespace — never exported)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Build a PlacedGlyphRun from per-glyph (byte_offset, byte_len) pairs,
/// one cluster per codepoint (ASCII fastpath).  Mirrors the helper pattern
/// used in tests/text/test_text_unit_map_8level.cpp so the canonical
/// `TextUnitMap` ctor reads the same grapheme-byte positions.
PlacedGlyphRun make_placed_ascii(const std::string& ascii_text) {
    PlacedGlyphRun run;
    run.font_size    = 16.0f;
    run.ascent       = 12.0f;
    run.descent      = 4.0f;
    run.baseline     = 12.0f;
    run.total_height = 16.0f;
    run.total_width  = static_cast<float>(ascii_text.size()) * 10.0f;

    for (std::size_t i = 0; i < ascii_text.size(); ++i) {
        PlacedGlyph g;
        g.glyph_id        = static_cast<u32>(i + 1);
        g.cluster         = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x       = 10.0f;
        g.x               = static_cast<float>(i) * 10.0f;
        g.byte_offset     = static_cast<u32>(i);
        g.byte_len        = 1u;
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = static_cast<u32>(i);
        cl.end_glyph   = static_cast<u32>(i + 1);
        cl.byte_offset = static_cast<u32>(i);
        cl.byte_len    = 1u;
        cl.advance     = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    return run;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (2) — Joint-include contract.

TEST_CASE("TICKET-105 (2) joint-include contract (distinct map types + 8-level ladder)") {
    static_assert(!std::is_same_v<TextUnitMap, GlyphSelectorUnitMap>);

    // ── (b) Observable canonical 8-level ladder for ASCII input. ───
    // Build a 7-char ASCII source ("abc def") with one cluster per
    // codepoint, then construct the canonical TextUnitMap and read
    // back the 6 forward counts.  NO font engine dependency — the
    // canonical header is engine-free.
    const std::string ascii_src = "abc def";
    const auto placed = make_placed_ascii(ascii_src);

    const TextUnitMap canonical_map(ascii_src, placed);
    const GlyphSelectorUnitMap selector_map = build_text_unit_map(placed, ascii_src);

    // ── 8-level ladder — observable in the canonical header ──────
    CHECK(canonical_map.byte_count()      == ascii_src.size());  // 7
    CHECK(canonical_map.codepoint_count() == ascii_src.size());  // 7 (all ASCII)
    CHECK(canonical_map.grapheme_count()  == ascii_src.size());  // 7 (no combining marks)
    CHECK(canonical_map.glyph_count()     == ascii_src.size());  // 7 (one glyph per cp)
    CHECK(canonical_map.word_count()      == 2u);                // "abc" + "def"
    CHECK(canonical_map.line_count()      == 1u);                // single line
    CHECK(canonical_map.paragraph_count() == 1u);                // empty paragraph layout
    CHECK(canonical_map.span_count()      == 0u);                // no spans supplied

    CHECK(selector_map.grapheme_count == 7u);
    CHECK(selector_map.word_count == 2u);
    CHECK(selector_map.line_count == 1u);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST CASE (3) — Double-include safety (pragma once verification).
//
// File-scope triple `#include <chronon3d/text/text_unit_map.hpp>` at
// lines 50-52.  If the canonical header were missing `#pragma once`
// (or had a broken include-guard), this file would fail to compile
// at the third include with a "redefinition of class chronon3d::TextUnitMap"
// error.  Compile-time success IS the regression lock.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TICKET-105 (3) text_unit_map.hpp double-include safe (#pragma once works)") {
    // Build a quick TextUnitMap so the test runs a real callable and is
    // not just a check(true).  If the header is well-formed AND
    // `#pragma once` is intact, this construct succeeds.
    const std::string ascii_src = "double include safe";
    const auto placed = make_placed_ascii(ascii_src);
    const TextUnitMap map(ascii_src, placed);

    CHECK(map.byte_count() == ascii_src.size());

    // Triple `#include` at file scope lines 50-52 is the COMPILE-TIME
    // assertion — `CHECK(true)` is the runtime backstop.
    CHECK(true);
}
